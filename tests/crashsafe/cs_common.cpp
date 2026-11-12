// The system test, stage 1: the shared parts (results, G5 to G7, the writer's text and typing,
// the outside sender, the program in the test process, fixtures, the model and the verify step).
#include "cs.h"

#include "compile/PluginManager.h"
#include "core/CrashGuard.h"
#include "core/ProjectTemplates.h"
#include "core/SettingsLayer.h"
#include "core/Snapshots.h"
#include "import/ScrivenerFormat.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QCryptographicHash>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QProgressDialog>
#include <QHash>
#include <QSet>
#include <QDirIterator>
#include <memory>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QImage>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QXmlStreamReader>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

namespace cs {

Ctx &ctx()
{
    static Ctx c;
    return c;
}

int sized(int n, int smokeMax)
{
    return ctx().smoke ? std::max(1, std::min(n, smokeMax)) : n;
}

// ================================================================ results
namespace {
// Component cases call the libraries in long loops of their own, without the event loop a window has.
// Between two of their steps the events are let through (at most every 200 ms), so the watchdog of G2
// reports a single operation that holds the thread for 10 s, not the harness's loop (shakedown
// 2026-11-09: C-02's 20 saves in a row gave a freeze report).
void heartbeat()
{
    // 0.64.2: also S-E05, whose cycles call the libraries the same way (four cycles of 2.5 s gave a freeze report)
    if (!ctx().id.startsWith(QLatin1String("C-")) && ctx().id != QLatin1String("S-E05"))
        return;
    static QElapsedTimer last;
    if (last.isValid() && last.elapsed() < 200)
        return;
    last.start();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}
} // namespace

void check(bool ok, const QString &what, const QString &detail)
{
    heartbeat();
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (ok)
        ++ctx().passes;
    else {
        ++ctx().failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
        std::fflush(stdout);   // a case killed at its time limit keeps its FAIL lines
    }
    std::fflush(stdout);
}

void note(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
    std::fflush(stdout);
}

void fact(const QString &key, const QJsonValue &v)
{
    ctx().facts.insert(key, v);
    note(QStringLiteral("fact %1: %2").arg(key, v.isString() ? v.toString()
                                                              : QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact))));
}

void step(const QString &s)
{
    heartbeat();
    crash::note(s);
    std::printf("  -- %s\n", qPrintable(s));
    std::fflush(stdout);
}

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

bool waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > ms)
            return false;
        settle(30);
    }
    return true;
}

void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(ctx().dir + QStringLiteral("/") + name + QStringLiteral(".png"));
}

QString readText(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString projectFileIn(const QString &dir)
{
    QDirIterator it(dir, {QStringLiteral("*.qbx")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (QFileInfo(f).absoluteDir().dirName() != QLatin1String("settings"))
            return f;
    }
    return QString();
}

bool writeText(const QString &file, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QString firstDifference(const QString &a, const QString &b, int context)
{
    const int n = int(qMin(a.size(), b.size()));
    int i = 0;
    while (i < n && a.at(i) == b.at(i))
        ++i;
    if (i == n && a.size() == b.size())
        return QString();
    const int from = qMax(0, i - context);
    return QStringLiteral("first difference at %1 (lengths %2 and %3): \"%4\" | \"%5\"")
        .arg(i)
        .arg(a.size())
        .arg(b.size())
        .arg(a.mid(from, 2 * context), b.mid(from, 2 * context));
}

bool copyTree(const QString &from, const QString &to)
{
    QFileInfo fi(from);
    if (fi.isFile()) {
        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile::remove(to);
        return QFile::copy(from, to);
    }
    if (!fi.isDir())
        return false;
    QDir().mkpath(to);
    bool ok = true;
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString dst = to + src.mid(from.size());
        if (it.fileInfo().isDir())
            QDir().mkpath(dst);
        else {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            ok = QFile::copy(src, dst) && ok;
        }
    }
    return ok;
}

QString runTool(const QString &program, const QStringList &args, int *rc, int ms)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(10000)) {
        if (rc)
            *rc = -1;
        return QString();
    }
    p.waitForFinished(ms);
    if (rc)
        *rc = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
    return QString::fromUtf8(p.readAll());
}

// ================================================================ G6: Qt's messages
namespace {
QtMessageHandler g_previousHandler = nullptr;
int g_fatal = 0, g_critical = 0, g_warnings = 0, g_failingWarnings = 0;
QStringList g_failingLines;
const char *const kFailingWarnings[] = {"ASSERT", "Recursive repaint", "QObject::connect: No such",
                                        "Cannot send events to objects owned by a different thread",
                                        "QBasicTimer can only be used with threads started with QThread"};

void counter(QtMsgType type, const QMessageLogContext &c, const QString &msg)
{
    if (type == QtFatalMsg)
        ++g_fatal;
    else if (type == QtCriticalMsg) {
        ++g_critical;
        g_failingLines << QStringLiteral("critical: ") + msg.left(300);
    } else if (type == QtWarningMsg) {
        ++g_warnings;
        for (const char *w : kFailingWarnings)
            if (msg.contains(QLatin1String(w))) {
                ++g_failingWarnings;
                g_failingLines << QStringLiteral("warning: ") + msg.left(300);
                break;
            }
    }
    if (g_previousHandler)
        g_previousHandler(type, c, msg);
    else
        std::fprintf(stderr, "%s\n", qPrintable(msg));
}
} // namespace

void installMessageCounter()
{
    g_previousHandler = qInstallMessageHandler(counter);
}

void finishMessages()
{
    note(QStringLiteral("Qt messages: %1 warning(s), %2 critical, %3 of the failing warnings").arg(g_warnings).arg(g_critical).arg(g_failingWarnings));
    fact(QStringLiteral("qtWarnings"), g_warnings);
    check(g_critical == 0 && g_failingWarnings == 0 && g_fatal == 0, QStringLiteral("G6: no qCritical, qFatal or failing warning"),
          g_failingLines.mid(0, 10).join(QLatin1Char('\n')));
}

// ================================================================ G5
void checkCleanWindows(QWidget *main)
{
    QStringList open;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl->isVisible() && tl != main && (qobject_cast<QDialog *>(tl) || qobject_cast<QMessageBox *>(tl)))
            open << (tl->windowTitle().isEmpty() ? QString::fromLatin1(tl->metaObject()->className()) : tl->windowTitle());
    bool inside = true;
    if (main) {
        const QRect avail = main->screen() ? main->screen()->availableGeometry() : QRect(0, 0, 1920, 1080);
        inside = avail.intersects(main->frameGeometry());
    }
    check(open.isEmpty() && inside, QStringLiteral("G5: no dialog left open, the window on the screen"),
          open.join(QLatin1String(", ")));
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl->isVisible() && tl != main && (qobject_cast<QDialog *>(tl) || qobject_cast<QMessageBox *>(tl)))
            tl->close();
}

// ================================================================ the writer's text
namespace {
const char *const kWords[] = {
    "the", "harbor", "light", "she", "walked", "along", "quiet", "stones", "and", "rain", "fell", "on", "old", "roofs",
    "while", "ships", "waited", "for", "morning", "tide", "keeper", "counted", "lanterns", "in", "a", "small", "room",
    "above", "water", "wind", "carried", "voices", "from", "market", "where", "fishermen", "sold", "silver", "herring",
    "her", "brother", "never", "returned", "after", "storm", "that", "winter", "letters", "arrived", "late", "with",
    "salt", "stains", "across", "every", "page", "he", "wrote", "about", "lighthouse", "remembered", "promise", "kept",
    "door", "open", "told", "nobody", "knew", "why", "station", "train", "smelled", "coal", "bread", "grandmother",
    "kitchen", "window", "glass", "cold", "hands", "careful", "slowly", "quickly", "suddenly", "because", "though"};
const char *const kGerman[] = {"Straße", "Mädchen", "Übermut", "schön", "Grüße", "Fräulein", "Bahnhof", "Wörterbuch",
                               "Größe", "Gemütlichkeit", "Weißbier", "Zuckertüte", "Schadenfreude", "Fernweh", "Heimat",
                               "Käsekuchen", "Doppelgänger", "Brötchen", "Äpfel", "Ölmühle"};
const char *const kNames[] = {"Anna", "Ben", "Clara", "Eleanor", "Jakob", "Marta", "Tobias", "Ines"};

QString sentence(std::mt19937 &r, bool german, int &budget)
{
    const int n = 4 + int(r() % 14);
    QStringList w;
    for (int i = 0; i < n && budget > 0; ++i, --budget) {
        const unsigned x = r() % 100;
        if (x < 4)
            w << QString::fromUtf8(kGerman[r() % (sizeof(kGerman) / sizeof(*kGerman))]);
        else if (x < 9)
            w << QString::fromLatin1(kNames[r() % (sizeof(kNames) / sizeof(*kNames))]);
        else if (x < 11)
            w << QString::number(1 + r() % 1990);
        else {
            QString s = QString::fromLatin1(kWords[r() % (sizeof(kWords) / sizeof(*kWords))]);
            if (r() % 60 == 0 && s.size() > 3)
                std::swap(s[1], s[2]);   // a typo for the spell checker
            w << s;
        }
    }
    if (german && !w.isEmpty())
        w[0] = QString::fromUtf8(kGerman[r() % (sizeof(kGerman) / sizeof(*kGerman))]);
    QString s = w.join(QLatin1Char(' '));
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    switch (r() % 12) {
    case 0: return s + QStringLiteral("...");
    case 1: return s + QChar(0x2026);
    case 2: return s + QStringLiteral(" — and then nothing.");
    case 3: return s + QLatin1Char('?');
    default: return s + QLatin1Char('.');
    }
}
} // namespace

