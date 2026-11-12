// The system test, stage 1: the functions of 0.64. S-Y01 to S-Y03: a project in a folder that a
// sync program keeps the same on two computers; S-R05: Record only, transcribed later. Use cases
// that chain the functions as a writer meets them; the single functions are checked in
// qb_v064_tests and qb_v061_tests.
#include "cs.h"

#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "import/ScrivenerFormat.h"
#include "tools/ToolApi.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"
#include "ui/ToolHost.h"

#include <QAbstractButton>
#include <QDir>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QDialog>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTextDocument>
#include <QUuid>
#include <QtEndian>

#include <cmath>
#include <random>

namespace cs {

namespace {

QMessageBox *visibleBox(const QString &name)
{
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *b = qobject_cast<QMessageBox *>(tl))
            if (b->isVisible() && b->objectName() == name)
                return b;
    return nullptr;
}

QMessageBox *waitBox(const QString &name, int ms = 3000)
{
    QMessageBox *b = nullptr;
    waitFor([&] { return (b = visibleBox(name)) != nullptr; }, ms);
    return b;
}

// closes a notice with its default button (as Enter does)
void dismiss(QMessageBox *b)
{
    if (!b)
        return;
    if (QAbstractButton *d = b->defaultButton())
        d->click();
    else
        b->accept();
    settle(150);
}

// a lock as another computer writes it (the format of 0.64)
void foreignLock(const QString &file, const QString &device, const QDateTime &alive)
{
    const QString s = device + QLatin1String(" 4242\ndevice=") + device + QLatin1String("\nuser=ann\ninstance=")
                      + QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1String("\nprogram=Quillbinder 0.64.0\nopened=")
                      + alive.addSecs(-3600).toUTC().toString(Qt::ISODate) + QLatin1String("\nalive=")
                      + alive.toUTC().toString(Qt::ISODate) + QLatin1Char('\n');
    writeText(file, s.toUtf8());
}

struct LockAnswer {
    bool asked = false;
    QString name, text, defaultButton;
};

// opens the project and answers the lock question: "cancel" (Escape), "anyway" (the Open
// button), "enter" (the default button). The question of a first opening of a .scriv is answered
// by setUpProgram's timer; this one waits for a box whose name starts with "lock".
LockAnswer openWithAnswer(MainWindow *w, const QString &path, const QString &answer, bool *opened)
{
    auto a = std::make_shared<LockAnswer>();
    auto tries = std::make_shared<int>(0);
    auto look = std::make_shared<std::function<void()>>();
    *look = [a, answer, tries, look] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible() && b->objectName().startsWith(QLatin1String("lock"))) {
                a->asked = true;
                a->name = b->objectName();
                a->text = b->text();
                a->defaultButton = b->defaultButton() ? b->defaultButton()->text().remove(QLatin1Char('&')) : QString();
                shot(b, QStringLiteral("lock-") + b->objectName() + QLatin1Char('-') + answer);
                if (answer == QLatin1String("enter"))
                    QTest::keyClick(b, Qt::Key_Return);
                else if (answer == QLatin1String("cancel"))
                    QTest::keyClick(b, Qt::Key_Escape);
                else if (auto *open = b->findChild<QPushButton *>(QStringLiteral("lockOpen")))
                    open->click();
                return;
            }
        if (++*tries < 60)
            QTimer::singleShot(150, qApp, *look);
        else
            note(QStringLiteral("(no lock question came)"));
    };
    QTimer::singleShot(300, qApp, *look);
    const bool scriv = path.endsWith(QLatin1String(".scriv"));
    *opened = scriv ? w->openScrivenerProject(path) : w->openProject(path);
    settle(600);
    *tries = 1000;   // stops a look still waiting (nothing was asked)
    return *a;
}

void typeAtEnd(MainWindow *w, const QString &title, const QString &text)
{
    if (TextEditor *e = showDoc(w, itemByTitle(w->project(), title))) {
        e->setFocus();
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
        keysInto(e, text);
    }
}

MainWindow *bareWindow()
{
    auto *w = new MainWindow;
    w->resize(1400, 950);
    w->show();
    w->activateWindow();
    (void)QTest::qWaitForWindowActive(w, 3000);
    return w;
}

QStringList otherVersions(Project *p, const QString &id)
{
    QStringList out;
    for (const Snapshot &s : snapshots::list(p, id))
        if (s.title.startsWith(QLatin1String("Other version"))) {
            QTextDocument d;
            snapshots::load(p, s, &d);
            out << d.toPlainText();
        }
    return out;
}

} // namespace

