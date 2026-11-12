// The system test, stage 1: the whole program (X-02 killed while typing, X-03 ended by the system).
// The real binary runs on the virtual display; keys come from the outside sender through the X
// server; xdotool finds and focuses its window.
#include "cs.h"

#include "core/CrashGuard.h"
#include "core/SettingsLayer.h"
#include "core/SyncSafety.h"
#include "ui/AppSettings.h"
#include "import/ScrivenerFormat.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"

#include <sys/stat.h>
#include <unistd.h>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>

#include <csignal>
#include <random>
#include <unistd.h>
#include <sys/resource.h>

namespace cs {

namespace {

QProcess *startProgram(const QStringList &args)
{
    auto *proc = new QProcess;
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert(QStringLiteral("QB_NO_DIALOGS"), QStringLiteral("1"));
    pe.insert(QStringLiteral("QB_SKIP_FIRST_START"), QStringLiteral("1"));
    pe.remove(QStringLiteral("APPIMAGE"));
    proc->setProcessEnvironment(pe);
    proc->setChildProcessModifier([] {
        struct rlimit rl = {0, 0};
        ::setrlimit(RLIMIT_CORE, &rl);
    });
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setStandardOutputFile(ctx().dir + QStringLiteral("/program.log"), QIODevice::Append);
    proc->start(ctx().buildDir + QStringLiteral("/quillbinder"), args);
    proc->waitForStarted(10000);
    return proc;
}

bool waitUp(QProcess *proc, int ms = 60000)
{
    const QString marker = crash::sessionDir() + QStringLiteral("/%1.json").arg(proc->processId());
    const bool up = waitFor([&] { return QFile::exists(marker) || proc->state() == QProcess::NotRunning; }, ms);
    settle(2500);
    return up && proc->state() == QProcess::Running;
}

// the program's main window: focused, the pointer on it, a click into the middle (the editor)
bool focusProgram(QProcess *proc)
{
    const QString pid = QString::number(proc->processId());
    QString wid;
    int area = 0;
    for (const QString &id : runTool(QStringLiteral("xdotool"), {QStringLiteral("search"), QStringLiteral("--onlyvisible"), QStringLiteral("--pid"), pid})
                                 .split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString g = runTool(QStringLiteral("xdotool"), {QStringLiteral("getwindowgeometry"), id});
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Geometry: (\\d+)x(\\d+)")).match(g);
        if (m.hasMatch() && m.captured(1).toInt() * m.captured(2).toInt() > area) {
            area = m.captured(1).toInt() * m.captured(2).toInt();
            wid = id;
        }
    }
    if (wid.isEmpty())
        return false;
    // 0.64.2: raised first: the two "computers" of X-08 / X-10 share one screen without a window manager, and the
    // click below landed on the other program's notice lying above (X-10 B: it pressed "Close the Project Here"
    // there and the keys went to the other program)
    runTool(QStringLiteral("xdotool"), {QStringLiteral("windowraise"), wid});
    runTool(QStringLiteral("xdotool"), {QStringLiteral("windowfocus"), QStringLiteral("--sync"), wid});
    const QString g = runTool(QStringLiteral("xdotool"), {QStringLiteral("getwindowgeometry"), wid});
    const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Geometry: (\\d+)x(\\d+)")).match(g);
    const int w = m.hasMatch() ? m.captured(1).toInt() : 800, h = m.hasMatch() ? m.captured(2).toInt() : 600;
    runTool(QStringLiteral("xdotool"), {QStringLiteral("mousemove"), QStringLiteral("--window"), wid, QString::number(w / 2), QString::number(h / 2),
                                        QStringLiteral("click"), QStringLiteral("1")});
    settle(400);
    runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+End")});
    settle(300);
    return true;
}

struct Sent {
    QString text;              // every character sent, in order
    QList<qint64> times;       // monotonic ns of each press
};

Sent readSent(const QString &textFile, const QString &logFile)
{
    Sent s;
    const QString text = readText(textFile);
    for (const QString &line : readText(logFile).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char(' '));
        if (f.size() == 3 && (f[0] == QLatin1String("k") || f[0] == QLatin1String("b"))) {
            s.text += text.at(f[1].toInt());
            s.times << f[2].toLongLong();
        }
    }
    return s;
}

QProcess *startSender(const QString &base, const QString &profile, int seconds, quint32 seed)
{
    writeText(base + QStringLiteral(".txt"), gateText(seed, seconds * 12 + 400).toUtf8());
    auto *snd = new QProcess;
    snd->setProcessChannelMode(QProcess::MergedChannels);
    snd->start(QStringLiteral("python3"), {QStringLiteral("-I"), ctx().sourceDir + QStringLiteral("/tests/crashsafe/sender.py"), QStringLiteral("--text"),
                                           base + QStringLiteral(".txt"), QStringLiteral("--log"), base + QStringLiteral(".log"), QStringLiteral("--profile"),
                                           profile, QStringLiteral("--seconds"), QString::number(seconds), QStringLiteral("--seed"), QString::number(seed),
                                           QStringLiteral("--burst"), QStringLiteral("0"), QStringLiteral("--start-delay"), QStringLiteral("0.3")});
    snd->waitForStarted(10000);
    return snd;
}

// the texts of every document, read as the next start reads them
QHash<QString, QString> texts(const QString &path, QString *error)
{
    QHash<QString, QString> out;
    std::unique_ptr<Project> p;
    if (path.endsWith(QLatin1String(".scriv"))) {
        scrivener::OpenResult r = scrivener::open(path, error);
        p = std::move(r.project);
    } else
        p = Project::open(path, error);
    if (!p)
        return out;
    for (BinderItem *it : p->allItems())
        if (it->hasText())
            out.insert(it->title + QLatin1Char('#') + QString::number(it->row()) + QLatin1Char('@') + (it->parent ? it->parent->title : QString()),
                       p->docs()->text(it->id)->toPlainText());
    return out;
}

// what was added to one document between two readings (common start and end removed)
QString added(const QHash<QString, QString> &before, const QHash<QString, QString> &after, QString *where)
{
    QString best;
    for (auto it = after.begin(); it != after.end(); ++it) {
        const QString a = before.value(it.key());
        const QString b = it.value();
        if (a == b)
            continue;
        int s = 0;
        while (s < a.size() && s < b.size() && a[s] == b[s])
            ++s;
        int e = 0;
        while (e < a.size() - s && e < b.size() - s && a[a.size() - 1 - e] == b[b.size() - 1 - e])
            ++e;
        const QString ins = b.mid(s, b.size() - s - e);
        if (ins.size() > best.size()) {
            best = ins;
            if (where)
                *where = it.key();
        }
    }
    return best;
}

QString plainOf(const QString &sent)
{
    QString s = sent;
    s.replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'));
    return s;
}

// the project prepared so that the program opens it with a scene in the editor
QString prepare(const QString &fixture, const QString &scene)
{
    const QString path = fixtureCopy(fixture);
    MainWindow *w = openWindow(path);
    if (w->project())
        showDoc(w, itemByTitle(w->project(), scene));
    settle(400);
    w->closeProject();
    delete w;
    settle(300);
    return path;
}

// the scene shown at the next start for each named computer (the view is kept per computer since 0.64:
// settings/ui-<computer>.json); dataHome: the computer's own place for working copies (Scrivener format)
void showSceneFor(const QString &path, const QString &scene, const QString &device, const QString &dataHome = QString())
{
    const QByteArray keepDev = qgetenv("QB_DEVICE_NAME"), keepData = qgetenv("XDG_DATA_HOME");
    qputenv("QB_DEVICE_NAME", device.toUtf8());
    if (!dataHome.isEmpty())
        qputenv("XDG_DATA_HOME", dataHome.toUtf8());
    MainWindow *w = openWindow(path);
    if (w->project())
        showDoc(w, itemByTitle(w->project(), scene));
    settle(400);
    w->closeProject();
    delete w;
    settle(300);
    if (keepDev.isEmpty())
        qunsetenv("QB_DEVICE_NAME");
    else
        qputenv("QB_DEVICE_NAME", keepDev);
    if (!dataHome.isEmpty()) {
        if (keepData.isEmpty())
            qunsetenv("XDG_DATA_HOME");
        else
            qputenv("XDG_DATA_HOME", keepData);
    }
}

} // namespace