QStringList atParagraphs(quint32 seed, int words, bool german)
{
    std::mt19937 r(seed * 2654435761u + 7u);
    QStringList out;
    int budget = words;
    while (budget > 0) {
        if (r() % 40 == 0 && !out.isEmpty()) {
            out << QStringLiteral("#");
            continue;
        }
        const int sentences = (r() % 25 == 0) ? 40 : 1 + int(r() % 12);   // now and then a very long paragraph
        QStringList para;
        for (int i = 0; i < sentences && budget > 0; ++i) {
            QString s = sentence(r, german, budget);
            const unsigned q = r() % 10;
            if (q == 0)
                s = QStringLiteral("\"") + s + QStringLiteral("\" she said.");
            else if (q == 1)
                s = QStringLiteral("“") + s + QStringLiteral("” he asked.");
            else if (q == 2)
                s = QStringLiteral("It's ") + s;
            para << s;
        }
        out << para.join(QLatin1Char(' '));
    }
    return out;
}

QString gateText(quint32 seed, int chars)
{
    static const QString allowed = QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;:!?'\"()-\n")
                                   + QString::fromUtf8("äöüÄÖÜß");
    QString out;
    quint32 s = seed;
    while (out.size() < chars) {
        const QStringList ps = atParagraphs(s++, 400);
        for (const QString &p : ps) {
            for (QChar c : p) {
                if (c == QChar(0x2026))
                    out += QStringLiteral("...");
                else if (c == QChar(0x201C) || c == QChar(0x201D))
                    out += QLatin1Char('"');
                else if (c == QChar(0x2014))
                    out += QLatin1Char('-');
                else if (allowed.contains(c))
                    out += c;
            }
            out += QLatin1Char('\n');
            if (out.size() >= chars)
                break;
        }
    }
    return out.left(chars);
}

