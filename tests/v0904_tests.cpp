// 0.90.4 beta (2026-11-13): the Windows requirements drawn from Linux 0.90.3 (project document
// claude/quillbinder-windows-requirements-0.90.4.md). Runs on Linux (the portable program's parts with
// QB_TEST_PORTABLE=1, set here) and on the Windows runner:
//  - W-03: the value of QT_QPA_PLATFORM that makes Qt tell AltGr from Ctrl+Alt (platform::qpaWithAltGr); on Windows
//    with the German keyboard also the keys themselves: AltGr+7 types "{" and does not run the command on Ctrl+Alt+7,
//    left Ctrl + left Alt + 7 runs it (real key presses through SendInput; left out with a note when the test window
//    cannot get the keyboard or the German layout cannot be switched on);
//  - W-01, W-02, W-10: the portable program's folder for one run (portable::sessionTemp): made in the temporary
//    folder, a folder left by a run that is gone removed, removed when the program ends (a child run); the copy of
//    a saved web page for the browser goes there and not into Downloads, the button says so;
//  - W-06: saved web pages in older character sets (windows-1252 with its curved quotes, dash and euro sign,
//    ISO 8859-15, an unknown name) read without ICU;
//  - W-12: the download page and the release text with the setup programs;
//  - W-08 (Windows only): a font with several designs (Windows 11's Sitka) is listed there as one family per design,
//    each with its own bold and italic, so that Bold and Italic stay within the design without the separation Linux
//    needs (text/FontFaces.cpp; left out with a note where Sitka is not installed).
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0904_tests /tmp/shots
#include "AppInfo.h"
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/Platform.h"
#include "core/Portable.h"
#include "core/Project.h"
#include "core/WebArchive.h"
#include "import/FileImporter.h"
#include "import/ImporterApi.h"
#include "ui/AppSettings.h"
#include "ui/LinkOpen.h"
#include "ui/MediaView.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <cstdio>
#include <memory>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace qb;

static int failures = 0;
static QString outDir;

static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}

[[maybe_unused]] static void note(const QString &text)
{
    std::printf("      (%s)\n", qPrintable(text));
    std::fflush(stdout);
}

static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

static void writeAll(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QByteArray pngBytes(const QColor &c)
{
    QImage img(24, 16, QImage::Format_RGB32);
    img.fill(c);
    QByteArray bytes;
    QBuffer b(&bytes);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    return bytes;
}

// ------------------------------------------------------------------------------------------------------------------
// W-03: the switch for Qt's Windows platform

static void altGrValue()
{
    const QString L = QStringLiteral("AltGr switch: ");
    struct {
        const char *given;
        const char *expected;
    } const cases[] = {{"", "windows:altgr"},
                       {"windows", "windows:altgr"},
                       {"windows:", "windows:altgr"},
                       {"windows:fontengine=freetype", "windows:fontengine=freetype,altgr"},
                       {"windows:darkmode=2,nocolorfonts", "windows:darkmode=2,nocolorfonts,altgr"},
                       {"windows:altgr", ""},
                       {"windows:darkmode=1,altgr", ""},
                       {"offscreen", ""},
                       {"offscreen:altgr=no", ""},
                       {"xcb", ""},
                       {"minimal", ""}};
    QStringList wrong;
    for (const auto &c : cases) {
        const QByteArray got = platform::qpaWithAltGr(c.given);
        if (got != c.expected)
            wrong << QStringLiteral("\"%1\" -> \"%2\", expected \"%3\"").arg(QLatin1String(c.given), QString::fromLatin1(got), QLatin1String(c.expected));
    }
    check(wrong.isEmpty(), L + QStringLiteral("no platform named, Windows' platform with and without switches, other platforms left alone"),
          wrong.join(QLatin1String("; ")));
}

#ifdef Q_OS_WIN
// W-08: on Linux fontconfig files Sitka's designs (Text, Small, Heading ...) under one family, and FontFaces.cpp
// separates them; Windows lists each design as a family of its own. Checked on what the machine has.
static void fontDesigns()
{
    const QString L = QStringLiteral("fonts with several designs (Windows): ");
    const QStringList families = QFontDatabase::families();
    QStringList sitka;
    for (const QString &f : families)
        if (f.startsWith(QLatin1String("Sitka")))
            sitka << f;
    if (!sitka.contains(QStringLiteral("Sitka Text"))) {
        note(QStringLiteral("Sitka is not installed on this machine (%1 Sitka families): the designs of a font are not checked here")
                 .arg(sitka.size()));
        return;
    }
    check(sitka.size() >= 2 && sitka.contains(QStringLiteral("Sitka Text")),
          L + QStringLiteral("each design of Sitka is a family of its own"), sitka.join(QLatin1String(", ")));
    QStringList wrong;
    for (const QString &family : sitka) {
        QFont bold(family, 12);
        bold.setBold(true);
        QFont italic(family, 12);
        italic.setItalic(true);
        const QFontInfo bi(bold), ii(italic);
        if (bi.family() != family || !bi.bold())
            wrong << QStringLiteral("%1 bold -> %2%3").arg(family, bi.family(), bi.bold() ? QString() : QStringLiteral(" (not bold)"));
        if (ii.family() != family || !ii.italic())
            wrong << QStringLiteral("%1 italic -> %2%3").arg(family, ii.family(), ii.italic() ? QString() : QStringLiteral(" (not italic)"));
    }
    check(wrong.isEmpty(), L + QStringLiteral("Bold and Italic of each design stay within that design"), wrong.join(QLatin1String("; ")));
}

static void sendKey(WORD vk, bool down, bool extended = false)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(in));
}