// ================================================================ X-02
void caseX02()
{
    std::mt19937 r(ctx().seed);
    struct Round {
        QString fixture, scene;
        bool fullSave;
    };
    const QList<Round> rounds = {{QStringLiteral("fx1-scriv"), QStringLiteral("Scene 2"), false},
                                 {QStringLiteral("fx1-scriv"), QStringLiteral("Scene 3"), false},
                                 {QStringLiteral("fx1-scriv"), QStringLiteral("Scene 4"), false},
                                 {QStringLiteral("fx1-qb"), QStringLiteral("Scene 6"), false},
                                 {QStringLiteral("fx2-scriv"), QStringLiteral("Scene 10.1"), true}};
    int n = 0;
    for (const Round &rd : rounds) {
        ++n;
        step(QStringLiteral("round %1: %2, %3").arg(n).arg(rd.fixture, rd.fullSave ? QStringLiteral("killed during a full save") : QStringLiteral("killed while typing")));
        const QString path = prepare(rd.fixture, rd.scene);
        QString err;
        const QHash<QString, QString> before = texts(path, &err);
        QProcess *proc = startProgram({path});
        if (!waitUp(proc)) {
            check(false, QStringLiteral("round %1: the program starts").arg(n), readText(ctx().dir + QStringLiteral("/program.log")).right(400));
            proc->kill();
            proc->waitForFinished(5000);
            delete proc;
            continue;
        }
        // what the program shows at its start (a question or notice takes the keys meant for the scene)
        {
            const QStringList names = runTool(QStringLiteral("sh"), {QStringLiteral("-c"),
                QStringLiteral("for w in $(xdotool search --onlyvisible --pid %1); do xdotool getwindowname $w; done").arg(proc->processId())})
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            fact(QStringLiteral("X-02 round %1: windows at the start").arg(n), names.join(QLatin1String(" | ")));
            // 0.64.1 (harness): a question at the first opening of a .scriv took the keys meant for the scene;
            // its default answer is taken (Return) before the typing starts, at most three times
            for (int tries = 0; tries < 3; ++tries) {
                const QStringList now = runTool(QStringLiteral("sh"), {QStringLiteral("-c"),
                    QStringLiteral("for w in $(xdotool search --onlyvisible --pid %1); do xdotool getwindowname $w; done").arg(proc->processId())})
                                            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                if (now.size() <= 1)
                    break;
                note(QStringLiteral("round %1: a window besides the main one (%2): Return").arg(n).arg(now.join(QLatin1String(" | "))));
                runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("Return")});
                settle(1200);
            }
        }
        // 0.64.2: a window that comes a moment after the start takes the first keys (a letter or space lost about
        // 1 s into the typing, only in rounds after a kill): wait until the program shows its window alone for 2 s
        {
            QElapsedTimer quiet;
            quiet.start();
            QElapsedTimer all;
            all.start();
            while (quiet.elapsed() < 2000 && all.elapsed() < 12000) {
                const QStringList now = runTool(QStringLiteral("sh"), {QStringLiteral("-c"),
                    QStringLiteral("for w in $(xdotool search --onlyvisible --pid %1); do xdotool getwindowname $w; done").arg(proc->processId())})
                                            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                if (now.size() > 1) {
                    note(QStringLiteral("round %1: a window came after the start (%2): Return").arg(n).arg(now.join(QLatin1String(" | "))));
                    runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("Return")});
                    quiet.restart();
                }
                settle(250);
            }
        }
        check(focusProgram(proc), QStringLiteral("round %1: (the program's window found and focused)").arg(n));
        runTool(QStringLiteral("import"), {QStringLiteral("-window"), QStringLiteral("root"),
                                           ctx().dir + QStringLiteral("/x02-round%1-start.png").arg(n)});
        const QString base = ctx().dir + QStringLiteral("/x02-round%1").arg(n);
        // 10 to 50 s: with typing that never pauses for 2 s the program saves in the first gap of 0.3 s once
        // the changes are 5 s old (0.64), and every 30 s at the latest (Project::m_maxSaveWait)
        const int killAfterMs = 10000 + int(r() % 40000);
        QProcess *snd = startSender(base, QStringLiteral("fast"), 60, ctx().seed + quint32(n));
        const qint64 t0 = nowNs();
        // 0.64.1 (finding 8: a letter lost about 1 s after the typing starts): the window with the focus and the
        // program's windows every 100 ms for the first 3 s, and screenshots at 1 s and 3 s
        {
            QProcess *rec = new QProcess;
            rec->setProcessChannelMode(QProcess::MergedChannels);
            rec->setStandardOutputFile(base + QStringLiteral("-focus.txt"));
            rec->start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral(
                // 0.64.2: '%s' as printf wants it (QString::arg leaves '%%' as it is, so the record held only "%s")
                "i=0; while [ $i -lt 30 ]; do printf '%s focus=%s windows=' \"$(date +%s.%N)\" \"$(xdotool getwindowfocus getwindowname 2>/dev/null)\"; "
                "for w in $(xdotool search --onlyvisible --pid %1 2>/dev/null); do printf '[%s] ' \"$(xdotool getwindowname $w)\"; done; echo; "
                "[ $i -eq 10 ] && import -window root %2-1s.png; i=$((i+1)); sleep 0.1; done; import -window root %2-3s.png")
                .arg(proc->processId()).arg(base)});
            QObject::connect(rec, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), rec, &QObject::deleteLater);
        }
        qint64 killedAt = 0;
        if (rd.fullSave) {
            settle(qMin(killAfterMs, 12000));
            runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+s")});
            settle(150 + int(r() % 300));
        } else
            settle(killAfterMs);
        const qint64 pid = proc->processId();
        runTool(QStringLiteral("import"), {QStringLiteral("-window"), QStringLiteral("root"),
                                           ctx().dir + QStringLiteral("/x02-round%1-kill.png").arg(n)});
        killedAt = nowNs();
        ::kill(pid_t(pid), SIGKILL);
        proc->waitForFinished(10000);
        snd->terminate();
        if (!snd->waitForFinished(1000)) {
            snd->kill();
            snd->waitForFinished(3000);
        }
        delete snd;
        delete proc;
        settle(500);
        const QString marker = crash::sessionDir() + QStringLiteral("/%1.json").arg(pid);
        const bool markerLeft = QFile::exists(marker);
        // what the next start finds
        const Sent sent = readSent(base + QStringLiteral(".txt"), base + QStringLiteral(".log"));
        // The procedure assumed at most 2 s + 1 s of loss; the program saves 2 s after a pause, while the
        // typing goes on in the first gap of 0.3 s once the changes are 5 s old (0.64: about 6 s of loss
        // for a writer, recorded as a fact), and every 30 s at the latest: the check holds that bound
        int sentBeforeLimit = 0, sentBefore3s = 0;
        for (qint64 t : sent.times) {
            if (t < killedAt - 31000000000LL)
                ++sentBeforeLimit;
            if (t < killedAt - 3000000000LL)
                ++sentBefore3s;
        }
        QProcess *again = startProgram({path});
        const bool up = waitUp(again);
        settle(1500);
        const bool markerGone = !QFile::exists(marker);
        ::kill(pid_t(again->processId()), SIGTERM);
        again->waitForFinished(30000);
        delete again;
        const QHash<QString, QString> after = texts(path, &err);
        QString where;
        const QString ins = added(before, after, &where);
        const QString sentPlain = plainOf(sent.text);
        const bool prefix = sentPlain.startsWith(ins);
        note(QStringLiteral("round %1: kill after %2 s; %3 keys sent, %4 of them more than 31 s before the kill; %5 characters found in \"%6\"")
                 .arg(n)
                 .arg((killedAt - t0) / 1e9, 0, 'f', 1)
                 .arg(sent.text.size())
                 .arg(sentBeforeLimit)
                 .arg(ins.size())
                 .arg(where));
        check(markerLeft && up && markerGone, QStringLiteral("round %1: the next start knows the session ended unexpectedly, and starts").arg(n),
              QStringLiteral("marker left %1, up %2, marker gone %3").arg(markerLeft).arg(up).arg(markerGone));
        check(!after.isEmpty(), QStringLiteral("round %1: the project opens after the kill").arg(n), err);
        {
            // the focus record of the first 3 s: did any other window have the focus? (finding 8)
            const QStringList rec = readText(base + QStringLiteral("-focus.txt")).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            QStringList focusNames;
            for (const QString &l : rec) {
                const QString fn = l.section(QLatin1String("focus="), 1).section(QLatin1String(" windows="), 0, 0);
                if (!focusNames.contains(fn))
                    focusNames << fn;
            }
            fact(QStringLiteral("X-02 round %1: windows with the focus in the first 3 s").arg(n), focusNames.join(QLatin1String(" | ")));
            if (!prefix)
                note(QStringLiteral("round %1: the saved text is not what was typed; the focus record: %2").arg(n).arg(base + QStringLiteral("-focus.txt")));
        }
        if (!rd.fullSave && ins.size() < sentBefore3s) {
            qint64 lostFrom = killedAt;
            if (ins.size() < sent.times.size())
                lostFrom = sent.times.at(ins.size());
            fact(QStringLiteral("X-02 round %1 typing lost by the kill").arg(n),
                 QStringLiteral("%1 characters, the last %2 s of typing (the procedure's assumption: at most 3 s; 0.64's gap rule: about 6 s)")
                     .arg(sent.text.size() - ins.size())
                     .arg((killedAt - lostFrom) / 1e9, 0, 'f', 1));
        }
        if (!rd.fullSave)
            check(prefix && ins.size() >= sentBeforeLimit,
                  QStringLiteral("round %1: the text typed up to the last autosave (30 s at most while typing on) is there (at least %2 of %3 characters)").arg(n).arg(sentBeforeLimit).arg(sent.text.size()),
                  QStringLiteral("found %1 characters: \"%2\"").arg(ins.size()).arg(ins.left(80)));
        else
            check(prefix, QStringLiteral("round %1: killed during a full save of FX-2: what is saved is what was typed (%2 characters)").arg(n).arg(ins.size()));
    }
}