void keysInto(QWidget *w, const QString &text)
{
    for (QChar c : text) {
        if (c == QLatin1Char('\n'))
            QTest::keyClick(w, Qt::Key_Return);
        else {
            Qt::Key k = Qt::Key_unknown;
            if (c.unicode() < 128 && c.isLetterOrNumber())
                k = Qt::Key(c.toUpper().unicode());
            else if (c == QLatin1Char(' '))
                k = Qt::Key_Space;
            QTest::sendKeyEvent(QTest::Click, w, k, QString(c), c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
        }
    }
}

QString rawText(QTextDocument *d)
{
    return d ? d->toRawText() : QString();
}

static QString toRaw(const QString &typed)
{
    QString s = typed;
    s.replace(QLatin1Char('\n'), QChar(QChar::ParagraphSeparator));
    return s;
}

QString typeLikeWriter(TextEditor *e, const QString &text, quint32 seed)
{
    std::mt19937 r(seed);
    e->setFocus();
    QTextCursor c = e->textCursor();
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    QString model = rawText(e->document());
    int since = 0, next = 150 + int(r() % 151);
    int mismatches = 0;
    for (int pos = 0; pos < text.size();) {
        const QString chunk = text.mid(pos, 40);
        pos += int(chunk.size());
        QTest::keyClick(e, Qt::Key_End, Qt::ControlModifier);
        keysInto(e, chunk);
        model += toRaw(chunk);
        since += int(chunk.size());
        QCoreApplication::processEvents();
        if (since < next)
            continue;
        since = 0;
        next = 150 + int(r() % 151);
        switch (r() % 4) {
        case 0: {   // a typo noticed: Backspace, then the right text again
            const int k = qMin(int(model.size()), 3 + int(r() % 6));
            const QString gone = model.right(k);
            for (int i = 0; i < k; ++i)
                QTest::keyClick(e, Qt::Key_Backspace);
            model.chop(k);
            if (rawText(e->document()) != model)
                ++mismatches;
            QString back = gone;
            back.replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'));
            keysInto(e, back);
            model += gone;
            break;
        }
        case 1:   // word-wise back and forth
            QTest::keyClick(e, Qt::Key_Left, Qt::ControlModifier);
            QTest::keyClick(e, Qt::Key_Left, Qt::ControlModifier);
            QTest::keyClick(e, Qt::Key_Right, Qt::ControlModifier);
            QTest::keyClick(e, Qt::Key_End, Qt::ControlModifier);
            break;
        case 2: {   // a word selected and typed over
            QTest::keyClick(e, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
            const QString sel = e->textCursor().selectedText();
            if (!sel.isEmpty() && model.endsWith(sel)) {
                keysInto(e, QStringLiteral("word "));
                model.chop(sel.size());
                model += QStringLiteral("word ");
            } else {
                QTest::keyClick(e, Qt::Key_End, Qt::ControlModifier);
                if (!sel.isEmpty())
                    ++mismatches;
            }
            break;
        }
        default: {   // undo, then redo: the same text again
            const QString before = rawText(e->document());
            QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
            QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            settle(20);
            if (rawText(e->document()) != before) {
                ++mismatches;
                model = rawText(e->document());   // go on from what is there; the case fails below
            }
            QTest::keyClick(e, Qt::Key_End, Qt::ControlModifier);
            break;
        }
        }
    }
    settle(100);
    if (mismatches)
        note(QStringLiteral("typing: %1 step(s) where the editor did not do what the writer expected").arg(mismatches));
    check(mismatches == 0, QStringLiteral("typing like a writer: corrections, overtyping, undo and redo do what is expected"));
    return model;
}

// ================================================================ G7: the outside sender
namespace {
struct GateState {
    bool active = false;
    QObject *editor = nullptr;
    QObject *viewport = nullptr;
    std::vector<qint64> keys, paints;
};
GateState g_gate;
} // namespace

qint64 nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool CsApp::notify(QObject *o, QEvent *e)
{
    if (!g_gate.active)
        return QApplication::notify(o, e);
    const bool key = e->type() == QEvent::KeyPress && o == g_gate.editor && !static_cast<QKeyEvent *>(e)->text().isEmpty();
    const bool paint = e->type() == QEvent::Paint && o == g_gate.viewport;
    const bool r = QApplication::notify(o, e);
    if (key)
        g_gate.keys.push_back(nowNs());
    else if (paint)
        g_gate.paints.push_back(nowNs());
    return r;
}

static double percentile(std::vector<double> v, double p)
{
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    const double idx = p * double(v.size() - 1);
    const size_t lo = size_t(std::floor(idx)), hi = size_t(std::ceil(idx));
    return v[lo] + (v[hi] - v[lo]) * (idx - double(lo));
}

GateResult typingGate(TextEditor *e, const QString &profile, int seconds, const QString &label)
{
    GateResult res;
    res.label = label;
    res.profile = profile;
    if (ctx().smoke)
        seconds = std::min(seconds, 5);   // a shakedown: the gate runs, its numbers mean little
    const double perSecond = profile.startsWith(QLatin1String("wpm")) ? profile.mid(3).toDouble() * 5.0 / 60.0 : (profile == QLatin1String("fast") ? 12 : 4);
    const int need = int(seconds * perSecond * 1.3) + 400;
    const quint32 seed = ctx().seed + qHash(label + profile);
    const QString text = gateText(seed, need);
    QString safe = label;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QStringLiteral("-"));
    const QString base = ctx().dir + QStringLiteral("/gate-") + safe + QLatin1Char('-') + profile;
    writeText(base + QStringLiteral(".txt"), text.toUtf8());
    QWidget *win = e->window();
    win->raise();
    win->activateWindow();
    (void)QTest::qWaitForWindowActive(win, 3000);
    e->setFocus(Qt::OtherFocusReason);
    settle(300);
    QCursor::setPos(e->viewport()->mapToGlobal(e->viewport()->rect().center()));
    settle(100);
    const QString before = rawText(e->document());
    const int at = e->textCursor().position();
    g_gate = GateState();
    g_gate.editor = e;
    g_gate.viewport = e->viewport();
    g_gate.active = true;
    QProcess snd;
    snd.setProcessChannelMode(QProcess::MergedChannels);
    snd.start(QStringLiteral("python3"), {QStringLiteral("-I"), ctx().sourceDir + QStringLiteral("/tests/crashsafe/sender.py"),
                                          QStringLiteral("--text"), base + QStringLiteral(".txt"), QStringLiteral("--log"),
                                          base + QStringLiteral(".log"), QStringLiteral("--profile"), profile,
                                          QStringLiteral("--seconds"), QString::number(seconds), QStringLiteral("--seed"),
                                          QString::number(seed), QStringLiteral("--burst"), QStringLiteral("200")});
    QEventLoop loop;
    QObject::connect(&snd, &QProcess::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot((seconds + 120) * 1000, &loop, &QEventLoop::quit);
    if (snd.waitForStarted(10000))
        loop.exec();
    settle(1500);
    g_gate.active = false;
    if (snd.state() != QProcess::NotRunning) {
        snd.kill();
        snd.waitForFinished(3000);
    }
    const QString senderOut = QString::fromUtf8(snd.readAll()).trimmed();
    // what was sent
    struct Sent {
        bool burst;
        int index;
        qint64 ns;
    };
    std::vector<Sent> sent;
    int skipped = 0;
    bool ended = false;
    for (const QString &line : readText(base + QStringLiteral(".log")).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char(' '));
        if (f.size() == 3 && (f[0] == QLatin1String("k") || f[0] == QLatin1String("b")))
            sent.push_back({f[0] == QLatin1String("b"), f[1].toInt(), f[2].toLongLong()});
        else if (f.value(0) == QLatin1String("x"))
            ++skipped;
        else if (f.value(0) == QLatin1String("end"))
            ended = true;
    }
    QString typed;
    for (const Sent &s : sent)
        typed += text.at(s.index);
    const QString expected = before.left(at) + toRaw(typed) + before.mid(at);
    const QString actual = rawText(e->document());
    res.sent = int(sent.size());
    res.handled = int(g_gate.keys.size());
    res.textOk = ended && actual == expected;
    QString diff;
    if (!res.textOk) {
        int i = 0;
        while (i < actual.size() && i < expected.size() && actual[i] == expected[i])
            ++i;
        diff = QStringLiteral("first difference at %1: expected \"%2\", got \"%3\"%4")
                   .arg(i)
                   .arg(expected.mid(qMax(0, i - 20), 50), actual.mid(qMax(0, i - 20), 50),
                        ended ? QString() : QStringLiteral(" (the sender did not end: %1)").arg(senderOut.left(300)));
    }
    // key-to-screen: from the send to the end of the next paint after the key was handled
    std::vector<double> delays, profileDelays;
    std::vector<qint64> shown;
    const size_t n = std::min(sent.size(), g_gate.keys.size());
    size_t pi = 0;
    for (size_t i = 0; i < n; ++i) {
        while (pi < g_gate.paints.size() && g_gate.paints[pi] < g_gate.keys[i])
            ++pi;
        const qint64 v = pi < g_gate.paints.size() ? g_gate.paints[pi] : g_gate.keys[i];
        shown.push_back(v);
        const double d = double(v - sent[i].ns) / 1e6;
        delays.push_back(d);
        if (!sent[i].burst)
            profileDelays.push_back(d);
    }
    res.median = percentile(profileDelays, 0.5);
    res.p95 = percentile(profileDelays, 0.95);
    res.p99 = percentile(profileDelays, 0.99);
    res.largest = profileDelays.empty() ? 0 : *std::max_element(profileDelays.begin(), profileDelays.end());
    // backlog during the profile run: keys sent but not yet shown
    {
        std::vector<qint64> sh;
        for (size_t i = 0; i < n; ++i)
            if (!sent[i].burst)
                sh.push_back(shown[i]);
        std::sort(sh.begin(), sh.end());
        size_t shownCount = 0;
        int sentCount = 0;
        for (size_t i = 0; i < n; ++i) {
            if (sent[i].burst)
                break;
            ++sentCount;
            while (shownCount < sh.size() && sh[shownCount] <= sent[i].ns)
                ++shownCount;
            res.backlog = std::max(res.backlog, sentCount - int(shownCount));
        }
    }
    // throughput of the burst
    {
        qint64 first = -1, last = -1;
        int nb = 0;
        for (size_t i = 0; i < n; ++i)
            if (sent[i].burst) {
                if (first < 0)
                    first = sent[i].ns;
                last = shown[i];
                ++nb;
            }
        if (nb > 10 && last > first)
            res.throughput = double(nb) / (double(last - first) / 1e9);
    }
    const bool keysOk = res.textOk && res.sent == res.handled;
    const bool slow = keysOk && res.p95 <= 100 && res.p99 <= 250 && res.largest <= 500 && res.backlog <= 2 && res.throughput >= 10;
    const bool fast = slow && res.p95 <= 60 && res.p99 <= 120 && res.largest <= 250 && res.backlog <= 3 && res.throughput >= 20;
    res.result = fast ? QStringLiteral("Pass fast") : slow ? QStringLiteral("Pass slow") : QStringLiteral("Fail");
    auto share = [](double v, double fastLimit, double slowLimit) {
        return QStringLiteral("%1 % of the fast limit, %2 % of the slow limit").arg(100.0 * v / fastLimit, 0, 'f', 0).arg(100.0 * v / slowLimit, 0, 'f', 0);
    };
    res.line = QStringLiteral("G7 %1, %2 writer: %3. Keys sent %4, handled %5, text equal: %6%7; median %8 ms; p95 %9 ms (%10); "
                              "p99 %11 ms (%12); largest %13 ms (%14); backlog %15 keys (fast limit 3, slow limit 2); "
                              "throughput %16 keys/s = %17 WPM (%18)")
                   .arg(label, profile, res.result)
                   .arg(res.sent)
                   .arg(res.handled)
                   .arg(res.textOk ? QStringLiteral("yes") : QStringLiteral("no"),
                        skipped ? QStringLiteral(" (%1 characters the layout cannot type were left out)").arg(skipped) : QString())
                   .arg(res.median, 0, 'f', 1)
                   .arg(res.p95, 0, 'f', 1)
                   .arg(share(res.p95, 60, 100))
                   .arg(res.p99, 0, 'f', 1)
                   .arg(share(res.p99, 120, 250))
                   .arg(res.largest, 0, 'f', 1)
                   .arg(share(res.largest, 250, 500))
                   .arg(res.backlog)
                   .arg(res.throughput, 0, 'f', 1)
                   .arg(res.throughput * 60 / 5, 0, 'f', 0)
                   .arg(QStringLiteral("%1 % of the fast minimum, %2 % of the slow minimum")
                            .arg(100.0 * res.throughput / 20, 0, 'f', 0)
                            .arg(100.0 * res.throughput / 10, 0, 'f', 0));
    note(res.line);
    // per key, for later study
    {
        QByteArray csv = "index,burst,delay_ms\n";
        for (size_t i = 0; i < n; ++i)
            csv += QByteArray::number(sent[i].index) + ',' + (sent[i].burst ? "1" : "0") + ',' + QByteArray::number(delays[i], 'f', 2) + '\n';
        writeText(base + QStringLiteral(".csv"), csv);
    }
    QJsonObject o{{QStringLiteral("result"), res.result}, {QStringLiteral("sent"), res.sent}, {QStringLiteral("handled"), res.handled},
                  {QStringLiteral("textEqual"), res.textOk}, {QStringLiteral("medianMs"), res.median}, {QStringLiteral("p95Ms"), res.p95},
                  {QStringLiteral("p99Ms"), res.p99}, {QStringLiteral("largestMs"), res.largest}, {QStringLiteral("backlog"), res.backlog},
                  {QStringLiteral("throughputKeysPerS"), res.throughput}};
    ctx().facts.insert(QStringLiteral("G7 ") + label + QLatin1Char(' ') + profile, o);
    check(res.result != QLatin1String("Fail"), QStringLiteral("G7 typing gate, %1, %2 writer: %3").arg(label, profile, res.result),
          diff.isEmpty() ? res.line : diff + QLatin1Char('\n') + res.line);
    return res;
}

// ================================================================ the program in this process
void setUpProgram()
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    // the model of what was typed: no corrections while typing (they have cases of their own)
    as.smartQuotes = false;
    as.smartDashes = false;
    as.ellipsis = false;
    as.capitalizeSentences = false;
    as.capitalizeI = false;
    as.save();
    theme::apply();
    if (qEnvironmentVariableIsEmpty("QB_PLUGIN_PATH"))
        qputenv("QB_PLUGIN_PATH", (ctx().buildDir + QStringLiteral("/plugins")).toUtf8());
    compile::PluginManager::instance().load();
    crash::install();
    crash::startWatchdog(10);
    installMessageCounter();
    // The first opening of a .scriv on a computer asks how to work on it ("Work in Scrivener
    // Format", "Convert...", Cancel; not left out by QB_NO_DIALOGS). Each case has a fresh home
    // without a working copy, so the question comes every time: the cases work in Scrivener format.
    auto *answer = new QTimer(qApp);
    answer->setInterval(100);
    QObject::connect(answer, &QTimer::timeout, [] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible() && b->objectName() == QLatin1String("openScrivenerQuestion"))
                if (auto *work = b->findChild<QPushButton *>(QStringLiteral("workInScrivener"))) {
                    note(QStringLiteral("(the first opening of a .scriv here: Work in Scrivener Format)"));
                    work->click();
                }
        // the notes after File > Project Format and Save a Copy (OK only; their text is recorded)
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible()
                && (b->objectName() == QLatin1String("formatSwitched") || b->objectName() == QLatin1String("copySaved")))
                if (QAbstractButton *ok = b->button(QMessageBox::Ok)) {
                    note(QStringLiteral("(%1: OK) %2").arg(b->objectName(), b->text().left(160).replace(QLatin1Char('\n'), QLatin1Char(' '))));
                    ok->click();
                }
        // the note after a compile ("The manuscript was saved as ..."): Close at once, its text recorded
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible() && b->windowTitle() == QLatin1String("Compile")
                && b->text().startsWith(QLatin1String("The manuscript was saved as")))
                if (QAbstractButton *ok = b->button(QMessageBox::Close)) {   // Open, Show Folder, Close
                    note(QStringLiteral("(Compile: Close) %1").arg(b->text().left(200).replace(QLatin1Char('\n'), QLatin1Char(' '))));
                    ok->click();
                }
        // A window that nobody answers would hold the case until its time limit (shakedown 2026-11-09: 25
        // cases). A message box or file dialog is closed after 8 s, any other modal window (never a progress
        // window) after 30 s, with its Escape answer; its title and text go into the log as UNANSWERED, so
        // the run goes on and the gap in the case is visible.
        static QHash<QWidget *, qint64> since;
        static QElapsedTimer clock;
        if (!clock.isValid())
            clock.start();
        QSet<QWidget *> now;
        for (QWidget *tl : QApplication::topLevelWidgets()) {
            auto *dlg = qobject_cast<QDialog *>(tl);
            if (!tl->isVisible() || !dlg || qobject_cast<QProgressDialog *>(tl))
                continue;
            const bool quickOne = qobject_cast<QMessageBox *>(tl) || qobject_cast<QFileDialog *>(tl);
            if (!quickOne && !dlg->isModal() && tl->windowModality() == Qt::NonModal)
                continue;
            now.insert(tl);
            if (!since.contains(tl))
                since.insert(tl, clock.elapsed());
            if (clock.elapsed() - since.value(tl) < (quickOne ? 8000 : 30000))
                continue;
            auto *b = qobject_cast<QMessageBox *>(tl);
            const QString text = b ? b->text() : qobject_cast<QFileDialog *>(tl) ? QStringLiteral("(a file dialog)")
                                                                             : QStringLiteral("(a %1)").arg(QString::fromLatin1(tl->metaObject()->className()));
            std::printf("UNANSWERED  %s: %s\n", qPrintable(tl->windowTitle()), qPrintable(text.left(240).replace(QLatin1Char('\n'), QLatin1Char(' '))));
            std::fflush(stdout);
            fact(QStringLiteral("an unanswered window, closed by the harness"), tl->windowTitle() + QStringLiteral(": ") + text.left(240));
            since.remove(tl);
            if (b && b->escapeButton())
                b->escapeButton()->click();
            else if (auto *d = qobject_cast<QDialog *>(tl))
                d->reject();
        }
        for (auto it = since.begin(); it != since.end();)
            it = now.contains(it.key()) ? std::next(it) : since.erase(it);
    });
    answer->start();
}