// ================================================================ S-Y01 two computers, one project
void caseSY01()
{
    qunsetenv("QB_NO_DIALOGS");   // the questions and notices of the lock are the point here
    for (const QString &fixture : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        const bool scriv = fixture.endsWith(QLatin1String("scriv"));
        const QString path = fixtureCopy(fixture);
        const QString lock = scriv ? path + QStringLiteral("/quillbinder.lock")
                                   : QFileInfo(path).absolutePath() + QStringLiteral("/settings/lock");
        MainWindow *w = bareWindow();
        bool opened = false;
        step(QStringLiteral("%1: the laptop has the project open (its lock renewed a minute ago)").arg(fixture));
        foreignLock(lock, QStringLiteral("laptop"), QDateTime::currentDateTimeUtc().addSecs(-60));
        LockAnswer a = openWithAnswer(w, path, QStringLiteral("cancel"), &opened);
        check(a.asked && a.name == QLatin1String("lockOtherDevice") && a.text.contains(QLatin1String("laptop")) && !opened && !w->project(),
              QStringLiteral("%1: opening asks first and names the other computer; Cancel leaves the project closed").arg(fixture),
              a.name + QStringLiteral(": ") + a.text);
        check(syncsafe::readLock(lock).device == QLatin1String("laptop"), QStringLiteral("%1: ... and the laptop's lock stays").arg(fixture));
        a = openWithAnswer(w, path, QStringLiteral("anyway"), &opened);
        check(opened && w->project() && syncsafe::readLock(lock).instance == syncsafe::instanceId(),
              QStringLiteral("%1: Open Anyway opens it, and the lock names this computer now").arg(fixture));
        if (!w->project()) {
            delete w;
            continue;
        }
        typeAtEnd(w, QStringLiteral("Scene 3"), QStringLiteral(" Written on this computer."));
        step(QStringLiteral("%1: the laptop opens it again while it is open here").arg(fixture));
        foreignLock(lock, QStringLiteral("laptop"), QDateTime::currentDateTimeUtc());
        QString err;
        w->project()->save(&err);
        QMessageBox *taken = waitBox(QStringLiteral("lockTakenNotice"));
        check(taken && taken->text().contains(QLatin1String("laptop")),
              QStringLiteral("%1: a notice says that the project was opened on the laptop while it is open here").arg(fixture),
              taken ? taken->text() : QString());
        if (taken) {
            shot(taken, QStringLiteral("lock-taken-") + fixture);
            fact(QStringLiteral("S-Y01 %1 default button of the notice").arg(fixture),
                 taken->defaultButton() ? taken->defaultButton()->text() : QString());
            dismiss(taken);   // Keep Working
        }
        typeAtEnd(w, QStringLiteral("Scene 3"), QStringLiteral(" And more."));
        w->project()->save(&err);
        settle(300);
        check(syncsafe::readLock(lock).device == QLatin1String("laptop") && !visibleBox(QStringLiteral("lockTakenNotice")),
              QStringLiteral("%1: Keep Working: this computer no longer renews its lock over the laptop's, and does not ask again").arg(fixture));
        const QString openPath = scriv ? w->project()->scrivenerPath : w->project()->indexPath();
        w->closeProject();
        settle(400);
        check(syncsafe::readLock(lock).device == QLatin1String("laptop"),
              QStringLiteral("%1: closing removes only this computer's own lock").arg(fixture));
        step(QStringLiteral("%1: a lock left over for an hour (the laptop was switched off)").arg(fixture));
        foreignLock(lock, QStringLiteral("laptop"), QDateTime::currentDateTimeUtc().addSecs(-3600));
        a = openWithAnswer(w, openPath, QStringLiteral("enter"), &opened);
        check(a.asked && a.name == QLatin1String("lockLeftOver") && opened && w->project(),
              QStringLiteral("%1: a lock not renewed for an hour counts as left over: Enter (Open is the default) opens").arg(fixture),
              a.name + QStringLiteral(", default: ") + a.defaultButton);
        if (w->project()) {
            BinderItem *it = itemByTitle(w->project(), QStringLiteral("Scene 3"));
            check(it && w->project()->docs()->plainText(it->id).contains(QLatin1String("Written on this computer. And more.")),
                  QStringLiteral("%1: what was written while the laptop held the lock is there").arg(fixture));
        }
        if (!scriv) {
            finishProject(w);
            check(!QFileInfo::exists(lock), QStringLiteral("%1: closing leaves no lock").arg(fixture));
        } else {
            w->closeProject();
            settle(300);
            check(!QFileInfo::exists(lock), QStringLiteral("%1: closing leaves no lock in the .scriv").arg(fixture));
        }
        checkCleanWindows(w);
        delete w;
        settle(300);
    }
}

