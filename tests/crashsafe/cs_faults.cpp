// The system test: fault injection (C-34 to C-36, S-D06, section 3.2 of the procedure), failed conflict
// copies (C-38) and deliberate conflicts between two synchronized computers (C-39). The faults come from
// libqbfault.so (faultinject.cpp), loaded with LD_PRELOAD: a case that finds it missing runs itself again
// in a sub-run with the library loaded.
#include "cs.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/PdfExporter.h"
#include "core/Backup.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/SettingsFile.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "core/Zip.h"
#include "import/ScrivenerFormat.h"
#include "ui/AppSettings.h"
#include "ui/DragDrop.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"

#include <QLabel>
#include <QAbstractButton>
#include <QDir>
#include <QEvent>
#include <QJsonArray>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>

#include <dlfcn.h>
#include <random>

namespace cs {

using namespace qb;
using namespace qb::compile;

// ---------------------------------------------------------------- the fault library
QString faultLibrary() { return ctx().buildDir + QStringLiteral("/tests/libqbfault.so"); }

bool faultsLoaded() { return dlsym(RTLD_DEFAULT, "qbfault_reload") != nullptr; }

QMap<QString, QString> faultEnv(const QString &dir)
{
    return {{QStringLiteral("LD_PRELOAD"), faultLibrary()},
            {QStringLiteral("QB_FAULT_CTL"), dir + QStringLiteral("/faults.ctl")},
            {QStringLiteral("QB_FAULT_LOG"), dir + QStringLiteral("/faults.log")}};
}

void setFaults(const QStringList &rules)
{
    const QString ctl = qEnvironmentVariable("QB_FAULT_CTL");
    if (ctl.isEmpty())
        return;
    writeText(ctl, (rules.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    if (auto reload = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, "qbfault_reload")))
        reload();
    else
        settle(120);   // another process: it looks at the file every 50 ms
}

void clearFaults() { setFaults({}); }

QStringList firedFaults(bool reset)
{
    const QString log = qEnvironmentVariable("QB_FAULT_LOG");
    const QStringList lines = readText(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (reset)
        QFile::remove(log);
    return lines;
}

// a case that needs the library and runs without it: runs itself again with it; returns true when the
// caller must return (the sub-run did the work)
bool runUnderFaults()
{
    if (faultsLoaded())
        return false;
    if (!QFileInfo::exists(faultLibrary())) {
        check(false, QStringLiteral("(the fault library is built: %1)").arg(faultLibrary()));
        return true;
    }
    QString fails;
    const int f = subCase(QStringLiteral("faults"), faultEnv(ctx().dir + QStringLiteral("/variant-faults")), &fails);
    check(f == 0, QStringLiteral("the case under injected faults (sub-run with libqbfault.so)"), f < 0 ? QStringLiteral("did not end") : fails);
    // the sub-run's observations go into this case's report
    const QJsonObject sub = QJsonDocument::fromJson(readText(ctx().dir + QStringLiteral("/variant-faults/facts.json")).toUtf8()).object();
    for (auto it = sub.begin(); it != sub.end(); ++it)
        fact(it.key(), it.value());
    return true;
}

QString scrivUuidOf(const QString &id)
{
    const QUuid u = QUuid::fromString(id);
    if (!u.isNull())
        return u.toString(QUuid::WithoutBraces).toUpper();
    static const QUuid ns = QUuid::fromString(QStringLiteral("6b1f4a36-3c55-4b55-9a4e-5d9e3c2b7a10"));
    return QUuid::createUuidV5(ns, id).toString(QUuid::WithoutBraces).toUpper();
}

// silent comparison of two models (compareModels reports a failure itself): "" when equal
QString modelDifference(const QJsonObject &a, const QJsonObject &b)
{
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (!b.contains(it.key()))
            return QStringLiteral("missing ") + it.key();
        const QJsonObject x = it.value().toObject(), y = b.value(it.key()).toObject();
        for (auto f = x.begin(); f != x.end(); ++f)
            if (y.value(f.key()) != f.value())
                return QStringLiteral("%1: %2").arg(it.key(), f.key());
    }
    for (auto it = b.begin(); it != b.end(); ++it)
        if (!a.contains(it.key()))
            return QStringLiteral("new ") + it.key();
    return QString();
}

namespace {

QString modelDiff(const QJsonObject &a, const QJsonObject &b) { return modelDifference(a, b); }

// the key of an item without the marks the cases add to titles (" [C34-3]", " [C36]")
QString unmarked(QString key)
{
    static const QRegularExpression mark(QStringLiteral(" \\[C3[46][^\\]]*\\]"));
    return key.remove(mark);
}

// the fields on disk that are neither their old nor their new value (a third state: cut off, mixed up),
// and the items missing or come from nowhere; each field may be old or new on its own
QStringList outsideOldAndNew(const QJsonObject &before, const QJsonObject &after, const QJsonObject &disk)
{
    QHash<QString, QJsonObject> b, a;
    for (auto it = before.begin(); it != before.end(); ++it)
        b.insert(unmarked(it.key()), it.value().toObject());
    for (auto it = after.begin(); it != after.end(); ++it)
        a.insert(unmarked(it.key()), it.value().toObject());
    QStringList out;
    QSet<QString> seen;
    for (auto it = disk.begin(); it != disk.end(); ++it) {
        const QString k = unmarked(it.key());
        seen.insert(k);
        if (!b.contains(k) && !a.contains(k)) {
            out << QStringLiteral("from nowhere: ") + it.key();
            continue;
        }
        const QJsonObject d = it.value().toObject(), o = b.value(k), n = a.value(k);
        for (auto f = d.begin(); f != d.end(); ++f)
            if (f.value() != o.value(f.key()) && f.value() != n.value(f.key()))
                out << QStringLiteral("%1: %2 (old %3, new %4, now %5)")   // 0.64.2: the values too
                           .arg(it.key(), f.key(), QString::fromUtf8(QJsonDocument(QJsonArray{o.value(f.key())}).toJson(QJsonDocument::Compact)).left(60),
                                QString::fromUtf8(QJsonDocument(QJsonArray{n.value(f.key())}).toJson(QJsonDocument::Compact)).left(60),
                                QString::fromUtf8(QJsonDocument(QJsonArray{f.value()}).toJson(QJsonDocument::Compact)).left(60));
    }
    for (auto it = b.begin(); it != b.end(); ++it)
        if (!seen.contains(it.key()) && a.contains(it.key()))
            out << QStringLiteral("missing: ") + it.key();
    return out;
}

// what is on disk, read by a new project object (Scrivener format: the .scriv read afresh)
QJsonObject diskModel(const QString &path, QString *err)
{
    if (path.endsWith(QLatin1String(".scriv"))) {
        const QByteArray keep = qgetenv("XDG_DATA_HOME");
        const QString fresh = ctx().work + QStringLiteral("/fresh-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
        qputenv("XDG_DATA_HOME", fresh.toUtf8());
        auto r = scrivener::open(path, err);
        QJsonObject m = r.project ? modelOf(r.project.get(), true) : QJsonObject();
        r.project.reset();
        qputenv("XDG_DATA_HOME", keep);
        QDir(fresh).removeRecursively();
        return m;
    }
    auto p = Project::open(path, err);
    return p ? modelOf(p.get(), false) : QJsonObject();
}

std::unique_ptr<Project> openFixture(const QString &fx, QString *path)
{
    QString err;
    *path = fixtureCopy(fx);
    if (fx.endsWith(QLatin1String("scriv")))
        return std::move(scrivener::open(*path, &err).project);
    return Project::open(*path, &err);
}

// an edit that touches every kind of file: a scene's text, notes, synopsis, a title, a label
void editAll(Project *p, int n, const QString &mark)
{
    const QList<BinderItem *> docs = textItems(p);
    BinderItem *it = docs.value(n % qMax(1, int(docs.size())));
    QTextCursor c(p->docs()->text(it->id));
    c.movePosition(QTextCursor::End);
    c.insertText(QLatin1Char(' ') + mark);
    QTextCursor nn(p->docs()->notes(it->id));
    nn.movePosition(QTextCursor::End);
    nn.insertText(QLatin1Char(' ') + mark);
    it->synopsis += QLatin1Char(' ') + mark;
    it->title = it->title.section(QStringLiteral(" ["), 0, 0) + QStringLiteral(" [") + mark + QLatin1Char(']');
    if (!p->labels.isEmpty()) {
        int next = 0;
        for (int i = 0; i < p->labels.size(); ++i)
            if (p->labels.at(i).id == it->label)
                next = i + 1;
        it->label = p->labels.at(next % p->labels.size()).id;
    }
    p->notifyItemChanged(it);
}

struct Target {
    QString what;
    std::function<QString(Project *)> pattern;   // a part of the path of the file that gets the fault
};

QList<Target> targetsFor(bool scriv)
{
    QList<Target> t;
    if (!scriv) {
        t << Target{QStringLiteral("the text files"), [](Project *p) { return QFileInfo(p->docs()->textPath(textItems(p).value(1)->id)).absolutePath() + QLatin1Char('/'); }};
        t << Target{QStringLiteral("the project file"), [](Project *p) { return QFileInfo(p->indexPath()).fileName(); }};
        t << Target{QStringLiteral("the notes"), [](Project *p) { return QFileInfo(p->docs()->notesPath(textItems(p).value(1)->id)).fileName(); }};
        t << Target{QStringLiteral("every file"), [](Project *p) { return p->folderPath(); }};
    } else {
        t << Target{QStringLiteral("content.rtf in the .scriv"), [](Project *p) { return scrivUuidOf(textItems(p).value(1)->id) + QStringLiteral("/content"); }};
        t << Target{QStringLiteral("the .scrivx"), [](Project *) { return QStringLiteral(".scrivx"); }};
        t << Target{QStringLiteral("docs.checksum"), [](Project *) { return QStringLiteral("docs.checksum"); }};
        t << Target{QStringLiteral("the working copy"), [](Project *p) { return p->folderPath(); }};
        t << Target{QStringLiteral("every file of the .scriv"), [](Project *p) { return p->scrivenerPath; }};
    }
    return t;
}

const QList<QPair<QString, QString>> &kinds()
{
    static const QList<QPair<QString, QString>> k = {
        {QStringLiteral("disk full at the first byte"), QStringLiteral("write %1 ENOSPC 0")},
        {QStringLiteral("disk full after 700 bytes"), QStringLiteral("write %1 ENOSPC 700")},
        {QStringLiteral("quota exceeded"), QStringLiteral("write %1 EDQUOT 0")},
        {QStringLiteral("no permission to create"), QStringLiteral("open %1 EACCES 0")},
        {QStringLiteral("read-only file system at the rename"), QStringLiteral("rename %1 EROFS 0")},
        {QStringLiteral("an I/O error at fsync"), QStringLiteral("fsync %1 EIO 0")},
        {QStringLiteral("an I/O error at close"), QStringLiteral("close %1 EIO 0")},
        // 0.64.1: a file system that cannot sync; the program counts these as success (SafeSave.h)
        {QStringLiteral("fsync answered EINVAL"), QStringLiteral("fsync %1 EINVAL 0")},
        {QStringLiteral("fsync answered ENOTSUP (full profile)"), QStringLiteral("fsync %1 ENOTSUP 0")},
        {QStringLiteral("fsync answered ENOSYS (full profile)"), QStringLiteral("fsync %1 ENOSYS 0")},
    };
    return k;
}

int falseSuccess = 0;

// the verdict of one operation under a fault: success must be true on disk, failure must say so and leave
// the old or the new state, never a third one
void judge(const QString &what, const QString &rule, bool ok, const QString &err, const QStringList &fired, const QJsonObject &before,
           const QJsonObject &after, const QJsonObject &disk)
{
    if (fired.isEmpty()) {
        note(QStringLiteral("%1: the fault did not hit (no file matched)").arg(what));
        return;
    }
    // 0.64.1: fsync answered EINVAL, ENOTSUP or ENOSYS (a file system that cannot sync) is no error for
    // the program: the save must succeed and the disk hold the saved state
    if (rule.startsWith(QLatin1String("fsync ")) && (rule.contains(QLatin1String(" EINVAL")) || rule.contains(QLatin1String(" ENOTSUP"))
                                                   || rule.contains(QLatin1String(" ENOSYS")))) {
        check(ok, QStringLiteral("%1: the save succeeds (a file system that cannot sync is no error)").arg(what), err);
        if (ok) {
            const QString d = modelDiff(after, disk);
            check(d.isEmpty(), QStringLiteral("%1: the disk holds the saved state").arg(what), d);
        }
        return;
    }
    // an I/O error at fsync or close means the data may never reach the disk: success is false whatever
    // the page cache shows now
    if (ok && (rule.startsWith(QLatin1String("fsync ")) || rule.startsWith(QLatin1String("close ")))) {
        ++falseSuccess;
        check(false, QStringLiteral("%1: no success after an I/O error at %2").arg(what, rule.section(QLatin1Char(' '), 0, 0)),
              QStringLiteral("FALSE SUCCESS: the save reported success (faults: %1)").arg(fired.mid(0, 3).join(QLatin1String("; "))));
        return;
    }
    if (ok) {
        const QString d = modelDiff(after, disk);
        if (!d.isEmpty())
            ++falseSuccess;
        check(d.isEmpty(), QStringLiteral("%1: reported success, and the disk holds the saved state").arg(what),
              QStringLiteral("FALSE SUCCESS: %1 (faults: %2)").arg(d, fired.mid(0, 3).join(QLatin1String("; "))));
    } else {
        check(!err.isEmpty(), QStringLiteral("%1: reported failure with a message").arg(what), err);
        const QStringList third = outsideOldAndNew(before, after, disk);
        check(third.isEmpty(), QStringLiteral("%1: every field on disk is its old or its new value").arg(what), third.mid(0, 5).join(QLatin1String(", ")));
    }
}

} // namespace

// ================================================================ C-34 saving under injected faults
void caseC34()
{
    if (runUnderFaults())
        return;
    falseSuccess = 0;
    int runs = 0;
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        const bool scriv = fx.endsWith(QLatin1String("scriv"));
        for (const Target &t : targetsFor(scriv)) {
            for (const auto &k : kinds()) {
                if (ctx().quick && (k.first.contains(QLatin1String("quota")) || k.first.contains(QLatin1String("full profile"))))
                    continue;
                const QString what = QStringLiteral("%1, %2, %3").arg(scriv ? QStringLiteral("Scrivener") : QStringLiteral("Quillbinder"), t.what, k.first);
                step(what);
                QString path, err;
                auto p = openFixture(fx, &path);
                if (!p)
                    continue;
                const QJsonObject before = diskModel(path, &err);
                editAll(p.get(), runs, QStringLiteral("C34-%1").arg(runs));
                const QJsonObject after = modelOf(p.get(), scriv);
                firedFaults(true);
                const QString rule = k.second.arg(t.pattern(p.get()));
                setFaults({rule});
                err.clear();
                const bool ok = p->save(&err);
                clearFaults();
                const QStringList fired = firedFaults(true);
                QString err3;
                judge(what, rule, ok, err, fired, before, after, diskModel(path, &err3));
                // without the fault the next save works and holds everything
                QString err2;
                const bool saved2 = p->save(&err2);
                const auto diff2 = modelDiff(after, diskModel(path, &err2));   // 0.64.2: what differs is printed
                check(saved2 && diff2.isEmpty(), QStringLiteral("%1: the next save without the fault holds everything").arg(what),
                      err2 + QLatin1Char(' ') + diff2.left(600));
                ++runs;
            }
        }
    }
    fact(QStringLiteral("C-34 runs / false successes"), QStringLiteral("%1 / %2").arg(runs).arg(falseSuccess));
}

// ================================================================ C-35 backups, copies, exports under injected faults
void caseC35()
{
    if (runUnderFaults())
        return;
    QString path, err;
    auto p = openFixture(QStringLiteral("fx1-qb"), &path);
    if (!p)
        return;
    const QString dir = ctx().work + QStringLiteral("/backups");
    QDir().mkpath(dir);
    // three good backups first (keep 3)
    for (int i = 0; i < 3; ++i) {
        QString out;
        backup::backupProject(p.get(), dir, 3, &out, &err);
        settle(1100);
    }
    const QStringList good = backup::backupsFor(p.get(), dir);
    check(good.size() == 3, QStringLiteral("(three backups to begin with)"), QString::number(good.size()));
    struct Op {
        QString what;
        QString rule;
        std::function<bool(QString *, QString *)> run;   // returns ok, sets the output path
        std::function<bool(const QString &)> complete;   // the output is whole
    };
    auto zipWhole = [](const QString &f) {
        ZipReader z(f);
        if (!z.isValid())
            return false;
        const QString out = ctx().work + QStringLiteral("/unzipped-") + QFileInfo(f).completeBaseName();
        QDir(out).removeRecursively();
        if (!z.extractAll(out))
            return false;
        const QString qbx = projectFileIn(out);   // 0.64.2: not the spare copy in settings
        if (qbx.isEmpty())
            return false;
        QString e;
        return Project::open(qbx, &e) != nullptr;
    };
    Project *pp = p.get();
    const QList<Op> ops = {
        {QStringLiteral("Back Up Now, disk full in the zip"), QStringLiteral("write .zip ENOSPC 5000"), [pp, dir](QString *out, QString *e) { return backup::backupProject(pp, dir, 3, out, e); }, zipWhole},
        {QStringLiteral("Back Up Now, the zip cannot be created"), QStringLiteral("open .zip EACCES 0"), [pp, dir](QString *out, QString *e) { return backup::backupProject(pp, dir, 3, out, e); }, zipWhole},
        {QStringLiteral("Back Up Now, I/O error at close"), QStringLiteral("close .zip EIO 0"), [pp, dir](QString *out, QString *e) { return backup::backupProject(pp, dir, 3, out, e); }, zipWhole},
        {QStringLiteral("Back Up Now, an old backup cannot be removed"), QStringLiteral("unlink .zip EACCES 0"), [pp, dir](QString *out, QString *e) { return backup::backupProject(pp, dir, 3, out, e); }, zipWhole},
        {QStringLiteral("Back Up Now while the save before it fails"), QStringLiteral("write %1 ENOSPC 0").arg(QFileInfo(path).absolutePath() + QLatin1Char('/')),
         [pp, dir](QString *out, QString *e) {
             QTextCursor c(pp->docs()->text(textItems(pp).value(3)->id));
             c.movePosition(QTextCursor::End);
             c.insertText(QStringLiteral(" The latest sentence before the backup (C-35)."));
             return backup::backupProject(pp, dir, 3, out, e);
         },
         [](const QString &f) {
             ZipReader z(f);
             const QString out = ctx().work + QStringLiteral("/unzipped-latest");
             QDir(out).removeRecursively();
             if (!z.isValid() || !z.extractAll(out))
                 return false;
             const QString qbx = projectFileIn(out);   // 0.64.2: not the spare copy in settings
             if (qbx.isEmpty())
                 return false;
             QString e;
             auto q = Project::open(qbx, &e);
             if (!q)
                 return false;
             for (BinderItem *x : textItems(q.get()))
                 if (q->docs()->text(x->id)->toPlainText().contains(QLatin1String("The latest sentence before the backup")))
                     return true;
             return false;   // the backup reported success without the latest change
         }},
        {QStringLiteral("Take Snapshot, disk full"), QStringLiteral("write snapshots ENOSPC 0"), [pp](QString *out, QString *e) {
             BinderItem *it = textItems(pp).value(4);
             const bool ok = snapshots::take(pp, it->id, QStringLiteral("C-35"), e);
             const QList<Snapshot> l = snapshots::list(pp, it->id);
             *out = l.isEmpty() ? QString() : l.first().file;
             return ok;
         }, [pp](const QString &f) {
             QTextDocument d;
             const QList<Snapshot> l = snapshots::list(pp, textItems(pp).value(4)->id);
             if (l.isEmpty() || l.first().file != f)
                 return false;
             snapshots::load(pp, l.first(), &d);
             return d.toPlainText() == pp->docs()->text(textItems(pp).value(4)->id)->toPlainText();
         }},
        {QStringLiteral("Save a Copy as Scrivener Project, disk full"), QStringLiteral("write Copy.scriv ENOSPC 20000"), [pp](QString *out, QString *e) {
             *out = ctx().work + QStringLiteral("/Copy.scriv");
             QDir(*out).removeRecursively();
             scrivener::WriteReport rep;
             return scrivener::saveCopyAsScrivener(pp, *out, &rep, e);
         }, [pp](const QString &f) {
             QString e;
             const QJsonObject m = diskModel(f, &e);
             return !m.isEmpty() && m.size() == modelOf(pp, true).size();
         }},
        {QStringLiteral("Compile to PDF, disk full"), QStringLiteral("write c35.pdf ENOSPC 30000"), [pp](QString *out, QString *e) {
             *out = ctx().work + QStringLiteral("/c35.pdf");
             Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
             AuthorInfo a;
             Compiler cc(pp, f, pp->compile, a);
             PdfExporter ex;
             return ex.exportManuscript(cc.build(), *out, e);
         }, [](const QString &f) { return !pdfToText(f).isEmpty(); }},
        {QStringLiteral("Compile to Word, disk full"), QStringLiteral("write c35.docx ENOSPC 20000"), [pp](QString *out, QString *e) {
             *out = ctx().work + QStringLiteral("/c35.docx");
             Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
             AuthorInfo a;
             Compiler cc(pp, f, pp->compile, a);
             DocxExporter ex;
             return ex.exportManuscript(cc.build(), *out, e);
         }, [](const QString &f) { return !zipFile(f, QStringLiteral("word/document.xml")).isEmpty(); }},
        {QStringLiteral("Save Project Settings to File, no permission"), QStringLiteral("open c35.qbsettings EACCES 0"), [pp](QString *out, QString *e) {
             *out = ctx().work + QStringLiteral("/c35.qbsettings");
             return settingsfile::save(*out, pp, e);
         }, [](const QString &f) { QJsonObject d; QString e; return settingsfile::load(f, &d, &e); }},
        {QStringLiteral("Export Files, disk full"), QStringLiteral("write export-c35 ENOSPC 0"), [pp](QString *out, QString *e) {
             dnd::ExportOptions eo;
             const QString d = ctx().work + QStringLiteral("/export-c35");
             QDir().mkpath(d);
             *out = dnd::exportItem(pp, textItems(pp).value(2), d, eo);
             Q_UNUSED(e)
             return !out->isEmpty();
         }, [pp](const QString &f) {
             // the same export without the fault, to compare with
             dnd::ExportOptions eo;
             const QString d = ctx().work + QStringLiteral("/export-c35-reference");
             QDir(d).removeRecursively();
             QDir().mkpath(d);
             const QString ref = dnd::exportItem(pp, textItems(pp).value(2), d, eo);
             return QFileInfo(f).size() > 0 && QFileInfo(f).size() == QFileInfo(ref).size();
         }},
        {QStringLiteral("the objects, disk full"), QStringLiteral("write objects ENOSPC 0"), [pp](QString *out, QString *e) {
             objects::Store *st = pp->objects();
             const QString cat = st->schema().categories.value(0).id;
             const QString id = st->create(cat, QString(), QStringLiteral("C-35 object"));
             *out = id;
             return st->save(e);
         }, [pp](const QString &id) {
             objects::Store again(pp->folderPath());
             QString e;
             again.load(&e);
             return again.object(id) != nullptr;
         }},
        {QStringLiteral("the program's settings, disk full"), QStringLiteral("write Quillbinder.conf ENOSPC 0"), [](QString *out, QString *) {
             AppSettings &as = AppSettings::instance();
             as.backupKeep = 7;
             as.save();
             QSettings s;
             *out = s.fileName();
             return s.status() == QSettings::NoError;   // the program cannot tell: save() returns nothing
         }, [](const QString &f) { return readText(f).contains(QLatin1String("keep=7")); }},
    };
    int falses = 0;
    for (const Op &op : ops) {
        step(op.what);
        const QStringList before = backup::backupsFor(p.get(), dir);
        firedFaults(true);
        setFaults({op.rule});
        QString out, e;
        const bool ok = op.run(&out, &e);
        clearFaults();
        const QStringList fired = firedFaults(true);
        const QStringList after = backup::backupsFor(p.get(), dir);
        if (fired.isEmpty()) {
            note(QStringLiteral("%1: the fault did not hit").arg(op.what));
            continue;
        }
        if (op.what.startsWith(QLatin1String("the program's settings"))) {
            // AppSettings::save() returns nothing: the writer can only be told by the window (S-D06)
            fact(QStringLiteral("C-35 %1").arg(op.what), op.complete(out) ? QStringLiteral("written after all") : QStringLiteral("not written, and the function cannot say so"));
            continue;
        }
        if (ok) {
            const bool whole = op.complete(out);
            if (!whole)
                ++falses;
            check(whole, QStringLiteral("%1: reported success, and the result is whole").arg(op.what), QStringLiteral("FALSE SUCCESS: %1").arg(out));
        } else {
            // Export Files has no message of its own (an empty path is its answer)
            if (!op.what.startsWith(QLatin1String("Export Files")))
                check(!e.isEmpty(), QStringLiteral("%1: reported failure with a message").arg(op.what), e);
            if (!out.isEmpty() && QFileInfo::exists(out) && !op.what.contains(QLatin1String("Snapshot")))
                fact(QStringLiteral("C-35 %1: a file was left under the final name").arg(op.what), out);
        }
        if (op.what.startsWith(QLatin1String("Back Up Now")) && !ok)
            check(after.size() >= before.size(), QStringLiteral("%1: no older backup was removed for a backup that failed").arg(op.what),
                  QStringLiteral("%1 before, %2 after").arg(before.size()).arg(after.size()));
        // 0.64.1: backup::backupProject stops when its save fails, so no new zip
        if (op.what == QLatin1String("Back Up Now while the save before it fails")) {
            check(!ok, QStringLiteral("%1: the backup reports failure (0.64.1)").arg(op.what), out);
            QStringList added = after;
            for (const QString &b : before)
                added.removeAll(b);
            check(added.isEmpty(), QStringLiteral("%1: no new backup is made").arg(op.what), added.join(QLatin1String(", ")));
        }
    }
    fact(QStringLiteral("C-35 false successes"), falses);
}

// ================================================================ C-36 interrupted writes
void caseC36()
{
    const QString v = variant();
    if (v.startsWith(QLatin1String("kill:"))) {
        // the sub-run: open the prepared copy, edit, write the models, save with the kill rule
        const QStringList a = v.split(QLatin1Char(':'));
        const QString path = qEnvironmentVariable("QB_CS_PROJECT");
        QString err;
        std::unique_ptr<Project> p = path.endsWith(QLatin1String(".scriv")) ? std::move(scrivener::open(path, &err).project) : Project::open(path, &err);
        if (!p)
            return;
        editAll(p.get(), 3, QStringLiteral("C36"));
        writeText(qEnvironmentVariable("QB_CS_AFTER"), QJsonDocument(modelOf(p.get(), path.endsWith(QLatin1String(".scriv")))).toJson());
        QString pattern = a.value(1);
        if (pattern == QLatin1String("text"))
            pattern = QFileInfo(p->docs()->textPath(textItems(p.get()).value(3)->id)).absolutePath() + QLatin1Char('/');
        else if (pattern == QLatin1String("content"))
            pattern = scrivUuidOf(textItems(p.get()).value(3)->id) + QStringLiteral("/content");
        setFaults({QStringLiteral("kill %1 0 %2").arg(pattern, a.value(2))});
        p->save(&err);
        clearFaults();
        note(QStringLiteral("the save ended without the kill (the rule did not hit)"));
        return;
    }
    if (!QFileInfo::exists(faultLibrary())) {
        check(false, QStringLiteral("(the fault library is built)"));
        return;
    }
    struct Point {
        QString fx, pattern;
        QList<int> bytes;
    };
    const QList<Point> points = {{QStringLiteral("fx1-qb"), QStringLiteral("text"), {0, 1, 300}},
                                 {QStringLiteral("fx1-qb"), QStringLiteral(".qbx"), {0, 2000, 20000}},
                                 {QStringLiteral("fx1-scriv"), QStringLiteral("content"), {0, 200}},
                                 {QStringLiteral("fx1-scriv"), QStringLiteral(".scrivx"), {0, 5000, 50000}},
                                 {QStringLiteral("fx1-scriv"), QStringLiteral("docs.checksum"), {0, 10}}};
    int n = 0, opened = 0;
    for (const Point &pt : points) {
        for (int b : pt.bytes) {
            ++n;
            const QString path = fixtureCopy(pt.fx);
            QString err;
            const QJsonObject before = diskModel(path, &err);
            const QString afterFile = ctx().dir + QStringLiteral("/after-%1.json").arg(n);
            QMap<QString, QString> env = faultEnv(ctx().dir + QStringLiteral("/variant-kill:%1:%2").arg(pt.pattern).arg(b));
            env.insert(QStringLiteral("QB_CS_PROJECT"), path);
            env.insert(QStringLiteral("QB_CS_AFTER"), afterFile);
            QString fails;
            subCase(QStringLiteral("kill:%1:%2").arg(pt.pattern).arg(b), env, &fails);
            const QJsonObject after = QJsonDocument::fromJson(readText(afterFile).toUtf8()).object();
            const QJsonObject disk = diskModel(path, &err);
            const QString what = QStringLiteral("%1, killed after %2 bytes of %3").arg(pt.fx).arg(b).arg(pt.pattern);
            check(!disk.isEmpty(), QStringLiteral("%1: the project opens").arg(what), err);
            if (disk.isEmpty())
                continue;
            ++opened;
            // each field of each item: its old or its new value
            const QStringList mixed = outsideOldAndNew(before, after, disk);
            check(mixed.isEmpty(), QStringLiteral("%1: every field is its old or its new value").arg(what), mixed.mid(0, 5).join(QLatin1String(", ")));
            // what the kill left behind
            int temp = 0;
            const QString folder = pt.fx.endsWith(QLatin1String("scriv")) ? path : QFileInfo(path).absolutePath();
            QDirIterator di(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (di.hasNext()) {
                const QString f = di.next();
                if (QRegularExpression(QStringLiteral("\\.[A-Za-z0-9]{6}$")).match(f).hasMatch())
                    ++temp;
            }
            fact(QStringLiteral("C-36 %1: temporary files left").arg(what), temp);
        }
    }
    fact(QStringLiteral("C-36 kills / projects that open"), QStringLiteral("%1 / %2").arg(n).arg(opened));
}

// ================================================================ C-38 conflict copies that cannot be made
void caseC38()
{
    if (runUnderFaults())
        return;
    struct Variant {
        QString what;
        QStringList rules;
    };
    const QList<Variant> variants = {
        {QStringLiteral("the conflicts folder cannot be made"), {QStringLiteral("mkdir conflicts EACCES 0"), QStringLiteral("open conflicts EACCES 0")}},
        {QStringLiteral("the copy runs out of space"), {QStringLiteral("write conflicts ENOSPC 100")}},
        {QStringLiteral("the snapshot cannot be written"), {QStringLiteral("open snapshots ENOSPC 0"), QStringLiteral("write snapshots ENOSPC 0")}},
        {QStringLiteral("neither copy nor snapshot"), {QStringLiteral("open conflicts EACCES 0"), QStringLiteral("mkdir conflicts EACCES 0"), QStringLiteral("open snapshots EACCES 0"),
                                                       QStringLiteral("mkdir snapshots EACCES 0")}},
    };
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        const bool scriv = fx.endsWith(QLatin1String("scriv"));
        for (const Variant &v : variants) {
            const QString what = QStringLiteral("%1: %2").arg(scriv ? QStringLiteral("Scrivener") : QStringLiteral("Quillbinder"), v.what);
            step(what);
            QString path, err;
            auto p = openFixture(fx, &path);
            if (!p)
                continue;
            BinderItem *it = textItems(p.get()).value(6);
            p->docs()->text(it->id);
            // the other computer's version arrives through the sync program
            QString file = scriv ? QString() : p->docs()->textPath(it->id);
            const QString theirs = QStringLiteral("Written on the laptop (C-38).");
            if (scriv) {
                file = scriv ? p->scrivenerPath + QStringLiteral("/Files/Data/") + scrivUuidOf(it->id) + QStringLiteral("/content.rtf") : file;
                QByteArray b = readText(file).toUtf8();
                b.insert(qMax(0, int(b.lastIndexOf('}'))), QByteArray("\\par ") + theirs.toUtf8());
                writeText(file, b);
            } else {
                writeText(file, readText(file).toUtf8() + "\n" + theirs.toUtf8() + "\n");
            }
            int notices = 0;
            QObject::connect(p.get(), &Project::conflictKept, [&notices](const QString &, const QString &, const QString &, bool) { ++notices; });
            QTextCursor c(p->docs()->text(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Written here (C-38)."));
            firedFaults(true);
            setFaults(v.rules);
            const bool ok = p->save(&err);
            clearFaults();
            const QStringList fired = firedFaults(true);
            // is the laptop's text anywhere: its file, a kept copy, a snapshot?
            QString everywhere = readText(file);
            for (const QString &sub : {QStringLiteral("/settings/conflicts"), QStringLiteral("/snapshots")}) {
                QDirIterator di(p->folderPath() + sub, QDir::Files, QDirIterator::Subdirectories);
                while (di.hasNext())
                    everywhere += readText(di.next());
            }
            fact(QStringLiteral("C-38 %1").arg(what), QStringLiteral("save %1 (%2); notices %3; faults hit %4").arg(ok ? QStringLiteral("reported success") : QStringLiteral("refused")).arg(err.left(120)).arg(notices).arg(fired.size()));
            if (fired.isEmpty()) {
                note(QStringLiteral("%1: the fault did not hit").arg(what));
                continue;
            }
            check(everywhere.contains(QStringLiteral("Written on the laptop")), QStringLiteral("%1: the laptop's version is not lost (its file, a copy or a snapshot)").arg(what));
            check(!ok || notices > 0, QStringLiteral("%1: a save that went on told the writer what was kept").arg(what));
            // 0.64.1 ("nothing is replaced until the other version is kept"): with neither a copy nor a
            // snapshot the save fails, names the file, leaves the laptop's file alone and this change unsaved
            if (v.what == QLatin1String("neither copy nor snapshot")) {
                check(!ok, QStringLiteral("%1: the save fails (0.64.1)").arg(what));
                check(!err.isEmpty(), QStringLiteral("%1: the message says why").arg(what), err.left(200));
                const QString now = readText(file);
                check(now.contains(QLatin1String("Written on the laptop")) && !now.contains(QLatin1String("Written here (C-38)")),
                      QStringLiteral("%1: the laptop's file is not written over").arg(what), now.right(200));
                // Scrivener format: the change is kept in the program's working copy (only the .scriv is not written)
                if (!scriv)
                    check(p->hasUnsavedChanges(), QStringLiteral("%1: this computer's change stays unsaved").arg(what));
                else
                    fact(QStringLiteral("C-38 %1: unsaved changes after the refused save").arg(what), p->hasUnsavedChanges());
                QString err2;
                const bool ok2 = p->save(&err2);
                QString kept = readText(file);
                for (const QString &sub : {QStringLiteral("/settings/conflicts"), QStringLiteral("/snapshots")}) {
                    QDirIterator di(p->folderPath() + sub, QDir::Files, QDirIterator::Subdirectories);
                    while (di.hasNext())
                        kept += readText(di.next());
                }
                check(ok2 && kept.contains(QLatin1String("Written on the laptop")) && readText(file).contains(QLatin1String("Written here (C-38)")),
                      QStringLiteral("%1: without the faults the next save keeps the laptop's version and writes this one").arg(what), err2.left(200));
            }
        }
    }
}

// ================================================================ C-39 deliberate conflicts between two synchronized computers
namespace {
QDateTime syncFiles(const QString &from, const QString &to, const QDateTime &since)
{
    const QDateTime now = QDateTime::currentDateTime();
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(from).relativeFilePath(f);
        if (rel.endsWith(QLatin1String("lock")) || QFileInfo(f).lastModified() < since)
            continue;
        const QString t = to + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(t).absolutePath());
        QFile::remove(t);
        QFile::copy(f, t);
    }
    // deletions: files of `to` that `from` no longer has, among the texts
    QDirIterator jt(to, QDir::Files, QDirIterator::Subdirectories);
    while (jt.hasNext()) {
        const QString t = jt.next();
        const QString rel = QDir(to).relativeFilePath(t);
        if (rel.startsWith(QLatin1String("content/")) && !QFileInfo::exists(from + QLatin1Char('/') + rel))
            QFile::remove(t);
    }
    return now;
}

QString allText(const QString &folder, Project *p)
{
    QString s;
    if (p)
        for (BinderItem *it : p->allItems()) {
            s += it->title + QLatin1Char('\n') + it->synopsis + QLatin1Char('\n');
            if (it->hasText())
                s += p->docs()->text(it->id)->toPlainText() + QLatin1Char('\n') + p->docs()->notes(it->id)->toPlainText() + QLatin1Char('\n');
        }
    for (const QString &sub : {QStringLiteral("/settings/conflicts"), QStringLiteral("/snapshots")}) {
        QDirIterator di(folder + sub, QDir::Files, QDirIterator::Subdirectories);
        while (di.hasNext())
            s += readText(di.next());
    }
    return s;
}
} // namespace

void caseC39()
{
    struct Scenario {
        QString what;
        std::function<void(Project *, const QString &)> onDesktop, onLaptop;
    };
    auto appendText = [](int n) {
        return [n](Project *p, const QString &mark) {
            QTextCursor c(p->docs()->text(textItems(p).value(n)->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QLatin1Char(' ') + mark);
        };
    };
    const QList<Scenario> scenarios = {
        {QStringLiteral("the same scene typed into on both"), appendText(5), appendText(5)},
        {QStringLiteral("the same title renamed differently"), [](Project *p, const QString &m) { BinderItem *it = textItems(p).value(6); it->title = m; p->notifyItemChanged(it); },
         [](Project *p, const QString &m) { BinderItem *it = textItems(p).value(6); it->title = m; p->notifyItemChanged(it); }},
        {QStringLiteral("a scene deleted on one, edited on the other"), [](Project *p, const QString &) { p->model()->moveToTrash({textItems(p).value(7)}); p->model()->emptyTrash(); },
         appendText(7)},
        // 0.64.2: the move carries the mark in the title (a move alone has no text the check could find)
        {QStringLiteral("a scene moved on one, edited on the other"), [](Project *p, const QString &m) { BinderItem *it = textItems(p).value(8); it->title += QLatin1Char(' ') + m; p->notifyItemChanged(it); p->model()->moveItems({it}, p->draft()->children.value(2), 0); },
         appendText(8)},
        {QStringLiteral("new documents on both"), [](Project *p, const QString &m) { BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, m); fillText(p, it, {m}); },
         [](Project *p, const QString &m) { BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, m); fillText(p, it, {m}); }},
        {QStringLiteral("notes and synopsis on both"), [](Project *p, const QString &m) { BinderItem *it = textItems(p).value(9); it->synopsis += m; QTextCursor(p->docs()->notes(it->id)).insertText(m); p->notifyItemChanged(it); },
         [](Project *p, const QString &m) { BinderItem *it = textItems(p).value(9); it->synopsis += m; QTextCursor(p->docs()->notes(it->id)).insertText(m); p->notifyItemChanged(it); }},
        {QStringLiteral("label and status on both"), [](Project *p, const QString &) { BinderItem *it = textItems(p).value(10); it->label = 1; it->status = 1; p->notifyItemChanged(it); },
         [](Project *p, const QString &) { BinderItem *it = textItems(p).value(10); it->label = 2; it->status = 2; p->notifyItemChanged(it); }},
    };
    int n = 0;
    for (const Scenario &sc : scenarios) {
        ++n;
        step(sc.what);
        const QString src = fixtureCopy(QStringLiteral("fx1-qb"));
        const QString srcFolder = QFileInfo(src).absolutePath();
        const QString rel = QFileInfo(src).fileName();
        const QString desk = ctx().work + QStringLiteral("/c39-%1-desktop").arg(n), lap = ctx().work + QStringLiteral("/c39-%1-laptop").arg(n);
        QDir(desk).removeRecursively();
        QDir(lap).removeRecursively();
        copyTree(srcFolder, desk);
        copyTree(srcFolder, lap);
        QString err;
        // both open (the writer forgot to close one), both edit, the desktop saves first
        qputenv("QB_DEVICE_NAME", "desktop");
        auto a = Project::open(desk + QLatin1Char('/') + rel, &err);
        qputenv("QB_DEVICE_NAME", "laptop");
        auto b = Project::open(lap + QLatin1Char('/') + rel, &err);
        if (!a || !b)
            continue;
        int notices = 0;
        QObject::connect(b.get(), &Project::conflictKept, [&notices](const QString &, const QString &, const QString &, bool) { ++notices; });
        const QString ma = QStringLiteral("DESK%1").arg(n), mb = QStringLiteral("LAP%1").arg(n);
        // the texts are read before the other side's files arrive
        for (BinderItem *it : textItems(b.get()))
            b->docs()->text(it->id);
        sc.onDesktop(a.get(), ma);
        sc.onLaptop(b.get(), mb);
        qputenv("QB_DEVICE_NAME", "desktop");
        const QDateTime t0 = QDateTime::currentDateTime().addSecs(-2);
        a->save(&err);
        settle(1100);
        syncFiles(desk, lap, t0);
        qputenv("QB_DEVICE_NAME", "laptop");
        const bool ok = b->save(&err);
        const QDateTime t1 = QDateTime::currentDateTime().addSecs(-2);
        settle(1100);
        syncFiles(lap, desk, t1);
        b.reset();
        a.reset();
        qunsetenv("QB_DEVICE_NAME");
        auto final = Project::open(desk + QLatin1Char('/') + rel, &err);
        const QString inProject = allText(QString(), final.get());
        const QString all = allText(desk, final.get()) + allText(lap, nullptr);
        auto where = [&](const QString &m) {
            return inProject.contains(m) ? QStringLiteral("in the project") : all.contains(m) ? QStringLiteral("only in a kept version") : QStringLiteral("LOST");
        };
        fact(QStringLiteral("C-39 %1").arg(sc.what), QStringLiteral("laptop's save %1, notices %2; the desktop's change %3, the laptop's change %4")
                                                          .arg(ok ? QStringLiteral("ok") : err.left(80)).arg(notices).arg(where(ma), where(mb)));
        check(final != nullptr, QStringLiteral("%1: the project opens after both computers saved").arg(sc.what), err);
        if (sc.what.contains(QLatin1String("deleted")) || sc.what.contains(QLatin1String("label")))
            continue;   // recorded: which of the two wins
        check(all.contains(ma) && all.contains(mb), QStringLiteral("%1: both versions are in the project or kept").arg(sc.what));
    }
    // the sync program's own conflict copies in the folder
    const QString src = fixtureCopy(QStringLiteral("fx1-qb"));
    const QString folder = QFileInfo(src).absolutePath();
    QString err;
    auto p = Project::open(src, &err);
    const QString text = p ? p->docs()->textPath(textItems(p.get()).value(2)->id) : QString();
    p.reset();
    const QStringList strays = {QFileInfo(text).absolutePath() + QStringLiteral("/text (laptop's conflicted copy 2026-11-09).md"),
                                QFileInfo(src).absolutePath() + QLatin1Char('/') + QFileInfo(src).completeBaseName() + QStringLiteral("-LAPTOP.qbx"),
                                QFileInfo(text).absolutePath() + QStringLiteral("/text-PC.md")};
    for (const QString &s : strays)
        writeText(s, "A copy made by the sync program.\n");
    auto q = Project::open(src, &err);
    check(q != nullptr, QStringLiteral("the project opens with the sync program's conflict copies in its folder"), err);
    if (q) {
        int listed = 0;
        for (BinderItem *it : q->allItems())
            if (it->title.contains(QLatin1String("conflicted")) || it->title.contains(QLatin1String("-PC")))
                ++listed;
        fact(QStringLiteral("C-39 the sync program's copies listed as documents"), listed);
        q->save(&err);
        q.reset();
    }
    int left = 0;
    for (const QString &s : strays)
        left += QFileInfo::exists(s) ? 1 : 0;
    check(left == strays.size(), QStringLiteral("the sync program's copies are not deleted"), QString::number(left));
    Q_UNUSED(folder)
}