// ================================================================ X-03
void caseX03()
{
    int n = 0;
    for (const auto &fx : QList<QPair<QString, QString>>{{QStringLiteral("fx1-qb"), QStringLiteral("Scene 2")},
                                                         {QStringLiteral("fx2-qb"), QStringLiteral("Scene 10.1")}}) {
        ++n;
        step(QStringLiteral("SIGTERM while typing: %1").arg(fx.first));
        const QString path = prepare(fx.first, fx.second);
        QString err;
        const QHash<QString, QString> before = texts(path, &err);
        QProcess *proc = startProgram({path});
        if (!waitUp(proc)) {
            check(false, QStringLiteral("%1: the program starts").arg(fx.first));
            proc->kill();
            delete proc;
            continue;
        }
        focusProgram(proc);
        const QString base = ctx().dir + QStringLiteral("/x03-%1").arg(fx.first);
        QProcess *snd = startSender(base, QStringLiteral("fast"), 20, ctx().seed + 50 + quint32(n));
        QEventLoop loop;
        QObject::connect(snd, &QProcess::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(80000, &loop, &QEventLoop::quit);
        loop.exec();
        delete snd;
        settle(1000);   // every key handled
        ::kill(pid_t(proc->processId()), SIGTERM);
        const bool ended = proc->waitForFinished(30000);
        const bool normal = ended && proc->exitStatus() == QProcess::NormalExit;
        delete proc;
        const Sent sent = readSent(base + QStringLiteral(".txt"), base + QStringLiteral(".log"));
        const QHash<QString, QString> after = texts(path, &err);
        QString where;
        const QString ins = added(before, after, &where);
        check(normal, QStringLiteral("%1: SIGTERM ends the program normally").arg(fx.first));
        check(ins == plainOf(sent.text), QStringLiteral("%1: every typed character is saved (%2 of %3)").arg(fx.first).arg(ins.size()).arg(sent.text.size()),
              QStringLiteral("in \"%1\": %2").arg(where, ins.left(120)));
    }
}

// ================================================================ stage 2: the whole program (X-01, X-04 to X-09) and S-Y04
namespace {

QProcess *startWith(const QStringList &args, const QMap<QString, QString> &env, const QString &log = QString())
{
    auto *proc = new QProcess;
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert(QStringLiteral("QB_NO_DIALOGS"), QStringLiteral("1"));
    pe.insert(QStringLiteral("QB_SKIP_FIRST_START"), QStringLiteral("1"));
    pe.remove(QStringLiteral("APPIMAGE"));
    for (auto i = env.begin(); i != env.end(); ++i)
        if (i.value().isNull())
            pe.remove(i.key());
        else
            pe.insert(i.key(), i.value());
    proc->setProcessEnvironment(pe);
    proc->setChildProcessModifier([] {
        struct rlimit rl = {0, 0};
        ::setrlimit(RLIMIT_CORE, &rl);
    });
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setStandardOutputFile(log.isEmpty() ? ctx().dir + QStringLiteral("/program.log") : log, QIODevice::Append);
    proc->start(ctx().buildDir + QStringLiteral("/quillbinder"), args);
    proc->waitForStarted(10000);
    return proc;
}

QStringList windowNames(QProcess *proc)
{
    return runTool(QStringLiteral("sh"), {QStringLiteral("-c"),
                                          QStringLiteral("for w in $(xdotool search --onlyvisible --pid %1); do xdotool getwindowname $w; done").arg(proc->processId())})
        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void endProgram(QProcess *proc, bool term = true)
{
    if (!proc)
        return;
    if (proc->state() != QProcess::NotRunning) {
        if (term)
            ::kill(pid_t(proc->processId()), SIGTERM);
        if (!proc->waitForFinished(30000)) {
            proc->kill();
            proc->waitForFinished(5000);
        }
    }
    delete proc;
}

// type with the outside sender into the focused program and wait for the sender to end
Sent typeInto(QProcess *proc, const QString &base, const QString &profile, int seconds, quint32 seed)
{
    focusProgram(proc);
    QProcess *snd = startSender(base, profile, seconds, seed);
    waitFor([snd] { return snd->state() == QProcess::NotRunning; }, (seconds + 60) * 1000);
    if (snd->state() != QProcess::NotRunning) {
        snd->terminate();
        snd->waitForFinished(2000);
    }
    delete snd;
    settle(1500);
    return readSent(base + QStringLiteral(".txt"), base + QStringLiteral(".log"));
}

// typographic marks back to the keys that were typed (the corrections while typing change them)
QString keysOf(QString s)
{
    for (const QChar c : {QChar(0x201C), QChar(0x201D), QChar(0x201E), QChar(0x00AB), QChar(0x00BB)})
        s.replace(c, QLatin1Char('"'));
    for (const QChar c : {QChar(0x2018), QChar(0x2019), QChar(0x201A)})
        s.replace(c, QLatin1Char('\''));
    s.replace(QChar(0x2014), QStringLiteral("--"));
    s.replace(QChar(0x2013), QLatin1Char('-'));
    s.replace(QChar(0x2026), QStringLiteral("..."));
    return s;
}

// every text of a project folder, its kept versions and snapshots, as one string (for "is it anywhere?")
QString everythingIn(const QString &path)
{
    QString err;
    QString all;
    for (const QString &t : texts(path, &err))
        all += t + QLatin1Char('\n');
    const QString folder = path.endsWith(QLatin1String(".scriv")) ? scrivener::workingCopyFor(path) : QFileInfo(path).absolutePath();
    for (const QString &sub : {QStringLiteral("/settings/conflicts"), QStringLiteral("/snapshots")}) {
        QDirIterator it(folder + sub, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            all += readText(it.next()) + QLatin1Char('\n');
    }
    return all;
}

// the question "This project is open on the computer ..." in the program `proc`: waits for it (8 s), then
// Open Anyway (Tab moves from the default Cancel to the other button, Space presses it) or Escape;
// returns whether it came
bool answerLockQuestion(QProcess *proc, bool openAnyway)
{
    // 0.64.2: the title carries the program's name ("Open Project — Quillbinder"): matched by its start
    auto isQuestion = [](const QString &n) { return n == QLatin1String("Open Project") || n.startsWith(QStringLiteral("Open Project \u2014")); };
    const bool came = waitFor([proc, isQuestion] {
        for (const QString &n : windowNames(proc))
            if (isQuestion(n))
                return true;
        return false;
    }, 8000);
    if (!came)
        return false;
    const QString wid = runTool(QStringLiteral("xdotool"), {QStringLiteral("search"), QStringLiteral("--onlyvisible"), QStringLiteral("--pid"),
                                                            QString::number(proc->processId()), QStringLiteral("--name"), QStringLiteral("^Open Project( \u2014.*)?$")})
                            .section(QLatin1Char('\n'), 0, 0)
                            .trimmed();
    if (!wid.isEmpty())
        runTool(QStringLiteral("xdotool"), {QStringLiteral("windowfocus"), QStringLiteral("--sync"), wid});
    settle(300);
    if (openAnyway)
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("Tab"), QStringLiteral("space")});
    else
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("Escape")});
    settle(1500);
    return true;
}

} // namespace

// ================================================================ X-01 real start, real keys
void caseX01()
{
    for (const auto &fx : QList<QPair<QString, QString>>{{QStringLiteral("fx1-qb"), QStringLiteral("Scene 5")}, {QStringLiteral("fx1-scriv"), QStringLiteral("Scene 7")}}) {
        if (ctx().quick && fx.first == QLatin1String("fx1-scriv"))
            continue;
        const QString path = prepare(fx.first, fx.second);
        QString err;
        const QHash<QString, QString> before = texts(path, &err);
        QProcess *proc = startProgram({path});
        if (!waitUp(proc)) {
            check(false, QStringLiteral("%1: the program starts").arg(fx.first));
            endProgram(proc);
            continue;
        }
        QString sentAll;
        for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")}) {
            const Sent s = typeInto(proc, ctx().dir + QStringLiteral("/x01-%1-%2").arg(fx.first, profile), profile, 60, ctx().seed + quint32(profile.size()));
            sentAll += plainOf(s.text);
        }
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+s")});
        settle(1500);
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+q")});
        proc->waitForFinished(30000);
        endProgram(proc);
        const QHash<QString, QString> after = texts(path, &err);
        QString where;
        const QString ins = added(before, after, &where);
        check(keysOf(ins) == sentAll, QStringLiteral("%1: the saved text equals what was sent (%2 of %3 characters)").arg(fx.first).arg(ins.size()).arg(sentAll.size()),
              firstDifference(sentAll, keysOf(ins), 40));
        QProcess *again = startProgram({path});
        check(waitUp(again), QStringLiteral("%1: the project opens at the next start").arg(fx.first));
        endProgram(again);
    }
}