// ================================================================ S-Y02 changed elsewhere and here
void caseSY02()
{
    qunsetenv("QB_NO_DIALOGS");   // the conflict notice is part of the case
    QString err;
    // ---- Scrivener format: the sync program writes a content.rtf while the same text is edited here
    {
        const QString scrivPath = fixtureCopy(QStringLiteral("fx1-scriv"));
        MainWindow *w = openWindow(scrivPath);
        if (Project *p = w->project()) {
            step(QStringLiteral("Scrivener format: a text changed here and in the .scriv"));
            BinderItem *it = itemByTitle(p, QStringLiteral("Scene 6"));
            typeAtEnd(w, QStringLiteral("Scene 6"), QStringLiteral(" Written here in Scrivener format."));
            p->save(&err);
            settle(300);
            QString uuid;
            {
                const QString x = readText(scrivener::scrivxIn(p->scrivenerPath));
                const int t = int(x.indexOf(QLatin1String("<Title>Scene 6</Title>")));
                const int u = t > 0 ? int(x.lastIndexOf(QLatin1String("UUID=\""), t)) : -1;
                if (u > 0)
                    uuid = x.mid(u + 6, 36);
            }
            const QString rtf = p->scrivenerPath + QStringLiteral("/Files/Data/") + uuid + QStringLiteral("/content.rtf");
            if (it && QFileInfo::exists(rtf)) {
                QTest::qWait(1100);
                writeText(rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}\\f0 The laptop wrote this version of scene six.\\par}");
                typeAtEnd(w, QStringLiteral("Scene 6"), QStringLiteral(" Second change here."));
                p->save(&err);
                settle(500);
                const QStringList others = otherVersions(p, it->id);
                check(others.join(QLatin1Char('|')).contains(QLatin1String("laptop wrote")) && readText(rtf).contains(QLatin1String("Second change here")),
                      QStringLiteral("Scrivener format: the laptop's version becomes a snapshot, this computer's text is written; nothing is lost"),
                      readText(rtf).left(300));
                dismiss(waitBox(QStringLiteral("conflictNotice"), 1500));
            } else {
                check(false, QStringLiteral("(the scene's content.rtf in the .scriv)"), rtf);
            }
            w->closeProject();
            settle(300);
        }
        checkCleanWindows(w);
        delete w;
        settle(300);
    }
    // ---- Quillbinder format
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QString conflicts = p->settingsDir() + QStringLiteral("/conflicts");
    step(QStringLiteral("a text changed here and by the sync program, twice, while the writer types"));
    BinderItem *it = itemByTitle(p, QStringLiteral("Scene 2"));
    TextEditor *e = showDoc(w, it);
    if (!e || !it)
        return;
    const QString path = p->docs()->textPath(it->id);
    e->setFocus();
    {
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
    }
    keysInto(e, QStringLiteral(" The first part typed here."));
    p->save(&err);
    QTest::qWait(1100);
    writeText(path, "The laptop's first version of scene two.\n");
    keysInto(e, QStringLiteral(" More typed here."));
    p->save(&err);
    settle(400);
    QMessageBox *n = waitBox(QStringLiteral("conflictNotice"), 1500);
    check(n && n->text().contains(QLatin1String("Scene 2")), QStringLiteral("the notice names the document and where the other version is"),
          n ? n->text() : QString());
    if (n) {
        shot(n, QStringLiteral("conflict-notice"));
        dismiss(n);
    }
    QTest::qWait(1100);
    writeText(path, "The laptop's second version of scene two.\n");
    e->setFocus();
    keysInto(e, QStringLiteral(" The last words typed here."));
    p->save(&err);
    settle(400);
    dismiss(waitBox(QStringLiteral("conflictNotice"), 1500));
    const QStringList others = otherVersions(p, it->id);
    const QStringList copies = QDir(conflicts).entryList(QDir::Files);
    QString copiesText;
    for (const QString &c : copies)
        copiesText += readText(conflicts + QLatin1Char('/') + c);
    const QString onDisk = readText(path);
    check(onDisk.contains(QLatin1String("The first part typed here.")) && onDisk.contains(QLatin1String("More typed here."))
              && onDisk.contains(QLatin1String("The last words typed here.")),
          QStringLiteral("the text typed here is saved whole"), onDisk.right(200));
    check(others.join(QLatin1Char('|')).contains(QLatin1String("first version")) && others.join(QLatin1Char('|')).contains(QLatin1String("second version"))
              && copiesText.contains(QLatin1String("first version")) && copiesText.contains(QLatin1String("second version")),
          QStringLiteral("both versions of the laptop are kept, as snapshots and as copies in settings/conflicts"),
          QStringLiteral("%1 snapshots; copies: %2").arg(others.size()).arg(copies.join(QLatin1String(", "))));
    // the project file changed elsewhere while the binder changes here
    step(QStringLiteral("the project file changed by the sync program while the binder changes here"));
    QTest::qWait(1100);
    {
        QString qbx = readText(p->indexPath());
        qbx.replace(QLatin1String(">Scene 9<"), QLatin1String(">Scene 9 renamed on the laptop<"));
        qbx.replace(QLatin1String("\"Scene 9\""), QLatin1String("\"Scene 9 renamed on the laptop\""));
        writeText(p->indexPath(), qbx.toUtf8());
    }
    if (BinderItem *s5 = itemByTitle(p, QStringLiteral("Scene 5"))) {
        s5->title = QStringLiteral("Scene 5 renamed here");
        p->notifyItemChanged(s5);
    }
    p->save(&err);
    settle(400);
    dismiss(waitBox(QStringLiteral("conflictNotice"), 1500));
    {
        const QStringList kept = QDir(conflicts).entryList({QStringLiteral("project file*")}, QDir::Files);
        check(!kept.isEmpty() && readText(conflicts + QLatin1Char('/') + kept.value(0)).contains(QLatin1String("renamed on the laptop"))
                  && readText(p->indexPath()).contains(QLatin1String("Scene 5 renamed here")),
              QStringLiteral("the project file: this computer's binder is saved, the laptop's version is kept as a copy"),
              kept.join(QLatin1String(", ")));
    }
    // a text changed only elsewhere is not overwritten here
    step(QStringLiteral("a text changed only by the sync program, shown here but not edited"));
    BinderItem *s4 = itemByTitle(p, QStringLiteral("Scene 4"));
    showDoc(w, s4);
    settle(300);
    const QString p4 = s4 ? p->docs()->textPath(s4->id) : QString();
    QTest::qWait(1100);
    writeText(p4, "Only the laptop changed scene four.\n");
    typeAtEnd(w, QStringLiteral("Scene 7"), QStringLiteral(" Something else typed."));
    p->save(&err);
    settle(300);
    const QString openPath = p->indexPath();
    w->closeProject();
    settle(300);
    check(readText(p4).contains(QLatin1String("Only the laptop changed")),
          QStringLiteral("a text changed only on the laptop is not overwritten here, not even at closing"), readText(p4).left(120));
    w->openProject(openPath);
    settle(500);
    if (w->project()) {
        BinderItem *again = itemByTitle(w->project(), QStringLiteral("Scene 4"));
        check(again && w->project()->docs()->plainText(again->id).contains(QLatin1String("Only the laptop changed")),
              QStringLiteral("... opened again, the laptop's text is shown"));
        finishProject(w);
    }
    checkCleanWindows(w);
    delete w;
}