EditorPane *visiblePane(MainWindow *w)
{
    if (!w)
        return nullptr;
    // the pane with the keyboard focus, else the first one shown
    for (EditorPane *ep : w->findChildren<EditorPane *>())
        if (ep->isVisible() && ep->isAncestorOf(QApplication::focusWidget()))
            return ep;
    for (EditorPane *ep : w->findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

QList<EditorPane *> visiblePanes(MainWindow *w)
{
    QList<EditorPane *> out;
    for (EditorPane *ep : w->findChildren<EditorPane *>())
        if (ep->isVisible() && ep->width() > 50)
            out << ep;
    return out;
}

static void collectPath(QMenu *m, QAction *target, QStringList &trail, QStringList &found)
{
    if (!found.isEmpty() || !m)
        return;
    emit m->aboutToShow();
    for (QAction *a : m->actions()) {
        // the menu bar shows some commands through an alias (MainWindow::menuAlias)
        if (a == target || a->property("qbAliasOf").value<QObject *>() == target) {
            found = trail;
            found << a->text();
            return;
        }
        if (a->menu()) {
            trail << a->text();
            collectPath(a->menu(), target, trail, found);
            trail.removeLast();
            if (!found.isEmpty())
                return;
        }
    }
}

QStringList menuPathOf(MainWindow *w, const QString &actionName)
{
    QAction *target = action(w, actionName);
    QStringList found;
    if (!target)
        return found;
    for (QAction *top : w->menuBar()->actions()) {
        QStringList trail{top->text()};
        collectPath(top->menu(), target, trail, found);
        if (!found.isEmpty())
            break;
    }
    return found;
}

bool chooseByMenu(MainWindow *w, const QString &actionName)
{
    const QStringList path = menuPathOf(w, actionName);
    if (path.isEmpty()) {
        note(QStringLiteral("no menu bar entry for %1").arg(actionName));
        return false;
    }
    return chooseMenu(w, path);
}

MainWindow *openWindow(const QString &path, int wd, int ht)
{
    auto *w = new MainWindow;
    w->resize(wd, ht);
    w->show();
    w->activateWindow();
    (void)QTest::qWaitForWindowActive(w, 3000);
    step(QStringLiteral("open %1").arg(QFileInfo(path).fileName()));
    const bool scriv = path.endsWith(QLatin1String(".scriv")) || path.endsWith(QLatin1String(".scrivx"));
    const bool ok = scriv ? w->openScrivenerProject(path) : w->openProject(path);
    settle(800);
    check(ok && w->project(), QStringLiteral("(the project opens: %1)").arg(QFileInfo(path).fileName()));
    return w;
}

TextEditor *showDoc(MainWindow *w, BinderItem *it)
{
    EditorPane *pane = visiblePane(w);
    if (!pane || !it)
        return nullptr;
    pane->showItems({it}, false, int(ViewMode::Single));
    settle(300);
    TextEditor *e = pane->currentEditor();
    if (e) {
        // 0.64.2: the window first: without a window manager, focus (and with it Edit > Cut / Paste / Undo and the
        // shortcuts) reaches the editor only in the active window, which a dialog closed before may have left
        w->activateWindow();
        e->setFocus();
    }
    return e;
}

QAction *action(MainWindow *w, const QString &name)
{
    return w ? w->findChild<QAction *>(name) : nullptr;
}

static QString plainEntry(QString t)
{
    t.remove(QLatin1Char('&'));
    t.replace(QChar(0x2026), QStringLiteral("..."));
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    return t.trimmed();
}

static QAction *entryIn(QWidget *menu, const QString &text)
{
    for (QAction *a : menu->actions())
        if (!a->isSeparator() && plainEntry(a->text()) == plainEntry(text))
            return a;
    return nullptr;
}

static void closePopups()
{
    for (int i = 0; i < 6; ++i)
        if (QWidget *p = QApplication::activePopupWidget())
            p->close();
    settle(50);
}

// Opens the submenu of the entry `a` of the open menu `m` (0.90.0): Qt 6.12 opens a submenu when the mouse rests on
// its entry and closes it again on a click (Qt 6.4 opened it on the click), so the click comes only when resting did
// not open it, and the keyboard (the right arrow) is the last way
static bool openSubmenu(QMenu *m, QAction *a)
{
    QMenu *sub = a->menu();
    if (!sub)
        return false;
    const QPoint c = m->actionGeometry(a).center();
    m->setActiveAction(a);
    QTest::mouseMove(m, c);
    if (waitFor([sub] { return sub->isVisible(); }, 800))
        return true;
    QTest::mouseClick(m, Qt::LeftButton, Qt::NoModifier, c);
    if (waitFor([sub] { return sub->isVisible(); }, 800))
        return true;
    m->setActiveAction(a);
    QTest::keyClick(m, Qt::Key_Right);
    return waitFor([sub] { return sub->isVisible(); }, 1000);
}

bool chooseMenu(MainWindow *w, const QStringList &path)
{
    if (!w || path.size() < 2)
        return false;
    QMenuBar *mb = w->menuBar();
    QAction *top = entryIn(mb, path.first());
    if (!top || !top->menu()) {
        note(QStringLiteral("menu not found: %1").arg(path.first()));
        return false;
    }
    QTest::mouseClick(mb, Qt::LeftButton, Qt::NoModifier, mb->actionGeometry(top).center());
    QMenu *m = top->menu();
    if (!waitFor([m] { return m->isVisible(); }, 1500)) {
        note(QStringLiteral("menu did not open: %1").arg(path.first()));
        closePopups();
        return false;
    }
    for (int i = 1; i < path.size(); ++i) {
        settle(40);
        QAction *a = entryIn(m, path.at(i));
        if (!a || !a->isEnabled()) {
            note(QStringLiteral("menu entry %1: %2").arg(path.mid(0, i + 1).join(QLatin1String(" > ")),
                                                        a ? QStringLiteral("disabled") : QStringLiteral("not found")));
            closePopups();
            return false;
        }
        const QPoint c = m->actionGeometry(a).center();
        if (QMenu *sub = a->menu()) {
            if (!openSubmenu(m, a)) {
                note(QStringLiteral("submenu did not open: %1").arg(path.at(i)));
                closePopups();
                return false;
            }
            m = sub;
        } else {
            QTest::mouseMove(m, c);
            QTest::mouseClick(m, Qt::LeftButton, Qt::NoModifier, c);
            settle(100);
            return true;
        }
    }
    closePopups();
    return false;
}

bool rightClickChoose(TextEditor *e, int position, const QString &entry, QStringList *entries)
{
    if (!e)
        return false;
    QTextCursor c = e->textCursor();
    if (position >= 0 && !(c.hasSelection() && position >= c.selectionStart() && position <= c.selectionEnd())) {
        c.setPosition(position);
        e->setTextCursor(c);
    }
    e->ensureCursorVisible();
    settle(50);
    const QRect cr = e->cursorRect(e->textCursor());
    const QPoint pos(cr.left() + 1, cr.center().y());
    // a real right click: the press gives that editor (and its pane) the keyboard first; the menu's
    // commands act on the active editor (first run: the second pane's menu split the first pane's text)
    // without a window manager the window must be active, or setFocus does not move the
    // application's focus (and MainWindow's focusChanged handler never makes the pane active)
    e->window()->activateWindow();
    (void)QTest::qWaitForWindowActive(e->window(), 1000);
    e->setFocus(Qt::MouseFocusReason);
    settle(20);
    if (QApplication::focusWidget() != e)
        note(QStringLiteral("(right click: the editor did not get the keyboard focus)"));
    QTest::mousePress(e->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    QTest::mouseRelease(e->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    settle(30);
    QContextMenuEvent ce(QContextMenuEvent::Mouse, pos, e->viewport()->mapToGlobal(pos), Qt::NoModifier);
    QApplication::sendEvent(e->viewport(), &ce);
    QMenu *m = nullptr;
    waitFor([&m] {
        m = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        return m && m->isVisible();
    }, 1500);
    if (!m)
        return false;
    settle(80);
    if (entries) {
        entries->clear();
        for (QAction *a : m->actions())
            *entries << (a->isSeparator() ? QStringLiteral("|") : plainEntry(a->text()));
    }
    if (entry.isEmpty()) {
        closePopups();
        return true;
    }
    const QStringList path = entry.split(QStringLiteral(" > "));
    for (int i = 0; i < path.size(); ++i) {
        QAction *a = entryIn(m, path.at(i));
        if (!a || !a->isEnabled()) {
            closePopups();
            return false;
        }
        // 0.90.0: whether it has a submenu is asked before the click: a click on a plain entry ends the right-click
        // menu, which deletes it and its entries (the shakedown of 0.90.0 crashed on the entry asked afterwards)
        if (QMenu *sub = a->menu()) {
            if (!openSubmenu(m, a)) {
                closePopups();
                return false;
            }
            m = sub;
        } else {
            const QPoint p = m->actionGeometry(a).center();
            QTest::mouseMove(m, p);
            QTest::mouseClick(m, Qt::LeftButton, Qt::NoModifier, p);
            settle(150);
            return true;
        }
    }
    closePopups();
    return false;
}

BinderItem *itemByTitle(Project *p, const QString &title)
{
    for (BinderItem *it : p->allItems())
        if (it->title == title)
            return it;
    return nullptr;
}

QList<BinderItem *> textItems(Project *p, BinderItem *under)
{
    QList<BinderItem *> out;
    std::function<void(BinderItem *)> walk = [&](BinderItem *it) {
        for (BinderItem *ch : it->children) {
            if (ch->type == ItemType::Text)
                out << ch;
            walk(ch);
        }
    };
    walk(under ? under : p->draft());
    return out;
}

// ================================================================ fixtures
QImage testPicture(int w, int h, QRgb color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QPainter pa(&img);
    pa.setPen(Qt::white);
    pa.drawLine(0, 0, w - 1, h - 1);
    pa.drawEllipse(QRect(w / 4, h / 4, w / 2, h / 2));
    return img;
}

int insertPicture(Project *p, BinderItem *it, QTextDocument *d, int position, const QImage &img)
{
    const QString rel = p->docs()->importImage(it->id, img);
    d->addResource(QTextDocument::ImageResource, QUrl(rel), img);
    QTextCursor c(d);
    c.setPosition(position);
    QTextImageFormat f;
    f.setName(rel);
    f.setWidth(img.width());
    f.setHeight(img.height());
    c.insertImage(f);
    return position;
}

QString tableShape(QTextDocument *d)
{
    QStringList out;
    for (QTextBlock b = d->begin(); b.isValid();) {
        QTextCursor c(b);
        if (QTextTable *t = c.currentTable()) {
            const tables::Spec s = tables::specOf(t);
            QStringList cells;
            for (int r = 0; r < t->rows(); ++r)
                for (int col = 0; col < t->columns(); ++col) {
                    const QTextTableCell cell = t->cellAt(r, col);
                    if (cell.row() != r || cell.column() != col)
                        continue;
                    QStringList texts;
                    for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it)
                        if (it.currentBlock().isValid())
                            texts << it.currentBlock().text();
                    QString one = texts.join(QLatin1Char('/'));
                    if (cell.rowSpan() > 1 || cell.columnSpan() > 1)
                        one += QStringLiteral("[%1x%2]").arg(cell.rowSpan()).arg(cell.columnSpan());
                    const QColor bg = cell.format().property(props::CellBackground).value<QColor>();
                    if (bg.isValid())
                        one += QLatin1Char('{') + bg.name() + QLatin1Char('}');
                    cells << one;
                }
            out << QStringLiteral("TABLE %1x%2 %3: %4")
                       .arg(t->rows())
                       .arg(t->columns())
                       .arg(QString::fromUtf8(QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact)), cells.join(QLatin1String(" | ")));
            b = d->findBlock(t->lastPosition() + 1);
            continue;
        }
        if (QTextFrame *fr = figures::at(d, b.position())) {
            const int ip = figures::imagePosition(fr);
            QString desc;
            if (ip >= 0) {
                QTextCursor ic(d);
                ic.setPosition(ip);
                ic.setPosition(ip + 1, QTextCursor::KeepAnchor);
                desc = figures::description(ic.charFormat());
            }
            out << QStringLiteral("FIGURE pos%1 caption='%2' desc='%3'").arg(int(figures::positionOf(fr))).arg(figures::caption(fr), desc);
            b = d->findBlock(fr->lastPosition() + 1);
            continue;
        }
        b = b.next();
    }
    return out.join(QLatin1Char('\n'));
}

namespace {
QString words(quint32 seed, int count)
{
    QString out;
    out.reserve(count * 7);
    quint32 x = seed * 2654435761u + 12345u;
    const int n = int(sizeof(kWords) / sizeof(*kWords));
    for (int i = 0; i < count; ++i) {
        x = x * 1664525u + 1013904223u;
        if (i > 0)
            out += (i % 100 == 0) ? QLatin1Char('\n') : QLatin1Char(' ');
        out += QLatin1String(kWords[(x >> 8) % n]);
        if (i % 12 == 11)
            out += QLatin1Char('.');
    }
    return out + QLatin1Char('.');
}

std::unique_ptr<Project> newProject(const QString &dir, const QString &name, const QString &templateId)
{
    templates::Info info;
    for (const auto &t : templates::all())
        if (t.id == templateId)
            info = t;
    QString err;
    QDir().mkpath(dir);
    auto p = templates::createProject(info, dir, name, &err);
    if (!p)
        note(QStringLiteral("project not made: %1").arg(err));
    return p;
}

void setParagraphs(Project *p, BinderItem *it, const QStringList &paras)
{
    QTextDocument *d = p->docs()->text(it->id);
    QTextCursor c(d);
    c.movePosition(QTextCursor::End);
    for (int i = 0; i < paras.size(); ++i) {
        if (i > 0 || !d->isEmpty())
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(paras[i]);
    }
}

// FX-1 "Novel": 3 parts, 12 chapters, 40 scenes of about 1,000 words, synopses, notes, labels,
// statuses, keywords, comments and footnotes, 4 figures, 3 tables, 10 snapshots
void fillNovel(Project *p, bool ownSettings)
{
    const quint32 seed = 1101;
    if (p->labels.size() < 3) {
        p->labels = {{1, QStringLiteral("Anna"), QColor(0xd0, 0x40, 0x40)}, {2, QStringLiteral("Ben"), QColor(0x40, 0x60, 0xd0)},
                     {3, QStringLiteral("Clara"), QColor(0x40, 0xa0, 0x40)}};
    }
    if (p->statuses.size() < 2)
        p->statuses = {{1, QStringLiteral("To Do")}, {2, QStringLiteral("First Draft")}, {3, QStringLiteral("Revised")}};
    p->keywords << Keyword{QStringLiteral("cs-k1"), QStringLiteral("Harbor"), QColor(0x30, 0x80, 0xc0), QString()}
                << Keyword{QStringLiteral("cs-k2"), QStringLiteral("Storm"), QColor(0x60, 0x60, 0x60), QString()}
                << Keyword{QStringLiteral("cs-k3"), QStringLiteral("Letters"), QColor(0xc0, 0x90, 0x30), QString()};
    const int perChapter[12] = {4, 3, 3, 4, 3, 4, 3, 3, 4, 3, 3, 3};   // 40 scenes
    int scene = 0;
    QList<BinderItem *> scenes;
    for (int part = 0; part < 3; ++part) {
        BinderItem *pf = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Part %1").arg(part + 1));
        for (int ch = 0; ch < 4; ++ch) {
            const int chapter = part * 4 + ch;
            BinderItem *cf = p->model()->createItem(pf, -1, ItemType::Folder, QStringLiteral("Chapter %1").arg(chapter + 1));
            cf->synopsis = QStringLiteral("Chapter %1 in short.").arg(chapter + 1);
            for (int s = 0; s < perChapter[chapter]; ++s) {
                ++scene;
                BinderItem *it = p->model()->createItem(cf, -1, ItemType::Text, QStringLiteral("Scene %1").arg(scene));
                setParagraphs(p, it, atParagraphs(seed + quint32(scene), 1000));
                it->synopsis = atParagraphs(seed + 500 + quint32(scene), 18).join(QLatin1Char(' '));
                QTextCursor n(p->docs()->notes(it->id));
                n.insertText(atParagraphs(seed + 900 + quint32(scene), 40).join(QLatin1Char('\n')));
                it->label = 1 + scene % 3;
                it->status = 1 + scene % 3;
                if (scene % 2)
                    it->keywords << QStringLiteral("cs-k1");
                if (scene % 3 == 0)
                    it->keywords << QStringLiteral("cs-k2");
                scenes << it;
            }
        }
    }
    // comments (scenes 11 to 20) and footnotes (21 to 30) on the third word of the first paragraph
    for (int i = 10; i < 30; ++i) {
        BinderItem *it = scenes.at(i);
        QTextDocument *d = p->docs()->text(it->id);
        const QString t = d->begin().text();
        const int a = int(t.indexOf(QLatin1Char(' ')));
        const int b = a >= 0 ? int(t.indexOf(QLatin1Char(' '), a + 1)) : -1;
        if (a < 0 || b < 0)
            continue;
        QTextCursor c(d);
        c.setPosition(a + 1);
        c.setPosition(b, QTextCursor::KeepAnchor);
        const bool comment = i < 20;
        c.mergeCharFormat(text::noteFormat(comment ? NoteKind::Comment : NoteKind::LinkedFootnote,
                                           QStringLiteral("cs-n%1").arg(i),
                                           comment ? QStringLiteral("Is \"this\" right? Check it.") : QStringLiteral("A footnote with 'quotes'.")));
    }
    // figures in scene 5: in the line, left, right, centered
    {
        BinderItem *it = scenes.at(4);
        QTextDocument *d = p->docs()->text(it->id);
        const figures::Position pos[4] = {figures::InLine, figures::Left, figures::Right, figures::Centered};
        for (int k = 0; k < 4; ++k) {
            QTextBlock b = d->begin();
            for (int j = 0; j < 2 + 3 * k && b.next().isValid(); ++j)
                b = b.next();
            const QImage img = testPicture(120 + 20 * k, 90, 0xff000000u | (0x406080u + 0x203000u * quint32(k)));
            int ip = insertPicture(p, it, d, b.position(), img);
            if (pos[k] != figures::InLine)
                ip = figures::setLayout(d, ip, pos[k], QStringLiteral("Figure %1: the harbor").arg(k + 1), p->formatting);
            figures::setDescription(d, ip, QStringLiteral("A drawing number %1 [test] of the harbor").arg(k + 1));
        }
    }
    // tables in scenes 7, 8 and 9
    {
        QTextBlockFormat cell = display::normalBlockFormat(p->formatting);
        cell.setTextIndent(0);
        const QTextCharFormat cf = display::charFormatFor(p->formatting, QString());
        auto at = [&](BinderItem *it, int para) {
            QTextDocument *d = p->docs()->text(it->id);
            QTextBlock b = d->begin();
            for (int j = 0; j < para && b.next().isValid(); ++j)
                b = b.next();
            QTextCursor c(b);
            c.movePosition(QTextCursor::EndOfBlock);
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
            return c;
        };
        {
            QTextCursor c = at(scenes.at(6), 1);
            QTextTable *t = tables::insert(c, 3, 3, tables::preset(QStringLiteral("header")), cell, cf);
            for (int r = 0; r < 3; ++r)
                for (int col = 0; col < 3; ++col)
                    t->cellAt(r, col).firstCursorPosition().insertText(r == 0 ? QStringLiteral("Head %1").arg(col + 1)
                                                                              : QStringLiteral("R%1 C%2").arg(r).arg(col + 1));
        }
        {
            QTextCursor c = at(scenes.at(7), 2);
            QTextTable *t = tables::insert(c, 120, 4, tables::preset(QStringLiteral("striped")), cell, cf);
            for (int r = 0; r < 120; ++r)
                for (int col = 0; col < 4; ++col)
                    t->cellAt(r, col).firstCursorPosition().insertText(r == 0 ? QStringLiteral("Column %1").arg(col + 1)
                                                                              : QStringLiteral("Row %1 cell %2").arg(r).arg(col + 1));
            t->mergeCells(5, 1, 2, 2);
            t->mergeCells(40, 0, 1, 3);
            tables::setCellBackground(t, 10, 2, QColor(QStringLiteral("#ffeeaa")));
            tables::apply(t, tables::specOf(t));
        }
        {
            QTextCursor c = at(scenes.at(8), 1);
            tables::Spec sp = tables::preset(QStringLiteral("grid"));
            sp.flow = 1;
            sp.widthPercent = 50;
            QTextTable *t = tables::insert(c, 4, 2, sp, cell, cf);
            for (int r = 0; r < 4; ++r)
                for (int col = 0; col < 2; ++col)
                    t->cellAt(r, col).firstCursorPosition().insertText(QStringLiteral("Beside %1.%2").arg(r + 1).arg(col + 1));
        }
    }
    QString err;
    p->docs()->recountAll();
    p->markIndexDirty();
    p->save(&err);
    // snapshots of scenes 1 to 10
    for (int i = 0; i < 10; ++i)
        snapshots::take(p, scenes.at(i)->id, QStringLiteral("Before the revision %1").arg(i + 1), &err);
    if (ownSettings) {
        QSettings s(settingslayer::fileFor(p->settingsDir()), QSettings::IniFormat);
        s.setValue(QStringLiteral("editing/quoteStyle"), QStringLiteral("en"));
        s.setValue(QStringLiteral("editing/spellLanguage"), QStringLiteral("en_US"));
        s.setValue(QStringLiteral("editing/spellUnderline"), QStringLiteral("dots"));
        s.setValue(QStringLiteral("editing/spellIgnoreNumbers"), false);
        s.setValue(QStringLiteral("editing/countChars"), true);
        s.sync();
    }
    p->save(&err);
}

void fillBig(Project *p, int chapters, int scenes, int sceneWords, int researchFolders, int researchDocs)
{
    quint32 seed = 1;
    for (int ch = 0; ch < chapters; ++ch) {
        BinderItem *folder = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter %1").arg(ch + 1));
        for (int s = 0; s < scenes; ++s) {
            BinderItem *it = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Scene %1.%2").arg(ch + 1).arg(s + 1));
            QTextCursor(p->docs()->text(it->id)).insertText(words(seed++, sceneWords));
            it->synopsis = words(seed++, 14);
            if (s % 5 == 0)
                QTextCursor(p->docs()->notes(it->id)).insertText(words(seed++, 40));
        }
    }
    for (int f = 0; f < researchFolders; ++f) {
        BinderItem *folder = p->model()->createItem(p->research(), -1, ItemType::Folder, QStringLiteral("Notes %1").arg(f + 1));
        for (int d = 0; d < researchDocs; ++d) {
            BinderItem *it = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Note %1.%2").arg(f + 1).arg(d + 1));
            QTextCursor(p->docs()->text(it->id)).insertText(words(seed++, sceneWords));
        }
    }
    p->docs()->recountAll();
    p->markIndexDirty();
    QString err;
    p->save(&err);
}

bool record(const QString &dir, const QString &name, const QString &openPath)
{
    return writeText(dir + QLatin1Char('/') + name + QStringLiteral(".open"),
                     QDir(dir + QLatin1Char('/') + name).relativeFilePath(openPath).toUtf8());
}
} // namespace

bool buildFixtures(const QString &dir)
{
    QElapsedTimer t;
    t.start();
    QString err;
    bool ok = true;
    {
        auto p = newProject(dir + QStringLiteral("/fx1-qb"), QStringLiteral("Novel"), QStringLiteral("novel"));
        if (p) {
            fillNovel(p.get(), true);
            ok = record(dir, QStringLiteral("fx1-qb"), p->indexPath()) && ok;
            note(QStringLiteral("FX-1 (Quillbinder format): %1 words, %2 items").arg(p->totalWordCount()).arg(p->allItems().size()));
        } else
            ok = false;
    }
    {
        QTemporaryDir tmp;
        auto p = newProject(tmp.path(), QStringLiteral("Novel"), QStringLiteral("novel"));
        if (p) {
            fillNovel(p.get(), false);
            const QString scriv = dir + QStringLiteral("/fx1-scriv/Novel.scriv");
            QDir().mkpath(dir + QStringLiteral("/fx1-scriv"));
            auto wc = scrivener::convertToScrivener(p.get(), scriv, &err);
            ok = wc != nullptr && record(dir, QStringLiteral("fx1-scriv"), scriv) && ok;
            if (!wc)
                note(QStringLiteral("FX-1 in Scrivener format failed: %1").arg(err));
        } else
            ok = false;
    }
    note(QStringLiteral("FX-1 made in %1 s").arg(t.elapsed() / 1000));
    if (!qEnvironmentVariableIsSet("QB_CS_NO_FX2")) {
        t.restart();
        {   // the writer's size in Scrivener format: 500,000 words in 1,000 documents (as qb_v0605_tests)
            QTemporaryDir tmp;
            auto p = newProject(tmp.path(), QStringLiteral("Big"), QStringLiteral("blank"));
            if (p) {
                fillBig(p.get(), 30, 10, 500, 10, 70);
                const QString scriv = dir + QStringLiteral("/fx2-scriv/Big.scriv");
                QDir().mkpath(dir + QStringLiteral("/fx2-scriv"));
                auto wc = scrivener::convertToScrivener(p.get(), scriv, &err);
                ok = wc != nullptr && record(dir, QStringLiteral("fx2-scriv"), scriv) && ok;
                note(QStringLiteral("FX-2 (Scrivener format): %1 words").arg(p->totalWordCount()));
            } else
                ok = false;
        }
        {   // the writer's project of the crash report: Quillbinder format, RTF, about 380,000 words in 470 texts
            auto p = newProject(dir + QStringLiteral("/fx2-qb"), QStringLiteral("Big RTF"), QStringLiteral("blank"));
            if (p) {
                p->storage = StorageFormat::Rtf;
                fillBig(p.get(), 47, 10, 810, 0, 0);
                ok = record(dir, QStringLiteral("fx2-qb"), p->indexPath()) && ok;
                note(QStringLiteral("FX-2 (Quillbinder format, RTF): %1 words").arg(p->totalWordCount()));
            } else
                ok = false;
        }
        note(QStringLiteral("FX-2 made in %1 s").arg(t.elapsed() / 1000));
    }
    return ok;
}

// shared with the cases of stage 2 (the functions above stay local to this file)
std::unique_ptr<Project> projectFromTemplate(const QString &dir, const QString &name, const QString &templateId)
{
    return newProject(dir, name, templateId);
}
void fillText(Project *p, BinderItem *it, const QStringList &paras) { setParagraphs(p, it, paras); }
void fillNovelInto(Project *p) { fillNovel(p, false); }
void fillBigInto(Project *p, int chapters, int scenes, int sceneWords) { fillBig(p, chapters, scenes, sceneWords, 2, 5); }

QString fixtureCopy(const QString &name)
{
    const QString src = ctx().fixtures + QLatin1Char('/') + name;
    const QString dst = ctx().work + QLatin1Char('/') + name;
    QDir(dst).removeRecursively();
    const bool ok = copyTree(src, dst);
    const QString rel = readText(ctx().fixtures + QLatin1Char('/') + name + QStringLiteral(".open")).trimmed();
    check(ok && !rel.isEmpty(), QStringLiteral("(a fresh copy of %1)").arg(name));
    return QDir::cleanPath(dst + QLatin1Char('/') + rel);
}

// ================================================================ G3 model, G4, verify
namespace {
QString hashOf(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

// the writer's formatting, run by run: what must survive saving (not fonts, which may be resolved)
QString formatRuns(QTextDocument *d)
{
    // 0.64.2: an empty text has no formatting to keep (an empty file is not written; its alignment, set by
    // S-E04's monkey on an empty folder text, is the editor's look of nothing)
    if (d->isEmpty())
        return QString();
    QString out;
    QString last;
    int len = 0;
    auto flush = [&] {
        if (len)
            out += QStringLiteral("%1*%2;").arg(last).arg(len);
        len = 0;
    };
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        flush();
        out += QStringLiteral("B[%1|%2|%3]").arg(bf.stringProperty(props::Style)).arg(int(bf.alignment() & Qt::AlignHorizontal_Mask)).arg(b.textList() ? 1 : 0);
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat f = fr.charFormat();
            // 0.64.2: white space carries no bold, italics, underline or strike here: the Markdown form keeps
            // the markers off white space at their ends (a "**word **" is no emphasis), by design
            const QString text = fr.text();
            int i = 0;
            while (i < text.size()) {
                const bool space = text.at(i).isSpace();
                int j = i + 1;
                while (j < text.size() && text.at(j).isSpace() == space)
                    ++j;
                const QString key = QStringLiteral("%1%2%3%4%5n%6%7%8")
                                        .arg(!space && f.fontWeight() > QFont::Normal ? QLatin1Char('b') : QLatin1Char('-'))
                                        .arg(!space && f.fontItalic() ? QLatin1Char('i') : QLatin1Char('-'))
                                        .arg(!space && f.fontUnderline() ? QLatin1Char('u') : QLatin1Char('-'))
                                        .arg(!space && f.fontStrikeOut() ? QLatin1Char('s') : QLatin1Char('-'))
                                        .arg(int(f.verticalAlignment()))
                                        .arg(f.intProperty(props::NoteKind))
                                        .arg(f.isImageFormat() ? QStringLiteral("img") : QString())
                                        .arg(f.isAnchor() ? QStringLiteral("a:") + f.anchorHref() : QString());
                if (key != last) {
                    flush();
                    last = key;
                }
                len += j - i;
                i = j;
            }
        }
    }
    flush();
    return out;
}

QString itemKey(BinderItem *it)
{
    QStringList parts;
    for (BinderItem *x = it; x && x->parent; x = x->parent)
        parts.prepend(x->title + QStringLiteral("[%1]").arg(x->row()));
    return parts.join(QLatin1Char('/'));
}

// The text as the Scrivener format keeps it. 0.64.2 to 0.81.2: inline notes became linked notes (decision 1A of
// 2026-11-10); since 0.81.3 (the writer's decision of 2026-11-12) they stay inline in Scrivener for Windows' form, so the
// text read back is the text itself.
QString scrivenerText(QTextDocument *d)
{
    return rawText(d);
}

QJsonObject itemModel(Project *p, BinderItem *it, bool reduced)
{
    QJsonObject o;
    o.insert(QStringLiteral("title"), it->title);
    o.insert(QStringLiteral("type"), int(it->type));
    o.insert(QStringLiteral("synopsis"), it->synopsis);
    // 0.90.0: labels, statuses, Include in Compile and keywords are in the .scriv as well (keywords since 0.81.2):
    // a .scriv read afresh gives them back too; keywords by their names (Scrivener's numbers replace this program's ids)
    o.insert(QStringLiteral("label"), it->label);
    o.insert(QStringLiteral("status"), it->status);
    o.insert(QStringLiteral("include"), it->includeInCompile);
    QStringList kw;
    for (const QString &k : it->keywords) {
        QString name = k;
        for (const Keyword &x : p->keywords)
            if (x.id == k)
                name = x.name;
        kw << name;
    }
    kw.sort();
    o.insert(QStringLiteral("keywords"), kw.join(QLatin1Char(',')));
    if (it->hasText()) {
        QTextDocument *d = p->docs()->text(it->id);
        QString raw = rawText(d);
        if (reduced)   // 0.64.2: as the .scriv holds it (the writer's decision 1A of 2026-11-10)
            raw = scrivenerText(d);
        // 0.64.2: up to 2,000,000 characters kept, so that a difference can be named (was 20,000)
        o.insert(QStringLiteral("text"), raw.size() <= 2000000 ? raw : QStringLiteral("sha1:") + hashOf(raw));
        o.insert(QStringLiteral("textLength"), int(raw.size()));
        if (!reduced) {
            const QString runs = formatRuns(d);
            o.insert(QStringLiteral("runs"), hashOf(runs));
            // 0.64.1: the runs themselves (at most 200,000 characters), so that G3 can name the first difference
            o.insert(QStringLiteral("runsText"), runs.left(200000));
        }
        const QString shape = tableShape(d);
        if (!shape.isEmpty())
            o.insert(QStringLiteral("tables"), shape);
        const QString notes = p->docs()->notes(it->id)->toPlainText();
        o.insert(QStringLiteral("notes"), notes.size() <= 4000 ? notes : QStringLiteral("sha1:") + hashOf(notes));
    }
    return o;
}

QJsonObject projectModel(Project *p, bool reduced)
{
    QJsonObject items;
    for (BinderItem *it : p->allItems())
        if (it->parent)
            items.insert(itemKey(it), itemModel(p, it, reduced));
    return items;
}

QString describe(const QJsonValue &v)
{
    const QString s = v.isString() ? v.toString() : QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact));
    return s.size() > 160 ? s.left(160) + QStringLiteral("...") : s;
}