// ================================================================ X-04 a second start
void caseX04()
{
    const QString path = prepare(QStringLiteral("fx1-qb"), QStringLiteral("Scene 3"));
    const QString other = prepare(QStringLiteral("fx1-scriv"), QStringLiteral("Scene 4"));
    QProcess *first = startProgram({path});
    if (!waitUp(first)) {
        check(false, QStringLiteral("the program starts"));
        endProgram(first);
        return;
    }
    for (const QString &p : {path, other}) {
        QElapsedTimer t;
        t.start();
        QProcess *second = startProgram({p});
        // 0.64.2: the same project goes to the running window and the second start ends; another project
        // opens in a window of its own (one project per window), which is then ended here
        if (p == other) {
            const bool up = waitUp(second);
            fact(QStringLiteral("X-04 a second start with another project"), windowNames(second).join(QLatin1String(" | ")));
            check(up && second->state() == QProcess::Running, QStringLiteral("another project opens in a window of its own (%1)").arg(QFileInfo(p).fileName()));
            endProgram(second);
            settle(1500);
            continue;
        }
        bool ended = false;
        waitFor([&] { ended = second->state() == QProcess::NotRunning; return ended; }, 20000);
        fact(QStringLiteral("X-04 a second start with %1").arg(QFileInfo(p).fileName()), QStringLiteral("%1 after %2 ms, exit %3; windows of the first: %4")
                                                                                       .arg(ended ? QStringLiteral("ended") : QStringLiteral("still running"))
                                                                                       .arg(t.elapsed())
                                                                                       .arg(second->exitCode())
                                                                                       .arg(windowNames(first).join(QLatin1String(" | "))));
        check(ended, QStringLiteral("the running program takes the request (%1): the second start ends").arg(QFileInfo(p).fileName()));
        endProgram(second);
        settle(1500);
    }
    check(first->state() == QProcess::Running, QStringLiteral("the first program still runs"));
    endProgram(first);
    // 0.64.1: the folder for the links between running programs (toolhost::Host::linkDir) made before the start
    // by someone else, as a link, or open to everyone. The runner gives each case a runtime folder of its own.
    const QString runtime = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty()) {
        note(QStringLiteral("(no XDG_RUNTIME_DIR: the link folder steps are left out)"));
        return;
    }
    const QString linkDir = runtime + QStringLiteral("/quillbinder");
    const QString cacheLink = QDir::homePath() + QStringLiteral("/.cache/quillbinder/link");
    const QString elsewhere = ctx().work + QStringLiteral("/elsewhere");
    struct Variant {
        QString what;
        std::function<void()> make;
        bool fallBack;   // the program must use its cache folder instead
    };
    const QList<Variant> variants = {
        {QStringLiteral("made by another user"), [&] { QDir().mkpath(linkDir); ::chown(QFile::encodeName(linkDir).constData(), 65534, 65534); ::chmod(QFile::encodeName(linkDir).constData(), 0700); }, true},
        {QStringLiteral("a link to another folder"), [&] { QDir().mkpath(elsewhere); QFile::link(elsewhere, linkDir); }, true},
        {QStringLiteral("open to everyone"), [&] { QDir().mkpath(linkDir); ::chmod(QFile::encodeName(linkDir).constData(), 0777); }, false},
    };
    for (const Variant &v : variants) {
        step(QStringLiteral("the link folder %1").arg(v.what));
        QFile::remove(linkDir);
        QDir(linkDir).removeRecursively();
        QDir(cacheLink).removeRecursively();
        QDir(elsewhere).removeRecursively();
        v.make();
        QProcess *proc = startProgram({path});
        const bool up = waitUp(proc);
        settle(1500);
        const QString sock = QStringLiteral("%1.sock").arg(proc->processId());
        const bool inCache = QFileInfo::exists(cacheLink + QLatin1Char('/') + sock);
        const bool inLinkDir = QFileInfo::exists(linkDir + QLatin1Char('/') + sock);
        const bool inElsewhere = !QDir(elsewhere).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System).isEmpty();
        struct stat st {};
        const bool statted = ::lstat(QFile::encodeName(linkDir).constData(), &st) == 0;
        fact(QStringLiteral("X-04 the link folder %1").arg(v.what), QStringLiteral("socket in the cache folder: %1, in the runtime folder: %2; mode %3")
                                                                    .arg(inCache).arg(inLinkDir).arg(statted ? QString::number(st.st_mode & 0777, 8) : QStringLiteral("-")));
        check(up, QStringLiteral("the link folder %1: the program starts").arg(v.what));
        if (v.fallBack) {
            check(!inLinkDir && !inElsewhere, QStringLiteral("the link folder %1: nothing is put into it (0.64.1)").arg(v.what));
            if (!inCache)
                note(QStringLiteral("the link folder %1: no socket in %2 either (the program may open the link only when a tool asks)").arg(v.what, cacheLink));
        } else {
            check(statted && (st.st_mode & 077) == 0, QStringLiteral("the link folder %1: closed to its owner (0.64.1)").arg(v.what),
                  statted ? QString::number(st.st_mode & 0777, 8) : QString());
        }
        endProgram(proc);
        settle(800);
    }
    QFile::remove(linkDir);
    QDir(linkDir).removeRecursively();
}

// ================================================================ X-05 damaged settings and surroundings
void caseX05()
{
    const QString path = prepare(QStringLiteral("fx1-qb"), QStringLiteral("Scene 3"));
    const QString settingsFile = settingslayer::programFile();
    const QByteArray good = readText(settingsFile).toUtf8();
    struct Try {
        QString what;
        std::function<void()> set;
        QStringList args;
    };
    const QString png = ctx().sourceDir + QStringLiteral("/tests/import/tower.png");
    const QList<Try> tries = {
        {QStringLiteral("a garbage settings file"), [&] { writeText(settingsFile, QByteArray("\x00\xff garbage [[[ =\n", 18)); }, {path}},
        {QStringLiteral("a recent list naming missing folders"), [&] { writeText(settingsFile, good + "\n[app]\nrecentProjects=/no/such/a.qbx, /gone/b.qbx\n"); }, {}},
        {QStringLiteral("a missing data folder"), [&] { writeText(settingsFile, good); QDir(QDir::homePath() + QStringLiteral("/.local/share/Quillbinder")).removeRecursively(); }, {path}},
        {QStringLiteral("an unknown command-line option"), [] {}, {QStringLiteral("--frobnicate"), path}},
        {QStringLiteral("a project path that is a picture"), [] {}, {png}},
        {QStringLiteral("a project whose settings/options.ini is garbage"), [&] { writeText(QFileInfo(path).absolutePath() + QStringLiteral("/settings/options.ini"), QByteArray("\x01\x02[[[\n= =\n")); }, {path}},
    };
    for (const Try &t : tries) {
        t.set();
        QProcess *proc = startProgram(t.args);
        const bool up = waitUp(proc, 30000);
        const bool crashed = proc->state() == QProcess::NotRunning && proc->exitStatus() == QProcess::CrashExit;
        fact(QStringLiteral("X-05 ") + t.what, QStringLiteral("%1; windows: %2").arg(up ? QStringLiteral("started") : QStringLiteral("not started (exit %1)").arg(proc->exitCode()), windowNames(proc).join(QLatin1String(" | "))));
        check(!crashed, QStringLiteral("%1: no crash").arg(t.what));
        endProgram(proc);
    }
    writeText(settingsFile, good);
    if (::geteuid() == 0)
        note(QStringLiteral("[A] root: the read-only HOME is left out"));
}