// Real key presses on the German keyboard: Windows turns the AltGr key into left Ctrl + right Alt, and without the
// switch Qt runs the command on Ctrl+Alt+7 where the writer wanted "{". A window with a text field and that command;
// AltGr+7 is pressed, then (when "alsoCommand") left Ctrl + left Alt + 7. False with the reason when the keys cannot
// be pressed on this machine.
struct KeyResult {
    QString typedByAltGr;
    int firedByAltGr = 0;
    QString typedByCtrlAlt;
    int firedByCtrlAlt = 0;
    bool arrived = false;
};

static bool pressKeys(KeyResult *r, QString *why, bool alsoCommand, const QString &shotName)
{
    QWidget win;
    win.setWindowTitle(QStringLiteral("AltGr check"));
    auto *lay = new QVBoxLayout(&win);
    auto *edit = new QPlainTextEdit(&win);
    lay->addWidget(edit);
    int fired = 0;
    QShortcut command(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_7), &win);
    command.setContext(Qt::ApplicationShortcut);
    QObject::connect(&command, &QShortcut::activated, &win, [&fired] { ++fired; });
    win.resize(420, 200);
    win.show();
    win.raise();
    win.activateWindow();
    edit->setFocus();
    settle(400);
    const HWND hwnd = HWND(win.winId());
    SetForegroundWindow(hwnd);
    settle(300);
    const HKL before = GetKeyboardLayout(0);
    const HKL german = LoadKeyboardLayoutW(L"00000407", KLF_ACTIVATE | KLF_SUBSTITUTE_OK);
    if (german)
        ActivateKeyboardLayout(german, 0);
    settle(300);
    const bool front = GetForegroundWindow() == hwnd && edit->hasFocus();
    const bool layout = german && (quintptr(GetKeyboardLayout(0)) & 0xffff) == 0x0407;
    if (!front || !layout) {
        *why = (front ? QString() : QStringLiteral("the test window does not have the keyboard on this machine; "))
               + (layout ? QString() : QStringLiteral("the German keyboard layout cannot be switched on"));
        if (before)
            ActivateKeyboardLayout(before, 0);
        return false;
    }
    // AltGr + 7
    sendKey(VK_RMENU, true, true);
    settle(60);
    sendKey(WORD('7'), true);
    settle(60);
    sendKey(WORD('7'), false);
    settle(60);
    sendKey(VK_RMENU, false, true);
    settle(400);
    if (!shotName.isEmpty())
        win.grab().save(outDir + QLatin1Char('/') + shotName);
    r->typedByAltGr = edit->toPlainText();
    r->firedByAltGr = fired;
    r->arrived = !r->typedByAltGr.isEmpty() || fired > 0;
    if (r->arrived && alsoCommand) {
        // left Ctrl + left Alt + 7
        fired = 0;
        edit->clear();
        sendKey(VK_LCONTROL, true);
        settle(40);
        sendKey(VK_LMENU, true);
        settle(40);
        sendKey(WORD('7'), true);
        settle(60);
        sendKey(WORD('7'), false);
        settle(40);
        sendKey(VK_LMENU, false);
        settle(40);
        sendKey(VK_LCONTROL, false);
        settle(400);
        r->typedByCtrlAlt = edit->toPlainText();
        r->firedByCtrlAlt = fired;
    }
    // every key up again, whatever happened
    for (WORD vk : {WORD(VK_RMENU), WORD(VK_LMENU), WORD(VK_LCONTROL), WORD(VK_RCONTROL)})
        sendKey(vk, false, vk == VK_RMENU || vk == VK_RCONTROL);
    if (before)
        ActivateKeyboardLayout(before, 0);
    settle(100);
    if (!r->arrived)
        *why = QStringLiteral("no key press arrived in the test window on this machine");
    return r->arrived;
}