int compareModelsIn(const QJsonObject &want, const QJsonObject &got, const QString &what)
{
    QStringList diffs;
    for (auto it = want.begin(); it != want.end(); ++it) {
        if (!got.contains(it.key())) {
            diffs << QStringLiteral("missing: %1").arg(it.key());
            continue;
        }
        const QJsonObject a = it.value().toObject(), b = got.value(it.key()).toObject();
        for (auto f = a.begin(); f != a.end(); ++f) {
            if (f.key() == QLatin1String("runsText"))
                continue;   // shown with "runs" below
            if (b.value(f.key()) == f.value())
                continue;
            if (f.key() == QLatin1String("runs") && a.contains(QLatin1String("runsText")) && b.contains(QLatin1String("runsText"))) {
                // the first differing format run, with its place and text (0.64.1)
                const QString d = firstDifference(a.value(QLatin1String("runsText")).toString(), b.value(QLatin1String("runsText")).toString(), 120);
                diffs << QStringLiteral("%1: the format runs differ; first difference: %2").arg(it.key(), d.isEmpty() ? QStringLiteral("(beyond the first 200,000 characters)") : d);
                continue;
            }
            // 0.64.2: a text's first difference is named (long texts were cut off before the place)
            if (f.key() == QLatin1String("text") && f.value().isString() && b.value(f.key()).isString()
                && !f.value().toString().startsWith(QLatin1String("sha1:"))) {
                diffs << QStringLiteral("%1: text: %2").arg(it.key(), firstDifference(f.value().toString(), b.value(f.key()).toString(), 60));
                continue;
            }
            diffs << QStringLiteral("%1: %2 was %3, now %4").arg(it.key(), f.key(), describe(f.value()), describe(b.value(f.key())));
        }
    }
    for (auto it = got.begin(); it != got.end(); ++it)
        if (!want.contains(it.key()))
            diffs << QStringLiteral("new after reopening: %1").arg(it.key());
    check(diffs.isEmpty(), QStringLiteral("G3: %1: every item as the case left it (%2 items)").arg(what).arg(want.size()),
          diffs.mid(0, 12).join(QLatin1Char('\n')) + (diffs.size() > 12 ? QStringLiteral("\n... %1 more").arg(diffs.size() - 12) : QString()));
    return int(diffs.size());
}

