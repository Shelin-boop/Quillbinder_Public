#include "core/CrashGuard.h"
// Windows (port, W2, 2026-11-07): crash and freeze reports, sessions, saving when Windows ends
// the session. The reports have the format of the Linux ones (core/CrashGuard.cpp; read by
// core/CrashGuardCommon.cpp), with Windows' words where Linux has its own:
//  - Crashes: an unhandled-exception filter, SIGABRT (abort, qFatal, std::terminate), the C
//    library's invalid-parameter and pure-call handlers. The faulting thread only copies the
//    processor state and wakes a writer thread made at install(): the faulting thread may hold
//    the heap's lock or have no stack left. The writer uses no heap memory: fixed buffers and
//    Windows calls. It writes the text report, then a minidump (crash-<time>-<pid>.dmp) beside
//    it, then the exception goes on to Windows as before (Windows Error Reporting ends the
//    process).
//  - The backtrace is unwound from the saved state with the unwind tables of the program files
//    (RtlVirtualUnwind), as "file+offset"; the symbols file of the version (.pdb) turns the
//    offsets into function names.
//  - Freezes: the watchdog as on Linux; the main thread's state is taken with SuspendThread /
//    GetThreadContext / ResumeThread and unwound after it runs again (no lock is held meanwhile).
//  - Sessions: a running process is recognized by its program file name and its start time
//    (process ids come back quickly on Windows; there is no boot id).
//  - WM_ENDSESSION (log off, shutdown, restart): the work is saved, then the process ends, as
//    SIGTERM does on Linux.
#ifdef Q_OS_WIN
#include "AppInfo.h"
#include "core/Paths.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <windows.h>
// after windows.h
#include <dbghelp.h>
#include <psapi.h>

#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif

namespace qb {
namespace crash {

namespace {

// ---------------------------------------------------------------- state read by the writers
// Everything the writers touch is prepared here in advance (no heap memory while writing).

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
QMutex g_msgMutex; // between writers of messages (Qt messages come from every thread)
QtMessageHandler g_prevHandler = nullptr;
QStringList g_log; // all messages for the log console (not used by the writers)
quint64 g_logCount = 0;
QMutex g_logMutex;

wchar_t g_dir[1024];  // the report folder with '\' at the end
long long g_start = 0; // unix time of install()
DWORD g_mainTid = 0;
HANDLE g_mainThread = nullptr;
std::atomic<int> g_busy{0};
bool g_installed = false;

long long unixNow()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

long long fileTimeToUnix(const FILETIME &ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

// milliseconds that stand still while the computer sleeps (like CLOCK_MONOTONIC on Linux)
long long monoMs()
{
    ULONGLONG t = 0;
    ::QueryUnbiasedInterruptTime(&t);
    return (long long)(t / 10000ULL);
}

// ---------------------------------------------------------------- output without heap memory

void put(HANDLE h, const char *s, size_t n)
{
    while (n > 0) {
        DWORD w = 0;
        if (!::WriteFile(h, s, DWORD(std::min<size_t>(n, 1 << 20)), &w, nullptr) || w == 0)
            return;
        s += w;
        n -= w;
    }
}
void put(HANDLE h, const char *s) { put(h, s, std::strlen(s)); }

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
void putNum(HANDLE h, long long v)
{
    char b[32];
    if (v < 0) {
        put(h, "-");
        v = -v;
    }
    put(h, b, size_t(formatNum(b, (unsigned long long)v, 10)));
}
void putUNum(HANDLE h, unsigned long long v)
{
    char b[32];
    put(h, b, size_t(formatNum(b, v, 10)));
}
void putHex(HANDLE h, unsigned long long v)
{
    char b[32];
    put(h, "0x");
    put(h, b, size_t(formatNum(b, v, 16)));
}

int formatNumW(wchar_t *out, unsigned long long v)
{
    char b[32];
    const int n = formatNum(b, v, 10);
    for (int i = 0; i <= n; ++i)
        out[i] = wchar_t(b[i]);
    return n;
}

void putRing(HANDLE h, char (*ring)[kNoteLen], unsigned count, int size)
{
    const unsigned n = std::min<unsigned>(count, unsigned(size));
    if (n == 0)
        put(h, "(none)\n");
    for (unsigned i = count - n; i < count; ++i)
        put(h, ring[i % unsigned(size)]);
}
void putMsgs(HANDLE h)
{
    const unsigned count = g_msgCount.load();
    const unsigned n = std::min<unsigned>(count, unsigned(kMsgs));
    if (n == 0)
        put(h, "(none)\n");
    for (unsigned i = count - n; i < count; ++i)
        put(h, g_msgs[i % unsigned(kMsgs)]);
}

// <report folder><kind>-<unix time>-<pid><ext> into path (sizeof g_dir + 64 wide characters)
void reportPath(wchar_t *path, const wchar_t *kind, long long when, const wchar_t *ext)
{
    size_t len = std::wcslen(g_dir);
    std::wmemcpy(path, g_dir, len);
    const size_t k = std::wcslen(kind);
    std::wmemcpy(path + len, kind, k);
    len += k;
    path[len++] = L'-';
    len += size_t(formatNumW(path + len, (unsigned long long)when));
    path[len++] = L'-';
    len += size_t(formatNumW(path + len, (unsigned long long)::GetCurrentProcessId()));
    std::wmemcpy(path + len, ext, std::wcslen(ext) + 1);
}

// a wide path as UTF-8 into out (cap bytes), only the file name when nameOnly
void utf8(const wchar_t *w, char *out, int cap, bool nameOnly)
{
    if (nameOnly) {
        const wchar_t *slash = std::wcsrchr(w, L'\\');
        if (slash)
            w = slash + 1;
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, nullptr, nullptr);
    if (n <= 0)
        out[0] = 0;
    out[cap - 1] = 0;
}

// ---------------------------------------------------------------- program files and stacks

constexpr int kMods = 512;
struct Modules {
    HMODULE handle[kMods];
    unsigned long long base[kMods];
    unsigned long long size[kMods];
    int count = 0;
    wchar_t wname[2048];
    char name[4096];
};
Modules g_crashMods; // the crash writer's
Modules g_freezeMods; // the watchdog's

void listModules(Modules &m)
{
    m.count = 0;
    DWORD need = 0;
    if (!::K32EnumProcessModules(::GetCurrentProcess(), m.handle, DWORD(sizeof m.handle), &need))
        return;
    const int n = int(std::min<DWORD>(DWORD(need / sizeof(HMODULE)), DWORD(kMods)));
    for (int i = 0; i < n; ++i) {
        MODULEINFO mi{};
        if (::K32GetModuleInformation(::GetCurrentProcess(), m.handle[i], &mi, DWORD(sizeof mi))) {
            m.base[i] = (unsigned long long)(uintptr_t)mi.lpBaseOfDll;
            m.size[i] = mi.SizeOfImage;
        } else {
            m.base[i] = m.size[i] = 0;
        }
    }
    m.count = n;
}

// "name+0xoffset" of the program file that holds address pc
void putWhere(HANDLE h, Modules &m, unsigned long long pc)
{
    for (int i = 0; i < m.count; ++i)
        if (pc >= m.base[i] && pc < m.base[i] + m.size[i]) {
            if (!::GetModuleFileNameW(m.handle[i], m.wname, DWORD(sizeof m.wname / sizeof m.wname[0])))
                m.wname[0] = 0;
            utf8(m.wname, m.name, int(sizeof m.name), true);
            put(h, m.name[0] ? m.name : "?");
            put(h, "+");
            putHex(h, pc - m.base[i]);
            return;
        }
    put(h, "?");
}

void putModules(HANDLE h, Modules &m)
{
    for (int i = 0; i < m.count; ++i) {
        if (!::GetModuleFileNameW(m.handle[i], m.wname, DWORD(sizeof m.wname / sizeof m.wname[0])))
            m.wname[0] = 0;
        utf8(m.wname, m.name, int(sizeof m.name), false);
        putHex(h, m.base[i]);
        put(h, "-");
        putHex(h, m.base[i] + m.size[i]);
        put(h, " ");
        put(h, m.name[0] ? m.name : "?");
        put(h, "\n");
    }
}

// Unwinds a stack from a processor state, innermost first; stops at the first frame it cannot
// unwind. Reading a damaged stack can fault: that ends the walk, not the writer.
int walkStack(const CONTEXT *start, unsigned long long *frames, int max)
{
    int n = 0;
#if defined(_M_X64) || defined(_M_AMD64)
#ifdef _MSC_VER
    __try {
#endif
        CONTEXT ctx = *start;
        DWORD64 lastSp = 0;
        while (n < max) {
            const DWORD64 pc = ctx.Rip;
            if (!pc || (n > 0 && ctx.Rsp <= lastSp))
                break;
            lastSp = ctx.Rsp;
            frames[n++] = pc;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = ::RtlLookupFunctionEntry(pc, &imageBase, nullptr);
            if (fn) {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                ::RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, pc, fn, &ctx, &handlerData, &establisher, nullptr);
            } else { // a leaf function: the return address is on top of the stack
                ctx.Rip = *reinterpret_cast<const DWORD64 *>(ctx.Rsp);
                ctx.Rsp += 8;
            }
        }
#ifdef _MSC_VER
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif
#else
    (void)start;
    (void)frames;
    (void)max;
#endif
    return n;
}

void putFrames(HANDLE h, Modules &m, const unsigned long long *frames, int n)
{
    if (n == 0)
        put(h, "(none could be read)\n");
    for (int i = 0; i < n; ++i) {
        put(h, "#");
        putNum(h, i);
        put(h, "  ");
        putHex(h, frames[i]);
        put(h, "  ");
        putWhere(h, m, frames[i]);
        put(h, "\n");
    }
}

void putProcess(HANDLE h)
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = DWORD(sizeof pmc);
    if (::K32GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), DWORD(sizeof pmc))) {
        put(h, "Working set: ");
        putUNum(h, pmc.WorkingSetSize / 1024);
        put(h, " kB\nPeak working set: ");
        putUNum(h, pmc.PeakWorkingSetSize / 1024);
        put(h, " kB\nPrivate memory: ");
        putUNum(h, pmc.PrivateUsage / 1024);
        put(h, " kB\nPeak private memory: ");
        putUNum(h, pmc.PeakPagefileUsage / 1024);
        put(h, " kB\n");
    }
    DWORD handles = 0;
    if (::GetProcessHandleCount(::GetCurrentProcess(), &handles)) {
        put(h, "Handles: ");
        putUNum(h, handles);
        put(h, "\n");
    }
}

void putShared(HANDLE h, const char *when)
{
    put(h, "\n== Program and system (prepared before the ");
    put(h, when);
    put(h, ")\n");
    const int idx = g_snapIdx.load();
    if (g_snapLen[idx] > 0)
        put(h, g_snap[idx], size_t(g_snapLen[idx]));
    else
        put(h, "(not prepared yet)\n");

    put(h, "\n== Process at the ");
    put(h, when);
    put(h, "\n");
    putProcess(h);

    put(h, "\n== Recent actions (oldest first; no text of documents)\n");
    putRing(h, g_notes, g_noteCount.load(), kNotes);

    put(h, "\n== Recent warnings of the program\n");
    putMsgs(h);
}

// ---------------------------------------------------------------- crashes

constexpr DWORD kStatusInvalidParameter = 0xC0000417;
constexpr DWORD kStatusHeapCorruption = 0xC0000374;
constexpr DWORD kCppException = 0xE06D7363;

struct Request {
    DWORD code = 0;     // exception code, or the number of a signal / handler
    int kind = 0;       // 0 exception, 1 SIGABRT, 2 invalid parameter, 3 pure virtual call
    DWORD tid = 0;
    long long when = 0;
    EXCEPTION_RECORD record;
    CONTEXT ctx;
    EXCEPTION_POINTERS own;
    EXCEPTION_POINTERS *ep = nullptr; // the system's, or &own
};
Request g_req;
HANDLE g_requestEvent = nullptr, g_doneEvent = nullptr;
wchar_t g_txtPath[sizeof g_dir / sizeof g_dir[0] + 64];
wchar_t g_dmpPath[sizeof g_dir / sizeof g_dir[0] + 64];
char g_dmpName[256];
unsigned long long g_crashFrames[128];

void describe(const Request &r, const char **name, const char **meaning)
{
    *name = "EXCEPTION";
    *meaning = "an error the program could not handle";
    if (r.kind == 1) {
        *name = "SIGABRT";
        *meaning = "abort: the program stopped itself after an internal error";
        return;
    }
    if (r.kind == 2) {
        *name = "INVALID_PARAMETER";
        *meaning = "a function of the C library was called with a value it does not accept";
        return;
    }
    if (r.kind == 3) {
        *name = "PURE_VIRTUAL_CALL";
        *meaning = "a function of an object that no longer exists was called";
        return;
    }
    switch (r.code) {
    case EXCEPTION_ACCESS_VIOLATION:
        *name = "EXCEPTION_ACCESS_VIOLATION";
        *meaning = "access violation: the program used memory it must not use";
        break;
    case EXCEPTION_IN_PAGE_ERROR:
        *name = "EXCEPTION_IN_PAGE_ERROR";
        *meaning = "memory that cannot be read (often a file on a drive or network that went away)";
        break;
    case EXCEPTION_STACK_OVERFLOW:
        *name = "EXCEPTION_STACK_OVERFLOW";
        *meaning = "stack overflow: too deep a chain of calls";
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        *name = "EXCEPTION_ILLEGAL_INSTRUCTION";
        *meaning = "illegal instruction";
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
        *name = "EXCEPTION_ARITHMETIC";
        *meaning = "arithmetic error";
        break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        *name = "EXCEPTION_DATATYPE_MISALIGNMENT";
        *meaning = "misaligned memory access";
        break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        *name = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        *meaning = "an index outside an array";
        break;
    case kStatusHeapCorruption:
        *name = "STATUS_HEAP_CORRUPTION";
        *meaning = "the program's memory management was damaged";
        break;
    case kCppException:
        *name = "CPP_EXCEPTION";
        *meaning = "an error the program did not handle (C++ exception)";
        break;
    default:
        break;
    }
}

void writeCrash()
{
    Request &r = g_req;
    const char *name = nullptr, *meaning = nullptr;
    describe(r, &name, &meaning);
    reportPath(g_txtPath, L"crash", r.when, L".txt");
    reportPath(g_dmpPath, L"crash", r.when, L".dmp");
    utf8(g_dmpPath, g_dmpName, int(sizeof g_dmpName), true);
    listModules(g_crashMods);

    const HANDLE h = g_dir[0] ? ::CreateFileW(g_txtPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr)
                              : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        put(h, "Quillbinder crash report\nFormat: 1\nVersion: " QB_APP_VERSION "\nSignal: ");
        putUNum(h, r.code);
        put(h, " ");
        put(h, name);
        put(h, " (");
        put(h, meaning);
        put(h, ")\n");
        if (r.kind == 0) {
            put(h, "Exception code: ");
            putHex(h, r.code);
            put(h, " at ");
            putHex(h, (unsigned long long)(uintptr_t)r.record.ExceptionAddress);
            put(h, " (");
            putWhere(h, g_crashMods, (unsigned long long)(uintptr_t)r.record.ExceptionAddress);
            put(h, ")\n");
            if ((r.code == EXCEPTION_ACCESS_VIOLATION || r.code == EXCEPTION_IN_PAGE_ERROR)
                && r.record.NumberParameters >= 2) {
                const ULONG_PTR how = r.record.ExceptionInformation[0];
                put(h, "Access: ");
                put(h, how == 0 ? "read" : how == 1 ? "write" : how == 8 ? "execute" : "?");
                put(h, " at ");
                putHex(h, (unsigned long long)r.record.ExceptionInformation[1]);
                put(h, "\n");
            }
        }
        put(h, "Unix time: ");
        putNum(h, r.when);
        put(h, "\nSeconds since start: ");
        putNum(h, r.when - g_start);
        put(h, "\nProcess: ");
        putUNum(h, ::GetCurrentProcessId());
        put(h, "\nThread: ");
        putUNum(h, r.tid);
        put(h, r.tid == g_mainTid ? " (the main thread)\n" : " (another thread)\n");
        put(h, "Memory dump: ");
        put(h, g_dmpName);
        put(h, " (beside this report; for the developer. It can hold pieces of the text you were working on:\n"
               " send it only if you want to.)\n");

        putShared(h, "crash");

        put(h, "\n== Backtrace (innermost first)\n"
               "(Program file and offset in it; the symbols file of this version (.pdb) turns the offsets into\n"
               " function names.)\n");
        const int n = walkStack(&r.ctx, g_crashFrames, int(sizeof g_crashFrames / sizeof g_crashFrames[0]));
        putFrames(h, g_crashMods, g_crashFrames, n);

        put(h, "\n== Program files in memory (to translate the addresses above)\n");
        putModules(h, g_crashMods);
        put(h, "\n== End\n");
        ::FlushFileBuffers(h);
        ::CloseHandle(h);
        const HANDLE err = ::GetStdHandle(STD_ERROR_HANDLE);
        if (err && err != INVALID_HANDLE_VALUE) {
            put(err, "\nQuillbinder ended because of an error. A crash report was written.\n");
            ::FlushFileBuffers(err);
        }
    }
    // the memory dump last: if it cannot be written, the report is there already
    const HANDLE d = g_dir[0] ? ::CreateFileW(g_dmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr)
                              : INVALID_HANDLE_VALUE;
    if (d != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = r.tid;
        mei.ExceptionPointers = r.ep;
        mei.ClientPointers = FALSE;
        const MINIDUMP_TYPE type =
            MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), d, type, &mei, nullptr, nullptr);
        ::CloseHandle(d);
    }
}