static void altGrKeys()
{
    const QString L = QStringLiteral("AltGr keys (German keyboard): ");
    if (!qgetenv("QT_QPA_PLATFORM").contains("altgr")) {
        check(false, L + QStringLiteral("the test program runs with the switch"), QString::fromLatin1(qgetenv("QT_QPA_PLATFORM")));
        return;
    }
    KeyResult r;
    QString why;
    if (!pressKeys(&r, &why, true, QStringLiteral("v0904-altgr.png"))) {
        // (a locked or remote desktop swallows injected keys: nothing learned, not a failure)
        note(QStringLiteral("AltGr keys not checked: ") + why);
        return;
    }
    check(r.typedByAltGr == QLatin1String("{") && r.firedByAltGr == 0, L + QStringLiteral("AltGr+7 types \"{\" and does not run the command on Ctrl+Alt+7"),
          QStringLiteral("typed \"%1\", command run %2 time(s)").arg(r.typedByAltGr).arg(r.firedByAltGr));
    check(r.firedByCtrlAlt == 1 && r.typedByCtrlAlt.isEmpty(), L + QStringLiteral("left Ctrl + left Alt + 7 runs the command and types nothing"),
          QStringLiteral("typed \"%1\", command run %2 time(s)").arg(r.typedByCtrlAlt).arg(r.firedByCtrlAlt));
    // for the record: the same keys in a run of Qt as it comes, without the switch (what the writer would have met)
    AllowSetForegroundWindow(ASFW_ANY);
    QProcess plain;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("windows"));
    plain.setProcessEnvironment(env);
    plain.setProcessChannelMode(QProcess::MergedChannels);
    plain.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-altgr-plain")});
    plain.waitForFinished(60000);
    QString line;
    for (const QString &l : QString::fromLocal8Bit(plain.readAll()).split(QLatin1Char('\n')))
        if (l.startsWith(QLatin1String("plain: ")))
            line = l.mid(7).trimmed();
    note(QStringLiteral("without the switch, AltGr+7 in Qt %1: %2").arg(QLatin1String(qVersion()), line.isEmpty() ? QStringLiteral("no answer") : line));
}

static int childAltGrPlain()
{
    KeyResult r;
    QString why;
    if (pressKeys(&r, &why, false, QString()))
        std::printf("plain: typed \"%s\", the command on Ctrl+Alt+7 run %d time(s)\n", qPrintable(r.typedByAltGr), r.firedByAltGr);
    else
        std::printf("plain: keys not pressed (%s)\n", qPrintable(why));
    std::fflush(stdout);
    return 0;
}
#endif

// ------------------------------------------------------------------------------------------------------------------
// W-01, W-02, W-10: the portable program's folder for one run