QStringList strayFiles(const QString &folder, const QString &spare)
{
    QStringList out;
    if (folder.isEmpty())
        return out;
    static const QRegularExpression saveTemp(QStringLiteral("\\.[A-Za-z0-9]{6}$"));
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString n = it.fileName();
        if (f == spare)
            continue;
        if (n.endsWith(QLatin1String(".tmp")) || n.endsWith(QLatin1String(".part")) || n.endsWith(QLatin1Char('~'))
            || (saveTemp.match(n).hasMatch() && n.count(QLatin1Char('.')) >= 2 && QFileInfo::exists(f.left(f.size() - 7))))
            out << QDir(folder).relativeFilePath(f);
    }
    return out;
}

bool xmlParses(const QString &file, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read");
        return false;
    }
    QXmlStreamReader x(&f);
    while (!x.atEnd())
        x.readNext();
    if (x.hasError())
        *error = x.errorString() + QStringLiteral(" at line %1").arg(x.lineNumber());
    return !x.hasError();
}
} // namespace

QJsonObject modelOf(Project *p, bool reduced)
{
    return projectModel(p, reduced);
}

int compareModels(const QJsonObject &want, const QJsonObject &got, const QString &what)
{
    return compareModelsIn(want, got, what);
}

QString runsOf(QTextDocument *d) { return formatRuns(d); }