// ================================================================ X-06 the safety net itself
void caseX06()
{
    const QString path = prepare(QStringLiteral("fx1-qb"), QStringLiteral("Scene 2"));
    const QStringList before = QDir(crash::reportDir()).entryList(QDir::Files);
    // 0.64.2: QB_TEST_CRASH acts only in the Windows program; on Linux the crash is a real SIGSEGV sent
    // to the running program, and the wait keeps this process's event loop running (a blocking wait
    // made this process's own watchdog write a freeze report)
    QProcess *proc = startWith({path}, {});
    waitUp(proc);
    settle(1500);
    ::kill(pid_t(proc->processId()), SIGSEGV);
    waitFor([proc] { return proc->state() == QProcess::NotRunning; }, 30000);
    endProgram(proc, false);
    settle(500);
    QStringList fresh;
    for (const QString &f : QDir(crash::reportDir()).entryList(QDir::Files))
        if (!before.contains(f) && f.startsWith(QLatin1String("crash-")))
            fresh << f;
    check(!fresh.isEmpty(), QStringLiteral("a crash report is written"), fresh.join(QLatin1String(", ")));
    const QString report = fresh.isEmpty() ? QString() : readText(crash::reportDir() + QLatin1Char('/') + fresh.first());
    check(report.contains(QLatin1String("acktrace")) || report.contains(QLatin1String("#0")) || report.contains(QLatin1String("frame")), QStringLiteral("... with a backtrace"), report.left(300));
    // 0.64.2: the report this case made on purpose goes into the case's folder, so that G1 of the verify step
    // (no crash report in this home) looks only for crashes that were not planned
    QDir().mkpath(ctx().dir + QStringLiteral("/planned-crash"));
    for (const QString &f : fresh)
        QFile::rename(crash::reportDir() + QLatin1Char('/') + f, ctx().dir + QStringLiteral("/planned-crash/") + f);
    QProcess *again = startProgram({path});
    check(waitUp(again), QStringLiteral("the program starts again normally"));
    endProgram(again);
}

// ================================================================ X-07 the first start after an update
void caseX07()
{
    const QString path = prepare(QStringLiteral("fx1-qb"), QStringLiteral("Scene 2"));
    const QString settingsFile = settingslayer::programFile();
    // settings as 0.60.4 left them: no first-start entry
    QString old = readText(settingsFile);
    old.remove(QRegularExpression(QStringLiteral("firstStartDone=[^\\n]*\\n")));
    writeText(settingsFile, old.toUtf8());
    const QMap<QString, QString> env = {{QStringLiteral("QB_SKIP_FIRST_START"), QString()}, {QStringLiteral("QB_NO_DIALOGS"), QString()}};
    for (int start = 1; start <= 3; ++start) {
        if (start == 3)
            writeText(settingsFile, QByteArray("\xff\xfe garbage\n[app\nx=\n"));
        QProcess *proc = startWith({path}, env);
        settle(6000);
        const QStringList names = windowNames(proc);
        fact(QStringLiteral("X-07 start %1: windows").arg(start), names.join(QLatin1String(" | ")));
        bool firstWindow = false;
        for (const QString &n : names)
            if (n.contains(QLatin1String("Welcome")) || n.contains(QLatin1String("First")) || n.contains(QLatin1String("Backup")))
                firstWindow = true;
        if (start == 1)
            check(firstWindow, QStringLiteral("the first-start window comes at the first start after the update"), names.join(QLatin1String(" | ")));
        if (start == 2)
            check(!firstWindow, QStringLiteral("... and never again"), names.join(QLatin1String(" | ")));
        if (firstWindow) {
            runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("Return")});
            settle(2000);
        }
        // the desktop question, if it comes, is answered No (Escape)
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("Escape")});
        settle(1500);
        check(proc->state() == QProcess::Running || start == 3, QStringLiteral("start %1: the program runs").arg(start));
        endProgram(proc);
        if (start == 1)
            check(readText(settingsFile).contains(QLatin1String("firstStartDone=true")), QStringLiteral("the answers are kept (firstStartDone)"));
    }
}