static void sessionFolder()
{
    const QString L = QStringLiteral("folder for this run: ");
    check(portable::active(), L + QStringLiteral("(the portable program: Windows, or QB_TEST_PORTABLE here)"));
    const QString base = QDir::tempPath() + QLatin1String("/" QB_APP_ID);
    // what a run that ended without its clean-up left behind, and a folder that is not a run's
    const QString stale = base + QLatin1String("/run-999999937");
    const QString other = base + QLatin1String("/run-notanumber");
    writeAll(stale + QLatin1String("/web pages/x/index.html"), "<html></html>");
    writeAll(other + QLatin1String("/keep.txt"), "kept");
    const QString dir = portable::sessionTemp();
    check(dir == base + QStringLiteral("/run-%1").arg(QCoreApplication::applicationPid()) && QFileInfo(dir).isDir(),
          L + QStringLiteral("made in the temporary folder, named after this run"), dir);
    check(!QFileInfo::exists(stale), L + QStringLiteral("the folder of a run that is gone is removed"));
    check(QFileInfo::exists(other + QLatin1String("/keep.txt")), L + QStringLiteral("a folder that is no run's is left alone"));
    QDir(other).removeRecursively();
    writeAll(dir + QLatin1String("/probe.txt"), "x");
    check(portable::sessionTemp() == dir && QFileInfo::exists(dir + QLatin1String("/probe.txt")),
          L + QStringLiteral("asked again: the same folder, its files still there"));
    QFile::remove(dir + QLatin1String("/probe.txt"));
    // a child run makes its folder, puts a file into it and ends: the folder is gone
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-session")});
    const bool ran = child.waitForFinished(60000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
    const QString printed = QString::fromLocal8Bit(child.readAll());
    QString childDir;
    for (const QString &line : printed.split(QLatin1Char('\n')))
        if (line.startsWith(QLatin1String("session: ")))
            childDir = QDir::fromNativeSeparators(line.mid(9).trimmed());
    check(ran && childDir.startsWith(base + QLatin1String("/run-")) && childDir != dir && printed.contains(QLatin1String("file: yes")),
          L + QStringLiteral("another run has a folder of its own"), printed.left(600));
    check(!childDir.isEmpty() && !QFileInfo::exists(childDir), L + QStringLiteral("removed when that run ended"), childDir);
    check(QFileInfo(dir).isDir(), L + QStringLiteral("this run's folder is still there"));
}

static int childSession()
{
    const QString dir = portable::sessionTemp();
    writeAll(dir + QLatin1String("/saved web pages/a/index.html"), "<html></html>");
    std::printf("session: %s\nfile: %s\n", qPrintable(QDir::toNativeSeparators(dir)),
                QFileInfo::exists(dir + QLatin1String("/saved web pages/a/index.html")) ? "yes" : "no");
    std::fflush(stdout);
    return 0;
}

static void savedPageCopy(const QString &W)
{
    const QString L = QStringLiteral("saved web page, portable: ");
    QString err;
    auto p = Project::create(W + QLatin1String("/pages"), QStringLiteral("Harbor"), &err);
    check(bool(p), L + QStringLiteral("(a project)"), err);
    if (!p)
        return;
    const QString tmpPage = W + QLatin1String("/fetched/page.html");
    writeAll(tmpPage, "<html><body><h1>Tide table</h1><p>High water at noon.</p><img src=\"page-files/buoy.png\"></body></html>");
    writeAll(W + QLatin1String("/fetched/page-files/buoy.png"), pngBytes(QColor(200, 90, 20)));
    importers::WebPage wp;
    wp.url = QStringLiteral("https://example.org/harbor/tides");
    wp.title = QStringLiteral("Tide table");
    wp.asWebPage = true;
    wp.htmlFile = tmpPage;
    BinderItem *web = importer::addWebPage(p.get(), wp, p->research(), -1, nullptr);
    check(web != nullptr, L + QStringLiteral("(a web page saved in it)"));
    if (!web)
        return;
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QLatin1String("/Quillbinder web pages");
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1String("/web pages");
    const bool downloadsBefore = QFileInfo::exists(downloads);
    const bool cacheBefore = QFileInfo::exists(cache);
    QList<QUrl> opened;
    links::setOpenerForTests([&](const QUrl &u) { opened << u; });
    {
        MediaView v;
        v.resize(820, 520);
        v.setProject(p.get());
        v.showItem(web);
        v.show();
        settle(200);
        auto *saved = v.findChild<QPushButton *>(QStringLiteral("webOpenSaved"));
        check(saved && saved->toolTip().contains(QLatin1String("removed when the program closes")) && !saved->toolTip().contains(QLatin1String("Downloads")),
              L + QStringLiteral("the button says that the copy goes when the program closes, not that it is put into Downloads"),
              saved ? saved->toolTip() : QString());
        if (saved) {
            QTest::mouseClick(saved, Qt::LeftButton);
            settle(200);
        }
        v.grab().save(outDir + QLatin1String("/v0904-saved-page.png"));
        const QString file = opened.isEmpty() ? QString() : opened.first().toLocalFile();
        const QString under = portable::sessionTemp() + QLatin1String("/saved web pages/");
        check(opened.size() == 1 && file.startsWith(under) && readAll(file).contains("High water at noon."),
              L + QStringLiteral("Open Saved Page in Browser opens a copy in the folder for this run"), file);
        const QString pic = QFileInfo(file).dir().filePath(QStringLiteral("page-files/buoy.png"));
        check(QFileInfo(pic).size() > 0, L + QStringLiteral("the copy has its picture"), pic);
        auto *noteLabel = v.findChild<QLabel *>(QStringLiteral("webNote"));
        check(noteLabel && noteLabel->isVisible() && noteLabel->text().contains(QDir::toNativeSeparators(QFileInfo(file).fileName())),
              L + QStringLiteral("the view says where the copy is"), noteLabel ? noteLabel->text() : QString());
    }
    links::setOpenerForTests(nullptr);
    check(QFileInfo::exists(downloads) == downloadsBefore, L + QStringLiteral("nothing new in Downloads"), downloads);
    check(QFileInfo::exists(cache) == cacheBefore, L + QStringLiteral("nothing new in the system's cache folder"), cache);
}

// ------------------------------------------------------------------------------------------------------------------
// W-06: pages in older character sets

static QByteArray archiveWith(const QByteArray &charset, const QByteArray &body)
{
    QByteArray a;
    a += "From: <Saved by a browser>\r\nSubject: Old page\r\nMIME-Version: 1.0\r\n";
    a += "Content-Type: multipart/related; type=\"text/html\"; boundary=\"----=_Part_7\"\r\n\r\n";
    a += "------=_Part_7\r\nContent-Type: text/html; charset=\"" + charset + "\"\r\n";
    a += "Content-Transfer-Encoding: 8bit\r\nContent-Location: https://example.org/old/page.html\r\n\r\n";
    a += body + "\r\n------=_Part_7--\r\n";
    return a;
}

static void characterSets(const QString &W)
{
    const QString L = QStringLiteral("character sets of saved pages: ");
    struct Case {
        const char *charset;
        QByteArray bytes;
        QString expected;
        const char *what;
    };
    const Case cases[] = {
        {"windows-1252", QByteArray("\x93" "Tide\x94 \x97 5\xA0\x80, caf\xE9"), QStringLiteral("\u201CTide\u201D \u2014 5\u00A0\u20AC, caf\u00E9"),
         "windows-1252: curved quotes, the dash, the euro sign"},
        {"iso-8859-15", QByteArray("5 \xA4, c\xBD" "ur"), QStringLiteral("5 \u20AC, c\u0153ur"), "ISO 8859-15: the euro sign and the oe ligature"},
        {"iso-8859-1", QByteArray("na\xEFve caf\xE9"), QStringLiteral("na\u00EFve caf\u00E9"), "ISO 8859-1"},
        {"utf-8", QByteArray("\xE2\x80\x9CTide\xE2\x80\x9D"), QStringLiteral("\u201CTide\u201D"), "UTF-8"},
    };
    int n = 0;
    for (const Case &c : cases) {
        const QByteArray mht = archiveWith(c.charset, "<html><head><meta charset=\"" + QByteArray(c.charset) + "\"></head><body><p>" + c.bytes + "</p></body></html>");
        QString page, err;
        const QString out = W + QStringLiteral("/charset-%1").arg(++n);
        const bool ok = webarchive::unpack(mht, out, &page, nullptr, &err);
        const QString html = QString::fromUtf8(readAll(page));
        check(ok && html.contains(c.expected), L + QString::fromLatin1(c.what), err + QLatin1String(" / ") + html.left(300));
    }
    // a name nobody knows: the page is still unpacked (its plain letters readable), nothing breaks
    const QByteArray mht = archiveWith("x-nobody-knows-9", "<html><body><p>Plain harbor words</p></body></html>");
    QString page, err;
    check(webarchive::unpack(mht, W + QLatin1String("/charset-unknown"), &page, nullptr, &err) && readAll(page).contains("Plain harbor words"),
          L + QStringLiteral("an unknown character set: the page is still unpacked"), err);
}

// ------------------------------------------------------------------------------------------------------------------
// W-12: the download page and the release text with the setup programs

static bool runPython(const QStringList &args, QString *output, const QString &inDir = QString())
{
    QProcess pr;
    pr.setProcessChannelMode(QProcess::MergedChannels);
    if (!inDir.isEmpty())
        pr.setWorkingDirectory(inDir);
    pr.start(QStringLiteral("python3"), args); // (also on the Windows runner, as v0650 and v0700 start it)
    const bool ok = pr.waitForFinished(60000) && pr.exitStatus() == QProcess::NormalExit && pr.exitCode() == 0;
    *output = QString::fromUtf8(pr.readAll());
    return ok;
}

static void downloadTexts(const QString &W)
{
    const QString L = QStringLiteral("downloads: ");
    const QString src = QStringLiteral(QB_TEST_SOURCE_DIR "/..");
    const QString dist = W + QLatin1String("/dist");
    const QStringList files = {QStringLiteral("Quillbinder-0.90.4-beta-x86_64.AppImage"),        QStringLiteral("Quillbinder-0.90.4-full-beta-x86_64.AppImage"),
                               QStringLiteral("quillbinder-0.90.4-update.qbpkg"),                 QStringLiteral("Quillbinder-0.90.4-windows-x64.zip"),
                               QStringLiteral("Quillbinder-0.90.4-full-windows-x64.zip"),         QStringLiteral("Quillbinder-0.90.4-windows-x64-setup.exe"),
                               QStringLiteral("Quillbinder-0.90.4-full-windows-x64-setup.exe"),   QStringLiteral("quillbinder-0.90.4-windows-update.qbpkg"),
                               QStringLiteral("Quillbinder-0.90.4-windows-x64-symbols.zip")};
    for (const QString &f : files)
        writeAll(dist + QLatin1Char('/') + f, "x");
    // the download page: tools/download-page.py <folder with the release's files> <tag> <folder for the page>
    auto cardOf = [&](const QString &names, const QString &site, QString *out) {
        runPython({QStringLiteral("-I"), src + QLatin1String("/tools/download-page.py"), names, QStringLiteral("v0.90.4-beta"), site}, out, src);
        const QString page = QString::fromUtf8(readAll(site + QLatin1String("/index.html")));
        const int at = page.indexOf(QLatin1String("data-system=\"windows\""));
        return at < 0 ? QString() : page.mid(at, page.indexOf(QLatin1String("</section>"), at) - at);
    };
    QString out;
    const QString card = cardOf(dist, W + QLatin1String("/site"), &out);
    check(!card.isEmpty(), L + QStringLiteral("(tools/download-page.py writes the page)"), out.left(800));
    if (!card.isEmpty()) {
        const int button = card.indexOf(QLatin1String("class=\"button\""));
        const QString buttonHtml = button < 0 ? QString() : card.mid(button, card.indexOf(QLatin1String("</a>"), button) - button);
        check(buttonHtml.contains(QLatin1String("/Quillbinder-0.90.4-full-windows-x64-setup.exe")),
              L + QStringLiteral("the Windows button downloads the full version's setup program"), buttonHtml);
        check(card.contains(QLatin1String("needs no admin rights")) && card.contains(QLatin1String("Choose a folder of your own"))
                  && !card.contains(QLatin1String("Unpack the zip file completely")),
              L + QStringLiteral("its steps describe the setup program: a folder of one's own, no admin rights"), card.left(1500));
        check(card.contains(QLatin1String("/Quillbinder-0.90.4-full-windows-x64.zip")) && card.contains(QLatin1String("/Quillbinder-0.90.4-windows-x64-setup.exe"))
                  && card.contains(QLatin1String("/Quillbinder-0.90.4-windows-x64.zip")) && card.contains(QLatin1String("/quillbinder-0.90.4-windows-update.qbpkg"))
                  && !card.contains(QLatin1String("symbols")),
              L + QStringLiteral("the other Windows downloads: the full zip, the program alone as setup and as zip, the update"),
              card.mid(card.indexOf(QLatin1String("Other downloads"))).left(1500));
    }
    // without setup programs the page is as before: the zip on the button, its steps about unpacking
    const QString dist2 = W + QLatin1String("/dist-zip-only");
    for (const QString &f : files)
        if (!f.endsWith(QLatin1String("-setup.exe")))
            writeAll(dist2 + QLatin1Char('/') + f, "x");
    const QString card2 = cardOf(dist2, W + QLatin1String("/site-zip-only"), &out);
    check(card2.contains(QLatin1String("Unpack the zip file completely")) && card2.contains(QLatin1String("/Quillbinder-0.90.4-full-windows-x64.zip"))
              && !card2.contains(QLatin1String("The full version as a zip file")) && !card2.contains(QLatin1String("setup")),
          L + QStringLiteral("a release without setup programs: the zip on the button, as before"), out.left(300) + card2.left(1200));
    // the release text (tools/release-notes.py <folder> --test): how to use the setup programs, and their rows
    QString notes;
    const bool wrote = runPython({QStringLiteral("-I"), src + QLatin1String("/tools/release-notes.py"), dist, QStringLiteral("--test")}, &notes, src);
    check(wrote && notes.contains(QLatin1String("Quillbinder-0.90.4-full-windows-x64-setup.exe")) && notes.contains(QLatin1String("needs no admin rights"))
              && notes.contains(QLatin1String("| Quillbinder-0.90.4-windows-x64-setup.exe |")) && notes.contains(QLatin1String("The zip files hold the same folders")),
          L + QStringLiteral("the release text explains the setup programs and lists them"), notes.left(2500));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
#ifndef Q_OS_WIN
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + QLatin1String("/cache")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_TEST_PORTABLE", "1");
    qputenv("QB_TEST_PORTABLE_FOLDER", (home.path() + QLatin1String("/Quillbinder")).toUtf8());
#else
    // a child run without the switch (Qt as it comes), started by altGrKeys with QT_QPA_PLATFORM=windows
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--child-altgr-plain") == 0) {
            QApplication plainApp(argc, argv);
            return childAltGrPlain();
        }
    }
    // as the program does at its start (main.cpp): Qt's Windows platform with the switch that tells AltGr apart
    {
        const QByteArray withAltGr = platform::qpaWithAltGr(qgetenv("QT_QPA_PLATFORM"));
        if (!withAltGr.isEmpty())
            qputenv("QT_QPA_PLATFORM", withAltGr);
    }
#endif
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--child-session") == 0) {
            QCoreApplication core(argc, argv);
            return childSession();
        }
    }
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.90.4")) >= 0, QStringLiteral("version 0.90.4 or newer"),
          QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    AppSettings::instance().load();
    QTemporaryDir work;
    const QString W = QDir::cleanPath(work.path());

    altGrValue();
    sessionFolder();
    savedPageCopy(W);
    characterSets(W);
    downloadTexts(W);
#ifdef Q_OS_WIN
    fontDesigns();
    altGrKeys(); // last: it presses real keys
#endif

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