bool writeModel(Project *p)
{
    QJsonObject m;
    const bool scriv = !p->scrivenerPath.isEmpty();
    m.insert(QStringLiteral("format"), scriv ? QStringLiteral("scriv") : QStringLiteral("qb"));
    m.insert(QStringLiteral("open"), scriv ? p->scrivenerPath : p->indexPath());
    m.insert(QStringLiteral("folder"), p->folderPath());
    m.insert(QStringLiteral("items"), projectModel(p, false));
    if (scriv)
        m.insert(QStringLiteral("itemsReduced"), projectModel(p, true));
    QJsonObject own;
    const QString ini = settingslayer::fileFor(p->settingsDir());
    if (QFileInfo::exists(ini)) {
        QSettings s(ini, QSettings::IniFormat);
        for (const QString &k : s.allKeys())
            own.insert(k, s.value(k).toString());
    }
    m.insert(QStringLiteral("ownSettings"), own);
    return writeText(ctx().dir + QStringLiteral("/model.json"), QJsonDocument(m).toJson(QJsonDocument::Indented));
}

bool finishProject(MainWindow *w)
{
    if (!w || !w->project())
        return false;
    step(QStringLiteral("finish: save, model, close"));
    checkCleanWindows(w);
    Project *p = w->project();
    QString err;
    const bool saved = p->save(&err);
    check(saved, QStringLiteral("the project saves at the end"), err);
    writeModel(p);
    shot(w, QStringLiteral("end"));
    const bool closed = w->closeProject();
    settle(300);
    check(closed, QStringLiteral("the project closes"));
    return saved && closed;
}