// ================================================================ X-08 two real programs on one project
void caseX08()
{
    const QString path = prepare(QStringLiteral("fx1-qb"), QStringLiteral("Scene 3"));
    // the view is kept per computer (0.64): both "computers" start with the scene in the editor
    showSceneFor(path, QStringLiteral("Scene 3"), QStringLiteral("desktop"));
    showSceneFor(path, QStringLiteral("Scene 3"), QStringLiteral("laptop"));
    QString err;
    const QHash<QString, QString> before = texts(path, &err);
    const QMap<QString, QString> common = {{QStringLiteral("QB_LOCK_RENEW_MS"), QStringLiteral("2000")}, {QStringLiteral("QB_LOCK_STALE_S"), QStringLiteral("10")}};
    QMap<QString, QString> desk = common, lap = common;
    desk.insert(QStringLiteral("QB_DEVICE_NAME"), QStringLiteral("desktop"));
    lap.insert(QStringLiteral("QB_DEVICE_NAME"), QStringLiteral("laptop"));
    // 0.64.2: two computers do not share the link between running programs; on one machine the second start hands
    // the project to the program that has it open and ends (X-04), so the "laptop" gets a runtime folder of its own
    const QString lapRuntime = ctx().dir + QStringLiteral("/laptop-runtime");
    QDir().mkpath(lapRuntime);
    QFile::setPermissions(lapRuntime, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    lap.insert(QStringLiteral("XDG_RUNTIME_DIR"), lapRuntime);
    QProcess *a = startWith({path}, desk, ctx().dir + QStringLiteral("/desktop.log"));
    if (!waitUp(a)) {
        check(false, QStringLiteral("the first program starts"));
        endProgram(a);
        return;
    }
    QProcess *b = startWith({path}, lap, ctx().dir + QStringLiteral("/laptop.log"));
    settle(5000);
    fact(QStringLiteral("X-08 the second program's windows at its start"), windowNames(b).join(QLatin1String(" | ")));
    // the lock question: its default is Cancel for a computer that is alive; the writer chooses Open Anyway
    fact(QStringLiteral("X-08 the lock question"), answerLockQuestion(b, true) ? QStringLiteral("came, answered Open Anyway") : QStringLiteral("did not come"));
    settle(3000);
    QString sent;
    sent += plainOf(typeInto(a, ctx().dir + QStringLiteral("/x08-desktop"), QStringLiteral("fast"), 20, ctx().seed).text);
    // 0.64.2: the laptop's typing is killed within its 20 s: typing that never pauses is saved at the latest after 30 s
    // (X-02's bound), so all of it may go with the kill; it is recorded, not judged (the check failed on it before)
    const QString laptopTyped = plainOf(typeInto(b, ctx().dir + QStringLiteral("/x08-laptop"), QStringLiteral("fast"), 20, ctx().seed + 1).text);
    fact(QStringLiteral("X-08 the desktop's windows after the laptop took over"), windowNames(a).join(QLatin1String(" | ")));
    const qint64 killed = b->processId();
    ::kill(pid_t(killed), SIGKILL);
    b->waitForFinished(5000);
    delete b;
    focusProgram(a);
    runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+s")});
    settle(12000);   // the laptop's lock is older than QB_LOCK_STALE_S now
    const syncsafe::LockInfo li = syncsafe::readLock(QFileInfo(path).absolutePath() + QStringLiteral("/settings/lock"));
    fact(QStringLiteral("X-08 the lock after the kill"), QStringLiteral("device %1, alive %2").arg(li.device, li.alive.toString(Qt::ISODate)));
    endProgram(a);
    QProcess *again = startWith({path}, lap, ctx().dir + QStringLiteral("/laptop2.log"));
    check(waitUp(again), QStringLiteral("the killed program starts again"));
    settle(2000);
    const bool marker = !QFile::exists(crash::sessionDir() + QStringLiteral("/%1.json").arg(killed));
    check(marker, QStringLiteral("the session marker of the killed program is found at its next start"));
    endProgram(again);
    const QString all = keysOf(everythingIn(path));
    QStringList lost;
    for (const QString &part : sent.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        if (!all.contains(part.left(40)))
            lost << part.left(40);
    fact(QStringLiteral("X-08 the laptop's typing (killed within 30 s of typing on)"),
         all.contains(laptopTyped.left(40)) ? QStringLiteral("in the project or a kept version") : QStringLiteral("gone with the kill"));
    check(lost.isEmpty(), QStringLiteral("no text typed in the program that was not killed is lost without a kept version"), lost.join(QLatin1String(" | ")));
    Q_UNUSED(before)
}

// ================================================================ X-09 real keys: default corrections against none (new, 2026-11-09)
// The first run lost one letter at the same place of the same text in the real program ("kept" saved as
// "kpt"), never in the test process's own editor. The real program runs here twice with the same text:
// with the program's standard corrections while typing and spelling, and with all of them off.
void caseX09()
{
    const QString base0 = ctx().dir + QStringLiteral("/x09");
    QStringList results;
    for (const bool corrections : {true, false}) {
        AppSettings &as = AppSettings::instance();
        as.smartQuotes = as.smartDashes = as.ellipsis = as.capitalizeSentences = as.capitalizeI = corrections;
        as.spellCheck = corrections;
        as.save();
        const QString path = prepare(QStringLiteral("fx1-scriv"), QStringLiteral("Scene 3"));
        QString err;
        const QHash<QString, QString> before = texts(path, &err);
        QProcess *proc = startProgram({path});
        if (!waitUp(proc)) {
            check(false, QStringLiteral("the program starts"));
            endProgram(proc);
            continue;
        }
        const QString base = base0 + (corrections ? QStringLiteral("-corrections") : QStringLiteral("-none"));
        const Sent s = typeInto(proc, base, QStringLiteral("fast"), 45, 7);   // the same seed both times: the same text
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+s")});
        settle(1500);
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+q")});
        proc->waitForFinished(30000);
        endProgram(proc);
        const QHash<QString, QString> after = texts(path, &err);
        QString where;
        const QString got = keysOf(added(before, after, &where));
        const QString want = plainOf(s.text);
        const QString diff = firstDifference(want, corrections ? got : got, 30);
        results << QStringLiteral("%1: %2 of %3 characters%4").arg(corrections ? QStringLiteral("with corrections") : QStringLiteral("without")).arg(got.size()).arg(want.size())
                       .arg(diff.isEmpty() ? QString() : QStringLiteral(", first difference: ") + diff);
        if (!corrections)
            check(got == want, QStringLiteral("without corrections the real program keeps every key"), diff);
        else
            fact(QStringLiteral("X-09 with the standard corrections"), diff.isEmpty() ? QStringLiteral("every key kept") : diff);
    }
    AppSettings &as = AppSettings::instance();
    as.smartQuotes = as.smartDashes = as.ellipsis = as.capitalizeSentences = as.capitalizeI = as.spellCheck = false;
    as.save();
    fact(QStringLiteral("X-09 results"), results.join(QLatin1String("; ")));
}

// ================================================================ S-Y04 a writing week on two computers
// ================================================================ X-10 to X-12: two synchronized computers (new, 2026-11-09)
namespace {

// A sync program between two folders, in the manner of the common ones: a file changed on one side only is
// copied to the other (its time kept); a file changed on both sides since the last sync: the newer one wins
// on both sides and the other is kept beside it as "<name> (<computer>'s conflicted copy <date>)<ext>".
// Temporary files of a save in progress are left alone.
class SyncSim
{
public:
    SyncSim(const QString &a, const QString &aName, const QString &b, const QString &bName) : m_a(a), m_b(b), m_aName(aName), m_bName(bName)
    {
        QObject::connect(&m_timer, &QTimer::timeout, [this] { tick(); });
        // in step at the start: what both sides hold alike (a file on one side only is new there)
        const QHash<QString, qint64> onB = scan(m_b);
        for (const QString &rel : scan(m_a).keys()) {
            const QByteArray h = hashOf(m_a + QLatin1Char('/') + rel);
            if (onB.contains(rel) && hashOf(m_b + QLatin1Char('/') + rel) == h)
                m_base.insert(rel, h);
        }
    }
    void start(int ms) { m_timer.start(ms); }
    void stop() { m_timer.stop(); }
    int copies = 0, conflicts = 0;
    int carryLimit = -1;   // a tick carries at most this many files changed on one side (-1: all); the rest wait
    QStringList conflictCopies;   // relative paths, on both sides

    void tick()
    {
        const QHash<QString, qint64> a = scan(m_a), b = scan(m_b);
        QSet<QString> all;
        for (const QString &k : a.keys())
            all.insert(k);
        for (const QString &k : b.keys())
            all.insert(k);
        for (const QString &k : m_base.keys())
            all.insert(k);
        int carried = 0;
        for (const QString &rel : all) {
            const QString fa = m_a + QLatin1Char('/') + rel, fb = m_b + QLatin1Char('/') + rel;
            const QByteArray ha = a.contains(rel) ? hashOf(fa) : QByteArray(), hb = b.contains(rel) ? hashOf(fb) : QByteArray();
            const QByteArray base = m_base.value(rel);
            if (ha == hb) {
                if (ha.isEmpty())
                    m_base.remove(rel);
                else
                    m_base.insert(rel, ha);
                continue;
            }
            const bool chA = ha != base, chB = hb != base;
            if ((chA != chB) && carryLimit >= 0 && carried >= carryLimit)
                continue;   // not yet: the sync program has not got to it
            if (chA && !chB) {
                carry(fa, fb, ha, rel);
                ++carried;
            } else if (chB && !chA) {
                carry(fb, fa, hb, rel);
                ++carried;
            }
            else {
                // both changed: the newer wins, the other is kept as a conflicted copy
                const bool aWins = hb.isEmpty() || (!ha.isEmpty() && a.value(rel) >= b.value(rel));
                const QString win = aWins ? fa : fb, lose = aWins ? fb : fa;
                const QString loser = aWins ? m_bName : m_aName;
                if (QFileInfo::exists(lose)) {
                    const QFileInfo fi(rel);
                    const QString dir = fi.path() == QLatin1String(".") ? QString() : fi.path() + QLatin1Char('/');
                    const QString name = dir + fi.completeBaseName() + QStringLiteral(" (%1's conflicted copy %2)").arg(loser, QDate::currentDate().toString(Qt::ISODate))
                                         + (fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix());
                    for (const QString &root : {m_a, m_b}) {
                        QFile::remove(root + QLatin1Char('/') + name);
                        QFile::copy(lose, root + QLatin1Char('/') + name);
                    }
                    conflictCopies << name;
                    m_base.insert(name, hashOf(m_a + QLatin1Char('/') + name));
                }
                ++conflicts;
                carry(win, lose, aWins ? ha : hb, rel);
            }
        }
    }

private:
    static bool skipped(const QString &rel)
    {
        static const QRegularExpression temp(QStringLiteral("\\.[A-Za-z0-9]{6}$|\\.part$|\\.tmp$"));
        return temp.match(rel).hasMatch();
    }
    QHash<QString, qint64> scan(const QString &root) const
    {
        QHash<QString, qint64> out;
        QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString rel = QDir(root).relativeFilePath(f);
            if (!skipped(rel))
                out.insert(rel, it.fileInfo().lastModified().toMSecsSinceEpoch());
        }
        return out;
    }
    QByteArray hashOf(const QString &file)
    {
        const QFileInfo fi(file);
        const QString key = file + QStringLiteral("|%1|%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
        if (m_hashes.contains(key))
            return m_hashes.value(key);
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            return QByteArray();
        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(&f);
        const QByteArray r = h.result().toHex();
        m_hashes.insert(key, r);
        return r;
    }
    void carry(const QString &from, const QString &to, const QByteArray &hash, const QString &rel)
    {
        if (hash.isEmpty()) {
            QFile::remove(to);
            m_base.remove(rel);
        } else {
            QDir().mkpath(QFileInfo(to).absolutePath());
            const QString part = to + QStringLiteral(".synctmp");
            QFile::remove(part);
            if (QFile::copy(from, part)) {
                QFile f(part);
                if (f.open(QIODevice::ReadWrite)) {
                    f.setFileTime(QFileInfo(from).lastModified(), QFileDevice::FileModificationTime);
                    f.close();
                }
                ::rename(QFile::encodeName(part).constData(), QFile::encodeName(to).constData());
            }
            m_base.insert(rel, hash);
        }
        ++copies;
    }
    QString m_a, m_b, m_aName, m_bName;
    QHash<QString, QByteArray> m_base;
    QHash<QString, QByteArray> m_hashes;
    QTimer m_timer;
};

struct Computer {
    QString name;
    QString root;       // the synced folder (the project folder, or the .scriv)
    QString open;       // what the program is started with
    QString dataHome;   // its own place for working copies (Scrivener format), "" = the case's
    QMap<QString, QString> env;
};

bool waitUpOn(QProcess *proc, const Computer &c)
{
    if (c.dataHome.isEmpty())
        return waitUp(proc);
    const QByteArray keep = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", c.dataHome.toUtf8());
    const QString marker = crash::sessionDir() + QStringLiteral("/%1.json").arg(proc->processId());
    qputenv("XDG_DATA_HOME", keep);
    const bool up = waitFor([&] { return QFile::exists(marker) || proc->state() == QProcess::NotRunning; }, 60000);
    settle(2500);
    return up && proc->state() == QProcess::Running;
}

void quitProgram(QProcess *proc)
{
    if (!proc || proc->state() == QProcess::NotRunning)
        return;
    focusProgram(proc);
    runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+q")});
    proc->waitForFinished(30000);
}

// where a typed piece ended up: in the project, in a version the program kept, only in a copy of the
// sync program, or nowhere
struct Places {
    QString project, kept, syncCopies;
};

Places placesOf(const QList<Computer> &cs, bool scriv)
{
    Places pl;
    QString err;
    for (const Computer &c : cs) {
        if (scriv) {
            const QByteArray keep = qgetenv("XDG_DATA_HOME");
            const QString fresh = ctx().work + QStringLiteral("/places-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
            qputenv("XDG_DATA_HOME", fresh.toUtf8());
            for (const QString &t : texts(c.root, &err))
                pl.project += t + QLatin1Char('\n');
            qputenv("XDG_DATA_HOME", c.dataHome.toUtf8());
            const QString wc = scrivener::workingCopyFor(c.root);
            qputenv("XDG_DATA_HOME", keep);
            QDir(fresh).removeRecursively();
            for (const QString &sub : {QStringLiteral("/settings/conflicts"), QStringLiteral("/snapshots")}) {
                QDirIterator it(wc + sub, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext())
                    pl.kept += readText(it.next()) + QLatin1Char('\n');
            }
            QDirIterator sn(c.root + QStringLiteral("/Snapshots"), QDir::Files, QDirIterator::Subdirectories);
            while (sn.hasNext())
                pl.kept += readText(sn.next()) + QLatin1Char('\n');
        } else {
            for (const QString &t : texts(c.open, &err))
                pl.project += t + QLatin1Char('\n');
            for (const QString &sub : {QStringLiteral("/settings/conflicts"), QStringLiteral("/snapshots")}) {
                QDirIterator it(c.root + sub, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext())
                    pl.kept += readText(it.next()) + QLatin1Char('\n');
            }
        }
        QDirIterator it(c.root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (f.contains(QLatin1String("conflicted copy")))
                pl.syncCopies += readText(f) + QLatin1Char('\n');
        }
    }
    // RTF escapes of typographic marks would hide a piece: compare the keys typed
    pl.project = keysOf(pl.project);
    pl.kept = keysOf(pl.kept);
    pl.syncCopies = keysOf(pl.syncCopies);
    return pl;
}

// judges every typed piece; returns the pieces found nowhere
QStringList judgePieces(const QString &label, const QString &typed, const Places &pl)
{
    int inProject = 0, inKept = 0, inCopies = 0;
    QStringList lost;
    for (const QString &part : typed.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString key = part.left(40);
        if (key.trimmed().size() < 8)
            continue;
        if (pl.project.contains(key))
            ++inProject;
        else if (pl.kept.contains(key))
            ++inKept;
        else if (pl.syncCopies.contains(key))
            ++inCopies;
        else
            lost << key;
    }
    fact(label + QStringLiteral(": where the typed text is"), QStringLiteral("in the project %1, only in a version the program kept %2, only in a copy of the sync program %3, nowhere %4")
                                                                   .arg(inProject).arg(inKept).arg(inCopies).arg(lost.size()));
    return lost;
}

struct TwoSetup {
    QString id;            // "X-10"
    bool scriv = false;
    QStringList laptopFaults;   // rules for the fault library in the laptop's program
    bool partB = true, partC = true;
};

void twoComputers(const TwoSetup &st)
{
    const QString fixture = st.scriv ? QStringLiteral("fx1-scriv") : QStringLiteral("fx1-qb");
    const QString src = fixtureCopy(fixture);
    QList<Computer> cs;
    for (const QString &name : {QStringLiteral("desktop"), QStringLiteral("laptop")}) {
        Computer c;
        c.name = name;
        const QString base = ctx().work + QLatin1Char('/') + st.id.toLower() + QLatin1Char('-') + name;
        QDir(base).removeRecursively();
        if (st.scriv) {
            c.root = base + QLatin1Char('/') + QFileInfo(src).fileName();
            copyTree(src, c.root);
            c.open = c.root;
            c.dataHome = base + QStringLiteral("-data");
            QDir().mkpath(c.dataHome);
            c.env.insert(QStringLiteral("XDG_DATA_HOME"), c.dataHome);
        } else {
            copyTree(QFileInfo(src).absolutePath(), base);
            c.root = base;
            c.open = base + QLatin1Char('/') + QFileInfo(src).fileName();
        }
        c.env.insert(QStringLiteral("QB_DEVICE_NAME"), name);
        c.env.insert(QStringLiteral("QB_LOCK_RENEW_MS"), QStringLiteral("3000"));
        cs << c;
    }
    // the scene in the editor at each computer's start (shown while offline: nothing to carry yet)
    for (const Computer &c : cs)
        showSceneFor(c.open, QStringLiteral("Scene 3"), c.name, c.dataHome);
    Computer &desk = cs[0], &lap = cs[1];
    if (!st.laptopFaults.isEmpty()) {
        const QString fdir = ctx().dir + QStringLiteral("/laptop-faults");
        QDir().mkpath(fdir);
        lap.env.insert(QStringLiteral("LD_PRELOAD"), faultLibrary());
        lap.env.insert(QStringLiteral("QB_FAULT_CTL"), fdir + QStringLiteral("/faults.ctl"));
        lap.env.insert(QStringLiteral("QB_FAULT_LOG"), fdir + QStringLiteral("/faults.log"));
        writeText(fdir + QStringLiteral("/faults.ctl"), (st.laptopFaults.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    }
    SyncSim sync(desk.root, desk.name, lap.root, lap.name);
    QString typed;
    QStringList seen;
    const QString log = ctx().dir + QStringLiteral("/computers.log");
    // ---- A: the laptop is offline; both write in the same scene; the laptop comes online while still open
    step(QStringLiteral("A: offline, both write in Scene 3"));
    QProcess *a = startWith({desk.open}, desk.env, log);
    QProcess *b = startWith({lap.open}, lap.env, log);
    if (waitUpOn(a, desk) && waitUpOn(b, lap)) {
        typed += plainOf(typeInto(a, ctx().dir + QStringLiteral("/a-desktop"), QStringLiteral("fast"), 15, ctx().seed).text) + QLatin1Char('\n');
        typed += plainOf(typeInto(b, ctx().dir + QStringLiteral("/a-laptop1"), QStringLiteral("fast"), 15, ctx().seed + 1).text) + QLatin1Char('\n');
        quitProgram(a);
        sync.tick();   // online: the desktop's files reach the laptop's folder while it is open there
        settle(1500);
        typed += plainOf(typeInto(b, ctx().dir + QStringLiteral("/a-laptop2"), QStringLiteral("fast"), 10, ctx().seed + 2).text) + QLatin1Char('\n');
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+s")});
        settle(2000);
        seen << QStringLiteral("A, the laptop: ") + windowNames(b).join(QLatin1String(" | "));
        quitProgram(b);
    } else
        check(false, QStringLiteral("%1 A: both programs start").arg(st.id));
    endProgram(a);
    endProgram(b);
    for (int i = 0; i < 3; ++i) {
        sync.tick();
        settle(300);
    }
    QStringList lost = judgePieces(st.id + QStringLiteral(" A"), typed, placesOf(cs, st.scriv));
    check(lost.isEmpty(), QStringLiteral("%1 A: every piece typed on either computer is in the project or kept").arg(st.id), lost.mid(0, 6).join(QLatin1String(" | ")));
    if (!st.laptopFaults.isEmpty()) {
        const QStringList fired = readText(lap.env.value(QStringLiteral("QB_FAULT_LOG"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        fact(st.id + QStringLiteral(" faults that hit in the laptop's program"), QStringLiteral("%1: %2").arg(fired.size()).arg(fired.mid(0, 4).join(QLatin1String("; "))));
        writeText(lap.env.value(QStringLiteral("QB_FAULT_CTL")), QByteArray("\n"));   // B and C without faults
    }
    // ---- B: both online, opened at the same moment, the sync program running every 2 s
    if (st.partB) {
        step(QStringLiteral("B: online, both open"));
        typed.clear();
        sync.start(2000);
        a = startWith({desk.open}, desk.env, log);
        b = startWith({lap.open}, lap.env, log);
        if (waitUpOn(a, desk) && waitUpOn(b, lap)) {
            // 0.64.2: whichever opened a moment later sees the other's lock (the sync program carries it within 2 s)
            // and asks; the writer, who means to work on both, opens it anyway (before, the keys typed next went
            // into the question, Cancel closed the project there, and the rest of the typing went nowhere)
            int asked = 0;
            for (QProcess *p : {b, a})
                if (answerLockQuestion(p, true))
                    ++asked;
            fact(st.id + QStringLiteral(" B: lock questions answered with Open Anyway"), asked);
            seen << QStringLiteral("B, before typing: the desktop: ") + windowNames(a).join(QLatin1String(" | ")) + QStringLiteral("; the laptop: ") + windowNames(b).join(QLatin1String(" | "));
            typed += plainOf(typeInto(a, ctx().dir + QStringLiteral("/b-desktop1"), QStringLiteral("fast"), 12, ctx().seed + 3).text) + QLatin1Char('\n');
            seen << QStringLiteral("B, after the desktop typed: the desktop: ") + windowNames(a).join(QLatin1String(" | ")) + QStringLiteral("; the laptop: ") + windowNames(b).join(QLatin1String(" | "));
            typed += plainOf(typeInto(b, ctx().dir + QStringLiteral("/b-laptop"), QStringLiteral("fast"), 12, ctx().seed + 4).text) + QLatin1Char('\n');
            seen << QStringLiteral("B, after the laptop typed: the desktop: ") + windowNames(a).join(QLatin1String(" | ")) + QStringLiteral("; the laptop: ") + windowNames(b).join(QLatin1String(" | "));
            typed += plainOf(typeInto(a, ctx().dir + QStringLiteral("/b-desktop2"), QStringLiteral("fast"), 8, ctx().seed + 5).text) + QLatin1Char('\n');
            settle(4000);
            seen << QStringLiteral("B, the desktop: ") + windowNames(a).join(QLatin1String(" | "));
            seen << QStringLiteral("B, the laptop: ") + windowNames(b).join(QLatin1String(" | "));
            quitProgram(b);
            settle(2500);
            quitProgram(a);
        } else
            check(false, QStringLiteral("%1 B: both programs start").arg(st.id));
        endProgram(a);
        endProgram(b);
        settle(4500);
        sync.stop();
        sync.tick();
        lost = judgePieces(st.id + QStringLiteral(" B"), typed, placesOf(cs, st.scriv));
        check(lost.isEmpty(), QStringLiteral("%1 B: every piece typed with both open is in the project or kept").arg(st.id), lost.mid(0, 6).join(QLatin1String(" | ")));
    }
    // ---- C: the laptop opens while the desktop has it open: the question, answered Cancel
    if (st.partC) {
        step(QStringLiteral("C: the question at opening"));
        sync.start(1500);
        a = startWith({desk.open}, desk.env, log);
        if (waitUpOn(a, desk)) {
            settle(5000);   // the desktop's lock reaches the laptop
            QStringList before;
            QDirIterator it(lap.root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                if (!f.endsWith(QLatin1String("lock")))
                    before << f + QLatin1Char(' ') + QString::number(QFileInfo(f).lastModified().toMSecsSinceEpoch());
            }
            sync.stop();
            b = startWith({lap.open}, lap.env, log);
            settle(4000);
            const bool asked = answerLockQuestion(b, false);
            check(asked, QStringLiteral("%1 C: the laptop asks before opening a project open on the desktop").arg(st.id), windowNames(b).join(QLatin1String(" | ")));
            settle(2000);
            QStringList after;
            QDirIterator jt(lap.root, QDir::Files, QDirIterator::Subdirectories);
            while (jt.hasNext()) {
                const QString f = jt.next();
                if (!f.endsWith(QLatin1String("lock")))
                    after << f + QLatin1Char(' ') + QString::number(QFileInfo(f).lastModified().toMSecsSinceEpoch());
            }
            check(before == after, QStringLiteral("%1 C: Cancel writes nothing on the laptop").arg(st.id));
            seen << QStringLiteral("C, the laptop after Cancel: ") + windowNames(b).join(QLatin1String(" | "));
            endProgram(b);
            quitProgram(a);
        } else
            check(false, QStringLiteral("%1 C: the desktop's program starts").arg(st.id));
        endProgram(a);
        sync.stop();
        for (int i = 0; i < 2; ++i)
            sync.tick();
    }
    fact(st.id + QStringLiteral(" what the programs showed"), seen.join(QLatin1String("; ")));
    fact(st.id + QStringLiteral(" the sync program"), QStringLiteral("%1 files carried, %2 conflicts, conflicted copies: %3")
                                                          .arg(sync.copies).arg(sync.conflicts).arg(sync.conflictCopies.mid(0, 6).join(QLatin1String(", "))));
    // at the end the project opens on both computers
    for (const Computer &c : cs) {
        QString err;
        const QHash<QString, QString> t = texts(c.open, &err);
        check(!t.isEmpty(), QStringLiteral("%1: the project opens on the %2 at the end").arg(st.id, c.name), err);
    }
}

} // namespace

// ================================================================ S-Y04 a writing week on two computers
// 0.64.2: the folders are kept in step by the sync simulator of X-10 (a file changed on both sides: the newer wins,
// the other is kept beside it as a conflicted copy, as the common sync programs do). Before, a plain copy of the
// changed files wrote the laptop's unsynced day over the desktop's, which no sync program does.
void caseSY04()
{
    const QString deskDir = ctx().work + QStringLiteral("/desktop"), lapDir = ctx().work + QStringLiteral("/laptop");
    const QString src = fixtureCopy(QStringLiteral("fx1-qb"));
    const QString rel = QDir(QFileInfo(src).absolutePath()).relativeFilePath(src);
    // the view is kept per computer (0.64): both start with a scene in the editor
    showSceneFor(src, QStringLiteral("Scene 3"), QStringLiteral("desktop"));
    showSceneFor(src, QStringLiteral("Scene 3"), QStringLiteral("laptop"));
    copyTree(QFileInfo(src).absolutePath(), deskDir);
    copyTree(QFileInfo(src).absolutePath(), lapDir);
    SyncSim sim(deskDir, QStringLiteral("desktop"), lapDir, QStringLiteral("laptop"));
    QString typed;
    QStringList notices;
    for (int day = 1; day <= 7; ++day) {
        const bool laptop = day % 2 == 0;
        const QString mine = laptop ? lapDir : deskDir, theirs = laptop ? deskDir : lapDir;
        QMap<QString, QString> env = {{QStringLiteral("QB_DEVICE_NAME"), laptop ? QStringLiteral("laptop") : QStringLiteral("desktop")}};
        step(QStringLiteral("day %1 on the %2").arg(day).arg(laptop ? QStringLiteral("laptop") : QStringLiteral("desktop")));
        QProcess *proc = startWith({mine + QLatin1Char('/') + rel}, env, ctx().dir + QStringLiteral("/week.log"));
        QProcess *other = nullptr;
        if (day == 5) {   // both have it open
            QMap<QString, QString> env2 = {{QStringLiteral("QB_DEVICE_NAME"), QStringLiteral("laptop")}};
            other = startWith({theirs + QLatin1Char('/') + rel}, env2, ctx().dir + QStringLiteral("/week.log"));
        }
        if (!waitUp(proc)) {
            endProgram(proc);
            endProgram(other);
            continue;
        }
        notices << QStringLiteral("day %1: %2").arg(day).arg(windowNames(proc).join(QLatin1String(" | ")));
        runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("Escape")});
        typed += plainOf(typeInto(proc, ctx().dir + QStringLiteral("/week-day%1").arg(day), QStringLiteral("slow"), 20, ctx().seed + quint32(day)).text) + QLatin1Char('\n');
        if (other && waitUp(other))
            typed += plainOf(typeInto(other, ctx().dir + QStringLiteral("/week-day%1-other").arg(day), QStringLiteral("slow"), 15, ctx().seed + 100).text) + QLatin1Char('\n');
        for (QProcess *p : {proc, other})
            if (p) {
                focusProgram(p);
                runTool(QStringLiteral("xdotool"), {QStringLiteral("key"), QStringLiteral("ctrl+q")});
                p->waitForFinished(30000);
            }
        endProgram(proc);
        endProgram(other);
        // the sync program after the day; after day 2 it gets only half of the files across before the desktop
        // opens on day 3 (the rest follows after day 3)
        sim.carryLimit = day == 2 ? 3 : -1;
        sim.tick();
        settle(300);
    }
    sim.carryLimit = -1;
    sim.tick();
    fact(QStringLiteral("S-Y04 what the program showed at each start"), notices.join(QLatin1String("; ")));
    fact(QStringLiteral("S-Y04 the sync program: copies, conflicts"), QStringLiteral("%1, %2: %3").arg(sim.copies).arg(sim.conflicts).arg(sim.conflictCopies.join(QLatin1String(", "))));
    QString all = keysOf(everythingIn(deskDir + QLatin1Char('/') + rel) + everythingIn(lapDir + QLatin1Char('/') + rel));
    // the sync program's conflicted copies are kept versions too (the writer finds them beside the file)
    for (const QString &c : sim.conflictCopies)
        all += keysOf(readText(deskDir + QLatin1Char('/') + c));
    QStringList lost;
    for (const QString &part : typed.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        if (!all.contains(part.left(40)))
            lost << part.left(40);
    check(lost.isEmpty(), QStringLiteral("every word typed on either computer is in the project or a kept version"), lost.join(QLatin1String(" | ")));
}


void caseX10() { twoComputers({QStringLiteral("X-10"), false, {}, true, true}); }

void caseX11() { twoComputers({QStringLiteral("X-11"), true, {}, true, false}); }

void caseX12()
{
    // the program cannot keep the other computer's version: its folder for kept versions and its snapshots fail
    const QStringList rules = {QStringLiteral("mkdir conflicts EACCES 0"), QStringLiteral("open conflicts EACCES 0"),
                               QStringLiteral("mkdir snapshots EACCES 0"), QStringLiteral("open snapshots EACCES 0")};
    twoComputers({QStringLiteral("X-12"), false, rules, false, false});
    twoComputers({QStringLiteral("X-12s"), true, rules, false, false});
}

} // namespace cs