// ================================================================ S-Y03 texts that have not arrived yet
void caseSY03()
{
    qunsetenv("QB_NO_DIALOGS");   // the notice after opening is part of the case
    const QString path = fixtureCopy(QStringLiteral("fx1-qb"));
    const QStringList titles = {QStringLiteral("Scene 30"), QStringLiteral("Scene 31"), QStringLiteral("Scene 32")};
    QStringList missing;
    QHash<QString, int> counts;
    {
        auto p = Project::open(path, nullptr);
        check(p != nullptr, QStringLiteral("(the project opens to find its files)"));
        if (!p)
            return;
        for (const QString &t : titles)
            if (BinderItem *it = itemByTitle(p.get(), t)) {
                missing << p->docs()->textPath(it->id);
                counts.insert(t, it->charCount);
            }
    }
    const QString held = ctx().work + QStringLiteral("/not-arrived");
    for (int i = 0; i < missing.size(); ++i) {
        copyTree(missing.at(i), held + QStringLiteral("/%1").arg(i));
        QFile::remove(missing.at(i));
    }
    check(missing.size() == 3, QStringLiteral("(three text files taken away)"));
    step(QStringLiteral("the project file has arrived, three of its texts not yet"));
    MainWindow *w = bareWindow();
    w->openProject(path);
    QMessageBox *n = waitBox(QStringLiteral("notArrivedNotice"), 4000);
    check(n && n->text().contains(QLatin1String("Scene 30")) && n->text().contains(QLatin1String("Scene 32")),
          QStringLiteral("a notice after opening names the texts that have not arrived"), n ? n->text() : QString());
    if (n) {
        shot(n, QStringLiteral("not-arrived"));
        dismiss(n);
    }
    Project *p = w->project();
    if (!p) {
        delete w;
        return;
    }
    if (TextEditor *e = showDoc(w, itemByTitle(p, QStringLiteral("Scene 30")))) {
        e->setFocus();
        keysInto(e, QStringLiteral("Typed into a text that has not arrived."));
        settle(200);
        check(e->isReadOnly() && !e->document()->toPlainText().contains(QLatin1String("Typed into")),
              QStringLiteral("such a text is shown read-only: typing does not change it"));
    }
    typeAtEnd(w, QStringLiteral("Scene 2"), QStringLiteral(" Work goes on while the sync program is busy."));
    QString err;
    p->save(&err);
    const QString openPath = p->indexPath();
    w->closeProject();
    settle(400);
    QStringList written;
    for (const QString &f : missing)
        if (QFileInfo::exists(f))
            written << QFileInfo(f).fileName();
    check(written.isEmpty(), QStringLiteral("saving and closing never write a text that has not arrived (no empty file over it)"),
          written.join(QLatin1String(", ")));
    {
        auto again = Project::open(openPath, nullptr);
        QStringList lost;
        for (const QString &t : titles) {
            BinderItem *it = again ? itemByTitle(again.get(), t) : nullptr;
            if (!it || it->charCount < counts.value(t))
                lost << t;
        }
        check(again && lost.isEmpty(), QStringLiteral("the project file keeps their counts (they are not counted as empty)"),
              lost.join(QLatin1String(", ")));
    }
    step(QStringLiteral("the files arrive"));
    for (int i = 0; i < missing.size(); ++i)
        copyTree(held + QStringLiteral("/%1").arg(i), missing.at(i));
    w->openProject(openPath);
    settle(600);
    check(!waitBox(QStringLiteral("notArrivedNotice"), 1200), QStringLiteral("once they are there: no notice"));
    p = w->project();
    if (p) {
        BinderItem *t30 = itemByTitle(p, QStringLiteral("Scene 30"));
        TextEditor *e2 = showDoc(w, t30);
        check(e2 && !e2->isReadOnly() && t30 && p->docs()->plainText(t30->id).size() > 1000,
              QStringLiteral("the arrived text is shown and can be edited"));
        if (e2) {
            e2->setFocus();
            QTextCursor c(e2->document());
            c.movePosition(QTextCursor::End);
            e2->setTextCursor(c);
            keysInto(e2, QStringLiteral(" Edited after it arrived."));
        }
        finishProject(w);
    }
    checkCleanWindows(w);
    delete w;
}