DWORD WINAPI writerMain(LPVOID)
{
    for (;;) {
        if (::WaitForSingleObject(g_requestEvent, INFINITE) != WAIT_OBJECT_0)
            return 0;
        writeCrash();
        ::SetEvent(g_doneEvent);
    }
}

// Called in the thread that failed. ep: the system's exception pointers, or null (a handler of
// the C library): then the state of this thread is taken here.
void crashNow(int kind, DWORD code, EXCEPTION_POINTERS *ep)
{
    if (g_busy.exchange(1) != 0) {
        ::Sleep(10000); // another thread writes the report; the process ends when it is done
        return;
    }
    Request &r = g_req;
    r.kind = kind;
    r.code = code;
    r.tid = ::GetCurrentThreadId();
    r.when = unixNow();
    if (ep && ep->ExceptionRecord && ep->ContextRecord) {
        r.record = *ep->ExceptionRecord;
        r.ctx = *ep->ContextRecord;
        r.ep = ep;
    } else {
        ::RtlCaptureContext(&r.ctx);
        std::memset(&r.record, 0, sizeof r.record);
        r.record.ExceptionCode = code;
#if defined(_M_X64) || defined(_M_AMD64)
        r.record.ExceptionAddress = reinterpret_cast<PVOID>(r.ctx.Rip);
#endif
        r.own.ExceptionRecord = &r.record;
        r.own.ContextRecord = &r.ctx;
        r.ep = &r.own;
    }
    if (g_requestEvent && g_doneEvent) {
        ::SetEvent(g_requestEvent);
        ::WaitForSingleObject(g_doneEvent, 60000);
    } else {
        writeCrash();
    }
}