int verifyCase(const QString &caseDir)
{
    ctx().dir = caseDir;
    int before = ctx().failures;
    // G1, G2: reports in this home
    QStringList reports;
    for (const QFileInfo &fi : QDir(crash::reportDir()).entryInfoList(QDir::Files))
        reports << fi.absoluteFilePath();
    QStringList crashes, freezes;
    for (const QString &r : reports)
        (QFileInfo(r).fileName().startsWith(QLatin1String("freeze")) ? freezes : crashes) << r;
    check(crashes.isEmpty(), QStringLiteral("G1: no crash report"), crashes.join(QLatin1Char('\n')));
    check(freezes.isEmpty(), QStringLiteral("G2: no freeze report"), freezes.join(QLatin1Char('\n')));
    const QString modelFile = caseDir + QStringLiteral("/model.json");
    if (!QFileInfo::exists(modelFile)) {
        note(QStringLiteral("(no project left by the case: G3 and G4 not checked)"));
        return ctx().failures - before;
    }
    const QJsonObject m = QJsonDocument::fromJson(readText(modelFile).toUtf8()).object();
    const bool scriv = m.value(QStringLiteral("format")).toString() == QLatin1String("scriv");
    const QString open = m.value(QStringLiteral("open")).toString();
    QString err;
    std::unique_ptr<Project> p;
    if (scriv) {
        scrivener::OpenResult r = scrivener::open(open, &err);
        p = std::move(r.project);
        check(p != nullptr, QStringLiteral("G3: the project opens again (Scrivener format, through its working copy)"), err);
        check(r.changedElsewhere == 0, QStringLiteral("G4: it opens without the \"changed outside\" message"),
              QStringLiteral("%1 document(s) counted as changed elsewhere").arg(r.changedElsewhere));
        QString xe;
        check(xmlParses(scrivener::scrivxIn(open), &xe), QStringLiteral("G4: the .scrivx parses as XML"), xe);
    } else {
        p = Project::open(open, &err);
        check(p != nullptr, QStringLiteral("G3: the project opens again"), err);
        QString xe;
        check(xmlParses(open, &xe), QStringLiteral("G4: the project file parses as XML"), xe);
    }
    if (!p)
        return ctx().failures - before;
    compareModelsIn(m.value(QStringLiteral("items")).toObject(), projectModel(p.get(), false), QStringLiteral("reopened"));
    // G4: files of the items, no half-written files, the project's own settings
    {
        QStringList missing;
        for (BinderItem *it : p->allItems())
            if (it->hasText() && !scriv && it->wordCount > 0 && !QFileInfo::exists(p->docs()->textPath(it->id)))
                missing << it->title;
        check(missing.isEmpty(), QStringLiteral("G4: each item with text has its file"), missing.mid(0, 10).join(QLatin1String(", ")));
        QStringList stray = strayFiles(p->folderPath(), Project::spareCopyOf(p->indexPath()));
        if (scriv)
            stray << strayFiles(open, QString());
        check(stray.isEmpty(), QStringLiteral("G4: no temporary or half-written files left"), stray.mid(0, 10).join(QLatin1String(", ")));
        const QString ini = settingslayer::fileFor(p->settingsDir());
        if (QFileInfo::exists(ini)) {
            QSettings s(ini, QSettings::IniFormat);
            QStringList programKeys;
            QJsonObject own;
            for (const QString &k : s.allKeys()) {
                own.insert(k, s.value(k).toString());
                if (settingslayer::programOnly(k))
                    programKeys << k;
            }
            check(s.status() == QSettings::NoError && programKeys.isEmpty(),
                  QStringLiteral("G4: the project's own settings parse and hold no program-only key"), programKeys.join(QLatin1String(", ")));
            check(own == m.value(QStringLiteral("ownSettings")).toObject(), QStringLiteral("G3: the project's own settings are kept"),
                  QString::fromUtf8(QJsonDocument(own).toJson(QJsonDocument::Compact)));
        }
    }
    p.reset();
    // Scrivener format: what Scrivener (or a fresh start elsewhere) reads from the .scriv itself
    if (scriv) {
        QTemporaryDir fresh;
        const QByteArray keep = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", fresh.path().toUtf8());
        scrivener::OpenResult r = scrivener::open(open, &err);
        if (r.project)
            compareModelsIn(m.value(QStringLiteral("itemsReduced")).toObject(), projectModel(r.project.get(), true),
                          QStringLiteral("the .scriv read afresh (titles, texts, notes, synopses, tables)"));
        else
            check(false, QStringLiteral("G3: the .scriv reads afresh"), err);
        r.project.reset();
        qputenv("XDG_DATA_HOME", keep);
    }
    return ctx().failures - before;
}

} // namespace cs