// ================================================================ S-R05 Record only, transcribed later
namespace {
QObject *speechObject()
{
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        if (t->id() == QLatin1String("speech-transcription"))
            return dynamic_cast<QObject *>(t);
    return nullptr;
}
bool boolOf(QObject *o, const char *slot)
{
    bool r = false;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(bool, r));
    return r;
}
QString stringOf(QObject *o, const char *slot)
{
    QString r;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(QString, r));
    return r;
}
// 16 kHz mono 16 bit: noise with a tone for speech, very quiet noise for pauses
void writeSpeechWav(const QString &file, int seconds)
{
    std::mt19937 r(11);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    QByteArray pcm;
    int ms = 0, k = 0;
    while (ms < seconds * 1000) {
        const int speak = 1200 + (k % 3) * 300, pause = (k % 4 == 3) ? 3500 : 1000;
        for (int i = 0; i < (speak + pause) * 16; ++i) {
            const float v = i < speak * 16 ? 0.35f * u(r) + 0.3f * std::sin(2 * float(M_PI) * 220.f * float(i) / 16000.f)
                                           : 0.002f * u(r);
            char b[2];
            qToLittleEndian(qint16(qBound(-32767, int(v * 32767), 32767)), b);
            pcm.append(b, 2);
        }
        ms += speak + pause;
        ++k;
    }
    QByteArray h = "RIFF";
    auto u32 = [&h](quint32 v) {
        char b[4];
        qToLittleEndian(v, b);
        h.append(b, 4);
    };
    auto u16 = [&h](quint16 v) {
        char b[2];
        qToLittleEndian(v, b);
        h.append(b, 2);
    };
    u32(quint32(36 + pcm.size()));
    h += "WAVEfmt ";
    u32(16);
    u16(1);
    u16(1);
    u32(16000);
    u32(32000);
    u16(2);
    u16(16);
    h += "data";
    u32(quint32(pcm.size()));
    writeText(file, h + pcm);
}
QString wavOf(const QString &txt) { return txt.left(txt.size() - 4) + QStringLiteral(".wav"); }
} // namespace

QObject *speechToolObject() { return speechObject(); }