LONG WINAPI onException(EXCEPTION_POINTERS *ep)
{
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    crashNow(0, code, ep);
    // on to Windows as before (Windows Error Reporting, the exit code of the exception)
    return EXCEPTION_CONTINUE_SEARCH;
}

void onAbortSignal(int)
{
    crashNow(1, SIGABRT, nullptr);
    std::signal(SIGABRT, SIG_DFL); // the C library ends the process after this handler
}

void onInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    crashNow(2, kStatusInvalidParameter, nullptr);
    ::TerminateProcess(::GetCurrentProcess(), kStatusInvalidParameter);
}

void onPureCall()
{
    crashNow(3, 25, nullptr);
    ::TerminateProcess(::GetCurrentProcess(), 3);
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

std::atomic<long long> g_beatMs{0};
std::atomic<int> g_freezeSeconds{30};
bool g_watchdog = false;
wchar_t g_freezePath[sizeof g_dir / sizeof g_dir[0] + 64];
unsigned long long g_freezeFrames[128];
CONTEXT g_freezeCtx;

// writes freeze-<time>-<pid>.txt into g_freezePath; false when it could not be written
bool writeFreeze(long long stalledMs)
{
    if (!g_dir[0])
        return false;
    const long long now = unixNow();
    reportPath(g_freezePath, L"freeze", now, L".txt");
    const HANDLE h = ::CreateFileW(g_freezePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    put(h, "Quillbinder freeze report\nFormat: 1\nVersion: " QB_APP_VERSION
           "\nKind: freeze (the program did not respond)\nNot responding for: ");
    putNum(h, stalledMs / 1000);
    put(h, " s\nUnix time: ");
    putNum(h, now);
    put(h, "\nSeconds since start: ");
    putNum(h, now - g_start);
    put(h, "\nProcess: ");
    putUNum(h, ::GetCurrentProcessId());
    put(h, "\n");
    putShared(h, "freeze");

    put(h, "\n== Where the main thread is (innermost first)\n"
           "(Program file and offset in it; the symbols file of this version (.pdb) turns the offsets into\n"
           " function names.)\n");
    // the state is taken while the main thread is held for a moment; it is unwound after it
    // runs again, so nothing here waits for a lock the main thread may hold
    bool got = false;
    if (g_mainThread && ::SuspendThread(g_mainThread) != DWORD(-1)) {
        std::memset(&g_freezeCtx, 0, sizeof g_freezeCtx);
        g_freezeCtx.ContextFlags = CONTEXT_FULL;
        got = ::GetThreadContext(g_mainThread, &g_freezeCtx) != 0;
        ::ResumeThread(g_mainThread);
    }
    listModules(g_freezeMods);
    if (got)
        putFrames(h, g_freezeMods, g_freezeFrames,
                  walkStack(&g_freezeCtx, g_freezeFrames, int(sizeof g_freezeFrames / sizeof g_freezeFrames[0])));
    else
        put(h, "(the state of the main thread could not be read)\n");

    put(h, "\n== Program files in memory (to translate the addresses above)\n");
    putModules(h, g_freezeMods);
    put(h, "\n== End\n");
    ::FlushFileBuffers(h);
    ::CloseHandle(h);
    const HANDLE err = ::GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE)
        put(err, "\nQuillbinder has not responded for a while. A freeze report was written.\n");
    return true;
}

void watchdogLoop()
{
    bool frozen = false, written = false;
    long long lastBeat = 0;
    for (;;) {
        ::Sleep(500);
        const long long beat = g_beatMs.load();
        if (!frozen) {
            if (monoMs() - beat >= (long long)g_freezeSeconds.load() * 1000 && !::IsDebuggerPresent()
                && g_busy.load() == 0) {
                frozen = true;
                lastBeat = beat;
                written = writeFreeze(monoMs() - beat);
            }
        } else if (beat != lastBeat) { // the main thread answered again
            frozen = false;
            if (written) {
                const HANDLE h = ::CreateFileW(g_freezePath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                               FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    put(h, "\n== Responded again after ");
                    putNum(h, (beat - lastBeat) / 1000);
                    put(h, " s\n");
                    ::CloseHandle(h);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- end of the Windows session

std::function<void()> g_onTerm;

class EndSessionFilter : public QAbstractNativeEventFilter
{
public:
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
    {
        if (type != "windows_generic_MSG" || !message)
            return false;
        const MSG *m = static_cast<const MSG *>(message);
        // WM_ENDSESSION with wParam TRUE: the session ends (log off, shutdown, restart); Windows
        // ends the process soon after this message. Sent to every top-level window: the first one acts.
        if (m->message == WM_ENDSESSION && m->wParam && !m_done) {
            m_done = true;
            note(QStringLiteral("Windows ends the session: saving, then ending"));
            if (g_onTerm)
                g_onTerm();
            endSession();
            std::fflush(nullptr);
            ::_exit(0);
        }
        return false;
    }

private:
    bool m_done = false;
};

// ---------------------------------------------------------------- sessions

bool g_session = false;
QString g_sessionProject;
QDateTime g_sessionStart;

QString markerPath(qint64 pid) { return sessionDir() + QStringLiteral("/%1.json").arg(pid); }

void writeMarker()
{
    QDir().mkpath(sessionDir());
    QJsonObject o;
    o.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    o.insert(QStringLiteral("started"), g_sessionStart.toSecsSinceEpoch());
    o.insert(QStringLiteral("boot"), QString());
    o.insert(QStringLiteral("version"), QStringLiteral(QB_APP_VERSION));
    o.insert(QStringLiteral("project"), g_sessionProject);
    QSaveFile f(markerPath(QCoreApplication::applicationPid()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}

// a running process of this program that started when the marker says
bool isRunningInstance(qint64 pid, qint64 started)
{
    if (pid <= 0 || pid == QCoreApplication::applicationPid())
        return false;
    const HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return false;
    wchar_t image[2048];
    DWORD len = DWORD(sizeof image / sizeof image[0]);
    const bool named = ::QueryFullProcessImageNameW(h, 0, image, &len) != 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool timed = ::GetProcessTimes(h, &created, &exited, &kernel, &user) != 0;
    DWORD code = 0;
    const bool running = ::GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    ::CloseHandle(h);
    if (!named || !timed || !running)
        return false;
    const QString file = QFileInfo(QString::fromWCharArray(image, int(len))).fileName();
    if (file.compare(QStringLiteral(QB_APP_ID ".exe"), Qt::CaseInsensitive) != 0)
        return false;
    // the marker is written a moment after the process started
    const long long c = fileTimeToUnix(created);
    return started <= 0 || (c >= started - 600 && c <= started + 60);
}

// the build's id: the GUID and age of its symbols file (.pdb), from the program file's debug directory
QString pdbSignature()
{
    const auto *base = reinterpret_cast<const unsigned char *>(::GetModuleHandleW(nullptr));
    if (!base)
        return QString();
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return QString();
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return QString();
    const IMAGE_DATA_DIRECTORY &dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!dd.VirtualAddress || !dd.Size)
        return QString();
    const auto *dbg = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(base + dd.VirtualAddress);
    for (DWORD i = 0; i < dd.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
        if (dbg[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || !dbg[i].AddressOfRawData)
            continue;
        const unsigned char *cv = base + dbg[i].AddressOfRawData;
        if (std::memcmp(cv, "RSDS", 4) != 0)
            continue;
        GUID g;
        std::memcpy(&g, cv + 4, sizeof g);
        DWORD age = 0;
        std::memcpy(&age, cv + 4 + sizeof g, sizeof age);
        return QStringLiteral("%1%2%3%4%5")
            .arg(ulong(g.Data1), 8, 16, QLatin1Char('0'))
            .arg(uint(g.Data2), 4, 16, QLatin1Char('0'))
            .arg(uint(g.Data3), 4, 16, QLatin1Char('0'))
            .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(g.Data4), 8).toHex()))
            .arg(ulong(age), 0, 16)
            .toUpper();
    }
    return QString();
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
    g_start = unixNow();
    g_mainTid = ::GetCurrentThreadId();
    ::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &g_mainThread,
                      THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    QDir().mkpath(reportDir());
    const std::wstring dir = QDir::toNativeSeparators(reportDir() + QLatin1Char('/')).toStdWString();
    if (dir.size() < sizeof g_dir / sizeof g_dir[0])
        std::wmemcpy(g_dir, dir.c_str(), dir.size() + 1);
    // room to write the report after the main thread's stack ran out
    ULONG guarantee = 64 * 1024;
    ::SetThreadStackGuarantee(&guarantee);
    g_requestEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_doneEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_requestEvent && g_doneEvent) {
        if (HANDLE t = ::CreateThread(nullptr, 256 * 1024, writerMain, nullptr, 0, nullptr))
            ::CloseHandle(t);
        else
            g_requestEvent = g_doneEvent = nullptr;
    }
    // the C library's own messages and Windows' error report window are left out: the report is ours
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(onInvalidParameter);
    _set_purecall_handler(onPureCall);
    std::signal(SIGABRT, onAbortSignal);
    ::SetUnhandledExceptionFilter(onException);
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

QString buildId() { return pdbSignature(); }

qint64 secondsSinceStart() { return g_start ? qint64(unixNow() - g_start) : 0; }

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
    const QList<Report> all = reports();
    const QFileInfoList markers = QDir(sessionDir()).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &fi : markers) {
        QFile f(fi.absoluteFilePath());
        QJsonObject o;
        if (f.open(QIODevice::ReadOnly))
            o = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const qint64 pid = o.value(QStringLiteral("pid")).toVariant().toLongLong();
        const qint64 started = o.value(QStringLiteral("started")).toVariant().toLongLong();
        // another program window that is still running
        if (isRunningInstance(pid, started))
            continue;
        PreviousEnd e;
        e.pid = pid;
        e.started = QDateTime::fromSecsSinceEpoch(started);
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
    static EndSessionFilter *filter = nullptr;
    if (filter || !QCoreApplication::instance())
        return;
    filter = new EndSessionFilter;
    QCoreApplication::instance()->installNativeEventFilter(filter);
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

QString readable(const QString &text) { return text; }

} // namespace crash
} // namespace qb
#endif // Q_OS_WIN