// ================================================================ S-D06 faults through the user interface
namespace {
// 0.64.2: every message box shown while a command runs (a box that the timed answer below did not catch, because
// the command's menu took longer than its tries: the Scrivener variant's File > Save told the writer, and the
// case counted it as silent)
struct BoxWatch : QObject {
    QStringList texts;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Show)
            if (auto *b = qobject_cast<QMessageBox *>(o))
                texts << b->text();
        return false;
    }
};
} // namespace

void caseSD06()
{
    if (runUnderFaults())
        return;
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        const bool scriv = fx.endsWith(QLatin1String("scriv"));
        const QString name = scriv ? QStringLiteral("Scrivener") : QStringLiteral("Quillbinder");
        const QString path = fixtureCopy(fx);
        MainWindow *w = openWindow(path);
        Project *p = w->project();
        if (!p) {
            delete w;
            continue;
        }
        int failedSignals = 0;
        QObject::connect(p, &Project::saveFailed, [&failedSignals](const QString &) { ++failedSignals; });
        // where the project is written: the folder, or the .scriv (the working copy is the program's own)
        const QString folder = scriv ? p->scrivenerPath : p->folderPath();
        struct Cmd {
            QString what, action, rule;
        };
        const QList<Cmd> cmds = {
            {QStringLiteral("File > Save, disk full"), QStringLiteral("save"), QStringLiteral("write %1 ENOSPC 0").arg(folder)},
            {QStringLiteral("File > Back Up > Back Up Now, disk full"), QStringLiteral("backupNow"), QStringLiteral("write .zip ENOSPC 0")},
            {QStringLiteral("Documents > Snapshots > Take Snapshot, no permission"), QStringLiteral("snapshot"), QStringLiteral("open snapshots EACCES 0")},
            {QStringLiteral("File > Save, read-only at the rename"), QStringLiteral("save"), QStringLiteral("rename %1 EROFS 0").arg(folder)},
            {QStringLiteral("File > Save, an I/O error at fsync"), QStringLiteral("save"), QStringLiteral("fsync %1 EIO 0").arg(folder)},
        };
        BinderItem *item = textItems(p).value(3);
        const QString title = item ? item->title : QString();
        TextEditor *e = showDoc(w, item);
        QStringList rows;
        for (const Cmd &c : cmds) {
            if (e) {
                QTextCursor cur = e->textCursor();
                cur.movePosition(QTextCursor::End);
                e->setTextCursor(cur);
                e->setFocus(Qt::OtherFocusReason);
                typeInto(e, QStringLiteral(" before %1.").arg(c.action));
            }
            firedFaults(true);
            setFaults({c.rule});
            QString shown;
            withModal<QMessageBox>(600, [&shown](QMessageBox *b) {
                shown = b->text();
                b->accept();
            }, 20);
            const int signalsBefore = failedSignals;
            BoxWatch watch;
            qApp->installEventFilter(&watch);
            chooseByMenu(w, c.action);
            settle(1500);
            qApp->removeEventFilter(&watch);
            if (shown.isEmpty())
                shown = watch.texts.join(QLatin1String(" / "));
            clearFaults();
            const QStringList fired = firedFaults(true);
            const QString status = w->statusBar() ? w->statusBar()->currentMessage() : QString();
            const bool told = !shown.isEmpty() || failedSignals > signalsBefore || status.contains(QLatin1String("could not"), Qt::CaseInsensitive);
            rows << QStringLiteral("%1: faults hit %2; message \"%3\"; status \"%4\"; save-failed signals %5").arg(c.what).arg(fired.size()).arg(shown.left(100), status.left(100)).arg(failedSignals - signalsBefore);
            if (!fired.isEmpty())   // 0.64.2: where the faults hit
                check(told, QStringLiteral("%1, %2: the writer is told that it failed").arg(name, c.what), status + QStringLiteral(" | ") + fired.mid(0, 6).join(QLatin1String(" ; ")));
            check(fired.isEmpty() || !status.contains(QLatin1String("saved"), Qt::CaseInsensitive) || told, QStringLiteral("%1, %2: no success shown").arg(name, c.what), status);
        }
        // typing while every save fails (autosave), then the fault goes
        if (e) {
            setFaults({QStringLiteral("write %1 ENOSPC 0").arg(folder)});
            QString shown;
            withModal<QMessageBox>(500, [&shown](QMessageBox *b) {
                shown = b->text();
                b->accept();
            }, 60);
            const int signalsBefore = failedSignals;
            typeInto(e, QStringLiteral(" Typed while the disk is full."));
            settle(8000);   // the autosave (2 s pause, 5 s gap rule)
            // 0.70.0 (audit A S-01): after the first notice, a failure within 5 minutes is told by the sign in the status
            // bar ("Not saved: the last save failed at ..."), and the window's own retry (every 15 s) may try before the autosave does, without
            // the project's signal: the sign counts as telling (2026-11-12, the full run: it went either way)
            auto *sign = w->findChild<QLabel *>(QStringLiteral("saveState"));
            const QString signText = sign ? sign->text() : QString();
            clearFaults();
            const QStringList fired = firedFaults(true);
            const bool hit = !fired.isEmpty();
            rows << QStringLiteral("autosave on a full disk: message \"%1\", save-failed signals %2, sign \"%3\"")
                        .arg(shown.left(120)).arg(failedSignals - signalsBefore).arg(signText.left(60));
            if (hit)
                check(!shown.isEmpty() || failedSignals > signalsBefore || signText.startsWith(QLatin1String("Not saved:")),
                      QStringLiteral("%1: an autosave that failed is reported").arg(name), fired.mid(0, 6).join(QLatin1String(" ; ")));
        }
        fact(QStringLiteral("S-D06 %1: what the writer saw").arg(name), rows.join(QLatin1String("; ")));
        // closing with a save that fails: the program must not close silently
        setFaults({QStringLiteral("write %1 ENOSPC 0").arg(folder)});
        QString asked;
        withModal<QMessageBox>(500, [&asked](QMessageBox *b) {
            asked = b->text();
            if (QAbstractButton *no = b->button(QMessageBox::No))
                no->click();
            else
                b->reject();
        }, 20);
        const bool closed = w->closeProject();
        settle(500);
        clearFaults();
        const bool hit = !firedFaults(true).isEmpty();
        fact(QStringLiteral("S-D06 %1: Close Project while saves fail").arg(name), closed ? QStringLiteral("closed (%1)").arg(asked.left(100)) : QStringLiteral("stays open (%1)").arg(asked.left(100)));
        if (hit)
            check(!closed || !asked.isEmpty(), QStringLiteral("%1: Close Project with a save that fails asks first").arg(name));
        if (w->project())
            finishProject(w);
        delete w;
        // without the fault everything typed is there
        QString err, home;
        std::unique_ptr<Project> q;
        if (scriv) {
            const QByteArray keep = qgetenv("XDG_DATA_HOME");
            home = ctx().work + QStringLiteral("/sd06-afresh");
            qputenv("XDG_DATA_HOME", home.toUtf8());
            q = std::move(scrivener::open(path, &err).project);
            qputenv("XDG_DATA_HOME", keep);
        } else
            q = Project::open(path, &err);
        BinderItem *again = q ? itemByTitle(q.get(), title) : nullptr;
        const QString text = again ? q->docs()->text(again->id)->toPlainText() : QString();
        check(text.contains(QLatin1String("Typed while the disk is full.")) && text.contains(QLatin1String("before save.")),
              QStringLiteral("%1: once the faults are gone, everything typed is saved").arg(name), text.right(160));
        q.reset();
        if (!home.isEmpty())
            QDir(home).removeRecursively();
    }
}

} // namespace cs