void caseSR05()
{
    qputenv("QB_SPEECH_FAKE", "1");
    QObject *tool = speechObject();
    check(tool != nullptr, QStringLiteral("(the speech tool is loaded)"));
    if (!tool)
        return;
    const QString id = QStringLiteral("speech-transcription");
    const QString model = ctx().work + QStringLiteral("/fake-model.bin");
    writeText(model, QByteArray(1024, 'm'));
    toolhost::host().setSetting(id, QStringLiteral("modelFile"), model);
    toolhost::host().setSetting(id, QStringLiteral("model"), QStringLiteral("small"));
    toolhost::host().setSetting(id, QStringLiteral("mode"), QStringLiteral("record"));
    toolhost::host().setSetting(id, QStringLiteral("deleteRecording"), false);
    const QString speechWav = ctx().work + QStringLiteral("/walk.wav");
    writeSpeechWav(speechWav, 12);
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    check(boolOf(tool, "recordOnly"), QStringLiteral("Record only is on (File > Options > Speech Transcription)"));
    auto idle = [tool](int ms) { return waitFor([tool] { return !boolOf(tool, "isBusy") && !boolOf(tool, "isRecording"); }, ms); };
    step(QStringLiteral("three recordings, Record only, while typing in a scene"));
    QStringList texts;
    QString typedModel;
    for (int i = 0; i < 3; ++i) {
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, speechWav));
        texts << stringOf(tool, "recordingFile");
        if (TextEditor *e = showDoc(w, itemByTitle(w->project(), QStringLiteral("Scene 22"))))
            typedModel = typeLikeWriter(e, QLatin1Char('\n') + atParagraphs(200 + quint32(i), 60).join(QLatin1Char('\n')), 200 + quint32(i));
        idle(60000);
        settle(300);
        check(started, QStringLiteral("recording %1 starts").arg(i + 1), stringOf(tool, "lastError"));
    }
    int wavs = 0;
    for (const QString &t : texts)
        if (QFileInfo::exists(wavOf(t)) && !QFileInfo::exists(t) && QFileInfo(wavOf(t)).size() > 32000)
            ++wavs;
    check(wavs == 3, QStringLiteral("each is a recording (.wav) without text yet"), texts.join(QLatin1String(", ")));
    // the transcriptions folder as it arrives on another computer (here: another folder)
    step(QStringLiteral("Transcribe All on the other computer while typing"));
    const QString folder = QFileInfo(texts.value(0)).absolutePath();
    const QString other = ctx().work + QStringLiteral("/other-computer/Transcriptions");
    copyTree(folder, other);
    toolhost::host().setSetting(id, QStringLiteral("folder"), other);
    QObject *panel = w->findChild<QWidget *>(QStringLiteral("speechPanel"));
    int pending = -1;
    if (panel) {
        QMetaObject::invokeMethod(panel, "refresh", Qt::DirectConnection);
        QMetaObject::invokeMethod(panel, "pendingCount", Qt::DirectConnection, Q_RETURN_ARG(int, pending));
    }
    int queued = 0;
    QMetaObject::invokeMethod(tool, "transcribeAll", Qt::DirectConnection, Q_RETURN_ARG(int, queued));
    if (TextEditor *e = showDoc(w, itemByTitle(w->project(), QStringLiteral("Scene 22"))))
        typedModel = typeLikeWriter(e, QLatin1Char('\n') + atParagraphs(300, 60).join(QLatin1Char('\n')), 300);
    idle(120000);
    settle(500);
    QStringList done;
    for (const QString &t : texts) {
        const QString there = other + QLatin1Char('/') + QFileInfo(t).fileName();
        if (readText(there).contains(QLatin1String("Piece 1")) && !QFileInfo::exists(there + QStringLiteral(".part")))
            done << QFileInfo(there).fileName();
    }
    if (!panel)
        note(QStringLiteral("(the Transcriptions tab was not made yet: its count of waiting recordings is not checked)"));
    check((!panel || pending == 3) && queued == 3 && done.size() == 3,
          QStringLiteral("on the other computer the three recordings wait; Transcribe All writes their texts while the writer types"),
          QStringLiteral("pending %1, queued %2, done: %3").arg(pending).arg(queued).arg(done.join(QLatin1String(", "))));
    check(!readText(texts.value(0)).contains(QLatin1String("Piece")) && QFileInfo::exists(wavOf(texts.value(0))),
          QStringLiteral("... the first computer's folder is not touched (its recordings still wait there)"));
    step(QStringLiteral("a fourth recording; the project is closed while its text is written"));
    {
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, speechWav));
        const QString t4 = stringOf(tool, "recordingFile");
        idle(60000);
        settle(300);
        bool going = false;
        QMetaObject::invokeMethod(tool, "transcribeRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, going), Q_ARG(QString, wavOf(t4)));
        settle(100);
        const QString projectPath = w->project()->indexPath();
        const bool closed = w->closeProject();
        idle(60000);
        settle(400);
        const bool partGone = !QFileInfo::exists(t4 + QStringLiteral(".part"));
        const bool whole = QFileInfo::exists(t4) ? readText(t4).contains(QLatin1String("Piece")) : QFileInfo::exists(wavOf(t4));
        fact(QStringLiteral("S-R05 closing the project during a transcription"),
             QFileInfo::exists(t4) ? QStringLiteral("the text was finished") : QStringLiteral("the recording waits again"));
        check(started && going && closed && partGone && whole,
              QStringLiteral("closing the project during a transcription: no half text is left, the recording or its whole text is there"));
        w->openProject(projectPath);
        settle(500);
    }
    if (w->project()) {
        BinderItem *s = itemByTitle(w->project(), QStringLiteral("Scene 22"));
        check(s && rawText(w->project()->docs()->text(s->id)) == typedModel,
              QStringLiteral("the scene typed into during the recordings and transcriptions is saved whole"));
        finishProject(w);
    }
    toolhost::host().setSetting(id, QStringLiteral("mode"), QStringLiteral("live"));
    delete w;
}

// ================================================================ S-R02 recording with real recognition
namespace {
QString jfkSample()
{
    for (const QString &c : {ctx().buildDir + QStringLiteral("/tool-plugins/speech/samples/jfk.wav"), addons::addonsRoot() + QStringLiteral("/speech-transcription/samples/jfk.wav"),
                             ctx().buildDir + QStringLiteral("/whisper-src/whisper.cpp/samples/jfk.wav")})
        if (QFileInfo::exists(c))
            return c;
    return QString();
}
bool modelJobDone(QObject *tool, int ms) { return waitFor([tool] { return !boolOf(tool, "modelJobRunning"); }, ms); }
} // namespace

void caseSR02()
{
    QObject *tool = speechObject();
    const QString model = qEnvironmentVariable("QB_SPEECH_MODEL_FILE");
    const QString jfk = jfkSample();
    if (!tool || !QFileInfo::exists(model) || jfk.isEmpty()) {
        note(QStringLiteral("[S] no tool, no Small model (QB_SPEECH_MODEL_FILE) or no jfk.wav: left out"));
        return;
    }
    const QString id = QStringLiteral("speech-transcription");
    toolhost::host().setSetting(id, QStringLiteral("mode"), QStringLiteral("live"));
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    for (const int gpu : {-1, 0}) {
        toolhost::host().setSetting(id, QStringLiteral("gpu"), gpu);
        toolhost::host().setSetting(id, QStringLiteral("threads"), 1);
        QMetaObject::invokeMethod(tool, "useLocalModel", Qt::DirectConnection, Q_ARG(QString, model));
        modelJobDone(tool, 900000);
        toolhost::host().setSetting(id, QStringLiteral("hint"), QStringLiteral("Mädchen, Straße"));
        for (const QString &quality : {QStringLiteral("fast"), QStringLiteral("careful")}) {
            toolhost::host().setSetting(id, QStringLiteral("quality"), quality);
            QElapsedTimer t;
            t.start();
            bool started = false;
            QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, jfk));
            const QString file = stringOf(tool, "recordingFile");
            TextEditor *e = showDoc(w, textItems(w->project()).value(5));
            if (e && gpu == -1 && quality == QLatin1String("fast"))
                for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
                    typingGate(e, profile, 60, QStringLiteral("S-R02 while the model runs"));
            waitFor([tool] { return !boolOf(tool, "isBusy"); }, 900000);
            const QString text = readText(file);
            fact(QStringLiteral("S-R02 %1, %2: s per s of speech").arg(gpu < 0 ? QStringLiteral("processor") : QStringLiteral("Vulkan"), quality), double(t.elapsed()) / 11000.0);
            check(started && text.contains(QLatin1String("country"), Qt::CaseInsensitive), QStringLiteral("the JFK sample holds its key words (%1, %2)").arg(gpu < 0 ? QStringLiteral("processor") : QStringLiteral("Vulkan"), quality), text.left(200));
        }
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-R03 speech options and models
void caseSR03()
{
    // 0.64.1: the local test server is not the official source; its models are read after a yes (test switch)
    qputenv("QB_SPEECH_ACCEPT_CHANGED", "1");
    QObject *tool = speechObject();
    if (!tool) {
        note(QStringLiteral("[S] the speech tool is not loaded"));
        return;
    }
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList controls;
    withModal<QDialog>(500, [&controls](QDialog *d) {
        for (QWidget *x : d->findChildren<QWidget *>())
            if (x->objectName().startsWith(QLatin1String("speech")) && x->isVisible())
                controls << x->objectName();
        shot(d, QStringLiteral("speech-options"));
        d->reject();
    });
    w->toolShowOptions(QStringLiteral("speech-transcription"));
    settle(1200);
    fact(QStringLiteral("S-R03 the controls of the Speech section"), controls.join(QLatin1String(", ")));
    // a source address that does not answer
    toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("source"), QStringLiteral("other"));
    toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("otherAddress"), QStringLiteral("http://127.0.0.1:9/model.bin"));
    QElapsedTimer t;
    t.start();
    QMetaObject::invokeMethod(tool, "downloadModel", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("small")));
    modelJobDone(tool, 120000);
    fact(QStringLiteral("S-R03 a source that does not answer (ms): message"), QStringLiteral("%1: %2").arg(t.elapsed()).arg(stringOf(tool, "modelMessage")));
    check(!stringOf(tool, "modelMessage").isEmpty(), QStringLiteral("a source that does not answer gives a message"));
    toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("source"), QStringLiteral("official"));
    // bad models through Use a File I Downloaded
    const QString bad = ctx().work + QStringLiteral("/not-a-model.bin");
    writeText(bad, QByteArray("no model\n").repeated(1000));
    const QString before = toolhost::host().setting(QStringLiteral("speech-transcription"), QStringLiteral("modelFile")).toString();
    QMetaObject::invokeMethod(tool, "useLocalModel", Qt::DirectConnection, Q_ARG(QString, bad));
    modelJobDone(tool, 60000);
    check(toolhost::host().setting(QStringLiteral("speech-transcription"), QStringLiteral("modelFile")).toString() == before,
          QStringLiteral("a bad model never replaces the model in use"), stringOf(tool, "modelMessage"));
    finishProject(w);
    delete w;
}

// ================================================================ S-R04 from dictation to the manuscript
void caseSR04()
{
    qputenv("QB_SPEECH_FAKE", "1");
    QObject *tool = speechObject();
    if (!tool) {
        note(QStringLiteral("[S] the speech tool is not loaded"));
        return;
    }
    const QString id = QStringLiteral("speech-transcription");
    const QString model = ctx().work + QStringLiteral("/fake-model.bin");
    writeText(model, QByteArray(1024, 'm'));
    toolhost::host().setSetting(id, QStringLiteral("modelFile"), model);
    toolhost::host().setSetting(id, QStringLiteral("model"), QStringLiteral("small"));
    toolhost::host().setSetting(id, QStringLiteral("mode"), QStringLiteral("live"));
    const QString wav = ctx().work + QStringLiteral("/outline.wav");
    writeSpeechWav(wav, 8);
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    int steps = 0;
    bool started = false;
    QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, wav));
    ++steps;
    const QString file = stringOf(tool, "recordingFile");
    waitFor([tool] { return !boolOf(tool, "isBusy"); }, 120000);
    QString text = readText(file);
    check(started && !text.isEmpty(), QStringLiteral("the dictation is written"), text.left(120));
    // corrected in the tab (here: in the file, as the tab writes it), copied into a new scene
    text.replace(QStringLiteral("Piece"), QStringLiteral("Corrected piece"));
    writeText(file, text.toUtf8());
    ++steps;
    BinderItem *chapter = p->draft()->children.value(0) ? p->draft()->children.value(0)->children.value(0) : p->draft();
    BinderItem *scene = p->model()->createItem(chapter, -1, ItemType::Text, QStringLiteral("Dictated scene"));
    TextEditor *e = showDoc(w, scene);
    ++steps;
    if (e) {
        QTextCursor c(e->document());
        c.insertText(text.trimmed() + QStringLiteral(" She said \"yes\"."));
        ++steps;
        QTextCursor all(e->document());
        all.select(QTextCursor::Document);
        e->setTextCursor(all);
        chooseByMenu(w, QStringLiteral("style:Block Quote"));
        ++steps;
    }
    w->convertQuotes(true, QStringLiteral("en"), w);
    ++steps;
    chapter->synopsis += QStringLiteral(" See the dictated scene (qb://%1).").arg(scene->id);
    p->notifyItemChanged(chapter);
    ++steps;
    fact(QStringLiteral("S-R04 steps for U-13"), steps);
    check(docText(p, scene).contains(QLatin1String("Corrected piece")), QStringLiteral("the scene holds the corrected text"));
    check(readText(file).contains(QLatin1String("Corrected piece")), QStringLiteral("the transcription file stays as edited"));
    finishProject(w);
    delete w;
}

// ================================================================ S-A06 tool extensions of the interface 1.1
void caseSA06()
{
    QObject *tool = speechObject();
    if (!tool) {
        note(QStringLiteral("[S] the speech tool is not loaded"));
        return;
    }
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList seen;
    for (QWidget *x : w->findChildren<QWidget *>())
        if (x->objectName().startsWith(QLatin1String("inspectorTab:")) || x->objectName().contains(QLatin1String("speech"), Qt::CaseInsensitive))
            seen << x->objectName();
    fact(QStringLiteral("S-A06 the tool's places in the window"), seen.join(QLatin1String(", ")));
    check(!seen.isEmpty(), QStringLiteral("the tool's inspector tab and toolbar button are there"));
    // a layout saved with the tool's tab, then the add-on removed and the layout chosen
    w->toolShowInspectorTab(QStringLiteral("speech-transcription"));
    withModal<QDialog>(300, [](QDialog *d) {
        if (auto *le = d->findChild<QLineEdit *>())
            le->setText(QStringLiteral("With speech tab"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("layoutSave"));
    settle(400);
    QString err;
    const bool removed = addons::removeAddon(QStringLiteral("speech-transcription"), &err);
    fact(QStringLiteral("S-A06 the add-on removed while its tab is shown"), removed ? QStringLiteral("removed (restart pending)") : err);
    menuPath(w, QStringLiteral("Window > Layouts > With speech tab"));
    settle(400);
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

} // namespace cs
