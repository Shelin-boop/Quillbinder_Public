// The system test: the cases of 0.64.1 (the external safety review of 2026-11-09). Specification: the project
// document claude/quillbinder-test-procedure-crashsafe.md, cases C-40, C-41, C-42, S-P09, S-I12, S-L08,
// S-A07, U-20 (revision of 2026-11-09 for 0.64.1).
#include "cs.h"

#include "AppInfo.h"

#include "core/Addons.h"
#include "core/PathSafety.h"
#include "core/ProjectTemplates.h"
#include "core/SyncSafety.h"
#include "core/Zip.h"
#include "import/FileImporter.h"
#include "import/ScrivenerFormat.h"
#include "ui/AddonDialogs.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/LinkOpen.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/TextEditor.h"

#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTest>
#include <QTextCursor>
#include <QtEndian>

#include <sys/stat.h>
#include <unistd.h>

namespace cs {

namespace {

// a folder's files as "path size sha1" lines: the guard beside the project must stay as it was
QString treeOf(const QString &dir)
{
    QStringList lines;
    QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QFileInfo fi(f);
        QByteArray sum;
        if (fi.isFile()) {
            QFile q(f);
            if (q.open(QIODevice::ReadOnly))
                sum = QCryptographicHash::hash(q.readAll(), QCryptographicHash::Sha1).toHex();
        }
        lines << QStringLiteral("%1 %2 %3").arg(QDir(dir).relativeFilePath(f)).arg(fi.size()).arg(QString::fromLatin1(sum));
    }
    lines.sort();
    return lines.join(QLatin1Char('\n'));
}

QString makeGuard(const QString &dir)
{
    QDir().mkpath(dir + QStringLiteral("/inner"));
    writeText(dir + QStringLiteral("/keep-me.txt"), "The guard beside the project (C-40).\n");
    writeText(dir + QStringLiteral("/inner/also.txt"), "Nothing may delete or change this.\n");
    return treeOf(dir);
}

// runs a small Python program (archives with made-up entries); returns its output
QString python(const QString &code, const QStringList &args = {})
{
    const QString script = ctx().work + QStringLiteral("/helper-%1.py").arg(qHash(code));
    writeText(script, code.toUtf8());
    int rc = 0;
    const QString out = runTool(QStringLiteral("python3"), QStringList{QStringLiteral("-I"), script} + args, &rc, 120000);
    if (rc != 0)
        note(QStringLiteral("(python helper rc %1: %2)").arg(rc).arg(out.left(300)));
    return out;
}

// a ZIP with entries named as given; "crc:<name>" writes a wrong checksum, "cut:<name>" cuts the entry
const char *kZipHelper = R"PY(
import sys, zipfile, struct
out = sys.argv[1]
entries = sys.argv[2:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
    for e in entries:
        name, _, text = e.partition("=")
        if name.startswith("crc:") or name.startswith("cut:"):
            name = name[4:]
        z.writestr(zipfile.ZipInfo(name), text.encode("utf-8"))
data = bytearray(open(out, "rb").read())
for e in entries:
    name, _, text = e.partition("=")
    if name.startswith("crc:"):
        n = name[4:].encode()
        i = data.find(b"PK\x03\x04" + b"")
        while i >= 0:
            ln = struct.unpack("<H", data[i+26:i+28])[0]
            if data[i+30:i+30+ln] == n:
                data[i+14:i+18] = b"\xde\xad\xbe\xef"
            i = data.find(b"PK\x03\x04", i + 4)
        # 0.64.2: the central directory's checksum too (readers check the data against that one)
        i = data.find(b"PK\x01\x02")
        while i >= 0:
            ln = struct.unpack("<H", data[i+28:i+30])[0]
            if data[i+46:i+46+ln] == n:
                data[i+16:i+20] = b"\xde\xad\xbe\xef"
            i = data.find(b"PK\x01\x02", i + 4)
open(out, "wb").write(bytes(data))
for e in entries:
    name, _, text = e.partition("=")
    if name.startswith("cut:"):
        n = name[4:].encode()
        d = bytes(data)
        i = d.find(n)
        open(out, "wb").write(d[: i + len(n) + max(1, len(text) // 2)])
print("ok")
)PY";

qint64 rssKb()
{
    const QString s = readText(QStringLiteral("/proc/self/status"));
    const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("VmRSS:\\s+(\\d+)")).match(s);
    return m.hasMatch() ? m.captured(1).toLongLong() : 0;
}

QWidget *topLevelNamed(const QString &name)
{
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w->isVisible() && w->objectName() == name)
            return w;
    return nullptr;
}

} // namespace

// ================================================================ C-40 names and archives from outside
void caseC40()
{
    const QString guard = ctx().work + QStringLiteral("/guard");
    const QString guardTree = makeGuard(guard);
    auto guardIntact = [&](const QString &after) {
        check(treeOf(guard) == guardTree, QStringLiteral("after %1: the guard folder beside the project is unchanged").arg(after),
              firstDifference(guardTree, treeOf(guard)));
    };
    // ---- ids in the project file that would lead out of the project's folder
    {
        step(QStringLiteral("ids that lead out of the folder"));
        const QString path = fixtureCopy(QStringLiteral("fx1-qb"));
        QString qbx = readText(path);
        const QStringList bad = {QStringLiteral(".."), QStringLiteral("../x"), QStringLiteral("../../guard"), QStringLiteral("/tmp/x"),
                                 QStringLiteral("a/b"), QStringLiteral("a\\\\b"), QString(300, QLatin1Char('k'))};
        QRegularExpression idRe(QStringLiteral("<Item id=\"([^\"]+)\" type=\"Text\""));
        QRegularExpressionMatchIterator mi = idRe.globalMatch(qbx);
        QStringList ids;
        while (mi.hasNext() && ids.size() < bad.size())
            ids << mi.next().captured(1);
        QStringList texts;
        {
            QString e0;
            auto before = Project::open(path, &e0);
            for (const QString &id : ids)
                texts << (before ? before->docs()->plainText(id).left(40) : QString());
        }
        for (int i = 0; i < ids.size(); ++i)
            qbx.replace(QStringLiteral("id=\"%1\"").arg(ids.at(i)), QStringLiteral("id=\"%1\"").arg(bad.at(i).toHtmlEscaped()));
        writeText(path, qbx.toUtf8());
        QString err;
        auto p = Project::open(path, &err);
        check(p != nullptr, QStringLiteral("a project file with ids that lead out of its folder opens"), err);
        if (p) {
            QStringList unsafe;
            for (BinderItem *it : p->allItems())
                if (it->parent && (it->id.contains(QLatin1Char('/')) || it->id.contains(QLatin1String("..")) || it->id.contains(QLatin1Char('\\'))))
                    unsafe << it->id;
            check(unsafe.isEmpty(), QStringLiteral("no id that leads out of the folder is kept (they get new ones)"), unsafe.join(QLatin1String(", ")));
            fact(QStringLiteral("C-40 texts of the renamed items"), texts.join(QLatin1String(" | ")));
            // every item of the project into the Trash, then Empty Trash; and a link inside the project to the guard
            const QList<BinderItem *> docs = textItems(p.get());
            if (!docs.isEmpty()) {
                const QString linked = p->contentDir(docs.first()->id);
                QDir(linked).removeRecursively();
                QDir().mkpath(QFileInfo(linked).absolutePath());
                QFile::link(guard, linked);
                p->model()->moveToTrash(docs.mid(0, qMin(int(docs.size()), 12)));
                p->model()->emptyTrash();
                p->save(&err);
                check(!QFileInfo(linked).exists() || !QFileInfo(linked).isSymLink(), QStringLiteral("Empty Trash removes a link inside the project, not what it points to"));
            }
        }
        guardIntact(QStringLiteral("opening, Empty Trash and saving"));
    }
    // ---- media file endings
    {
        step(QStringLiteral("media endings"));
        const QString path = fixtureCopy(QStringLiteral("fx1-qb"));
        QString err;
        auto p = Project::open(path, &err);
        if (p) {
            QStringList outside;
            for (const QString &ext : {QStringLiteral("../x"), QStringLiteral("../../guard/evil"), QStringLiteral("sh"), QStringLiteral("a/b")}) {
                BinderItem *it = p->model()->createItem(p->research(), -1, ItemType::Image, QStringLiteral("Picture %1").arg(ext));
                it->fileExt = ext;
                const QString f = p->mediaFilePath(it);
                if (!pathsafety::isInside(f, p->folderPath()))
                    outside << f;
            }
            check(outside.isEmpty(), QStringLiteral("a picture's ending never leads its file out of the project"), outside.join(QLatin1String(", ")));
        }
    }
    // ---- a Scrivener project whose binder names folders outside Files/Data
    {
        step(QStringLiteral("a .scriv with UUIDs that lead out"));
        const QString scriv = fixtureCopy(QStringLiteral("fx1-scriv"));
        QString scrivx;
        QDirIterator di(scriv, {QStringLiteral("*.scrivx")}, QDir::Files);
        if (di.hasNext())
            scrivx = di.next();
        QString x = readText(scrivx);
        QRegularExpression uu(QStringLiteral("UUID=\"([0-9A-Fa-f-]{36})\""));
        QRegularExpressionMatchIterator ui = uu.globalMatch(x);
        int n = 0;
        while (ui.hasNext() && n < 3) {
            const QString u = ui.next().captured(1);
            x.replace(u, n == 0 ? QStringLiteral("../../../guard") : n == 1 ? QStringLiteral("../x") : QStringLiteral("/tmp/escape"));
            ++n;
        }
        writeText(scrivx, x.toUtf8());
        QString err;
        auto r = scrivener::open(scriv, &err);
        fact(QStringLiteral("C-40 .scriv with UUIDs that lead out"), r.project ? QStringLiteral("opened") : QStringLiteral("refused: %1").arg(err.left(160)));
        if (r.project) {
            r.project->save(&err);
            scrivener::sync(r.project.get(), &err, false);
        }
        guardIntact(QStringLiteral("a .scriv with UUIDs that lead out, opened and saved"));
        check(!QFileInfo::exists(QStringLiteral("/tmp/escape")), QStringLiteral("nothing written at /tmp/escape"));
    }
    // ---- archives: a project template, add-on packages, a DOCX
    {
        step(QStringLiteral("archives with names that lead out, damaged entries"));
        const QString rel = QStringLiteral("../../") + QFileInfo(guard).fileName() + QStringLiteral("/planted.txt");
        const QString tpl = ctx().work + QStringLiteral("/evil.qbtemplate");
        python(QString::fromUtf8(kZipHelper), {tpl, rel + QStringLiteral("=planted"), QStringLiteral("/tmp/planted-abs.txt=planted"),
                                               QStringLiteral("crc:Template/notes.txt=a damaged entry"), QStringLiteral("project.qbx=<Project/>")});
        templates::Info info;
        info.id = QStringLiteral("cs-evil");
        info.name = QStringLiteral("Evil template");
        info.zipPath = tpl;
        QString err;
        const QString into = ctx().work + QStringLiteral("/from-template");
        QDir().mkpath(into);
        auto p = templates::createProject(info, into, QStringLiteral("From Evil"), &err);
        fact(QStringLiteral("C-40 a template archive with names that lead out"), p ? QStringLiteral("a project was made") : QStringLiteral("refused: %1").arg(err.left(160)));
        guardIntact(QStringLiteral("a project template with names that lead out"));
        check(!QFileInfo::exists(QStringLiteral("/tmp/planted-abs.txt")), QStringLiteral("a template entry with an absolute name writes nothing there"));
        // add-on packages
        const QString pkg = ctx().work + QStringLiteral("/evil.qbpkg");
        python(QString::fromUtf8(kZipHelper), {pkg, QStringLiteral("manifest.json={\"format\":1,\"kind\":\"addon\",\"id\":\"cs.evil\",\"name\":\"Evil\",\"version\":\"1.0\",\"files\":[]}"),
                                               QStringLiteral("payload/") + rel + QStringLiteral("=planted"), QStringLiteral("payload//tmp/planted-pkg.txt=planted")});
        QStringList log;
        bool restart = false;
        const bool installed = addons::install(pkg, &log, &err, &restart);
        fact(QStringLiteral("C-40 an add-on package with names that lead out"), installed ? QStringLiteral("installed") : QStringLiteral("refused: %1").arg(err.left(160)));
        guardIntact(QStringLiteral("an add-on package with names that lead out"));
        check(!QFileInfo::exists(QStringLiteral("/tmp/planted-pkg.txt")), QStringLiteral("a package entry with an absolute name writes nothing there"));
        // a DOCX whose document part has a wrong checksum, and one cut off
        for (const QString &kind : {QStringLiteral("crc"), QStringLiteral("cut")}) {
            const QString docx = ctx().work + QStringLiteral("/damaged-%1.docx").arg(kind);
            python(QString::fromUtf8(kZipHelper),
                   {docx, QStringLiteral("[Content_Types].xml=<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>"),
                    kind + QStringLiteral(":word/document.xml=<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body><w:p><w:r><w:t>")
                        + QString(400, QLatin1Char('Z')) + QStringLiteral("</w:t></w:r></w:p></w:body></w:document>")});
            QString e2;
            auto q = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &e2);
            if (!q)
                continue;
            QStringList warnings;
            QElapsedTimer t;
            t.start();
            const QList<BinderItem *> got = importer::importFiles(q.get(), {docx}, q->research(), -1, &warnings);
            QString text;
            for (BinderItem *b : got)
                if (b->hasText())
                    text += q->docs()->plainText(b->id);
            fact(QStringLiteral("C-40 a DOCX with a %1 document part").arg(kind == QLatin1String("crc") ? QStringLiteral("wrong checksum in its") : QStringLiteral("cut-off")),
                 QStringLiteral("%1 item(s), %2 characters, %3 ms; %4").arg(got.size()).arg(text.size()).arg(t.elapsed()).arg(warnings.join(QLatin1String(" | ")).left(160)));
            check(t.elapsed() < 5000 && !(kind == QLatin1String("crc") && text.contains(QLatin1String("ZZZZ"))),
                  QStringLiteral("a DOCX with a damaged entry (%1): read in a moment, the damaged part not read as text").arg(kind));
        }
        guardIntact(QStringLiteral("the archives"));
    }
}

// ================================================================ C-41 loops and deep nesting in files
void caseC41()
{
    QString err;
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &err);
    if (!p) {
        check(false, QStringLiteral("(FX-1 opens)"), err);
        return;
    }
    auto importOne = [&](const QString &what, const QString &file) {
        QStringList warnings;
        const qint64 rss0 = rssKb();
        QElapsedTimer t;
        t.start();
        const QList<BinderItem *> got = importer::importFiles(p.get(), {file}, p->research(), -1, &warnings);
        const qint64 ms = t.elapsed();
        const qint64 grew = (rssKb() - rss0) / 1024;
        fact(QStringLiteral("C-41 %1").arg(what), QStringLiteral("%1 item(s), %2 ms, memory +%3 MB; %4").arg(got.size()).arg(ms).arg(grew).arg(warnings.join(QLatin1String(" | ")).left(160)));
        check(ms < 2000, QStringLiteral("%1: returns within 2 s").arg(what), QStringLiteral("%1 ms").arg(ms));
        check(grew < 200, QStringLiteral("%1: the memory grows less than 200 MB (proposed limit)").arg(what), QStringLiteral("%1 MB").arg(grew));
    };
    // ---- Word 97 (compound files): the sector chains loop or leave the file
    {
        step(QStringLiteral("Word 97 files whose chains loop"));
        auto cfb = [](int variant) {
            QByteArray f(512 * 4, '\0');
            f.replace(0, 8, QByteArray::fromHex("D0CF11E0A1B11AE1"));
            auto put16 = [&](int at, quint16 v) { qToLittleEndian<quint16>(v, f.data() + at); };
            auto put32 = [&](int at, quint32 v) { qToLittleEndian<quint32>(v, f.data() + at); };
            put16(0x1A, 3);
            put16(0x1C, 0xFFFE);
            put16(0x1E, 9);
            put16(0x20, 6);
            put32(0x2C, variant == 1 ? 1 : 0);
            put32(0x30, variant == 1 ? 2 : 0xFFFFFFFE);
            put32(0x38, 4096);
            put32(0x3C, 0xFFFFFFFE);
            put32(0x44, variant == 2 ? 99999 : variant == 1 ? 0xFFFFFFFE : 0);
            put32(0x48, variant == 1 ? 0 : 50000);
            for (int i = 0; i < 109; ++i)
                put32(0x4C + i * 4, 0xFFFFFFFF);
            put32(0x4C, 1);
            for (int i = 0; i < 127; ++i)
                put32(512 + i * 4, 1);
            put32(512 + 127 * 4, 0);
            for (int i = 0; i < 128; ++i)
                put32(1024 + i * 4, variant == 1 ? 2 : 0xFFFFFFFF);   // variant 1: the directory's chain points to itself
            if (variant == 3)
                f.truncate(300);   // cut in the header
            return f;
        };
        const QStringList names = {QStringLiteral("a DIFAT chain that loops"), QStringLiteral("a FAT chain that loops (the directory)"),
                                   QStringLiteral("a DIFAT chain beyond the end of the file"), QStringLiteral("a file cut in its header")};
        for (int v = 0; v < 4; ++v) {
            const QString f = ctx().work + QStringLiteral("/loop-%1.doc").arg(v);
            QFile o(f);
            if (o.open(QIODevice::WriteOnly))
                o.write(cfb(v));
            o.close();
            importOne(QStringLiteral("Word 97: %1").arg(names.at(v)), f);
        }
    }
    // ---- DOCX and ODT nested deeply
    {
        step(QStringLiteral("DOCX and ODT nested deeply"));
        const char *zipper = R"PY(
import sys, zipfile
out, kind, depth = sys.argv[1], sys.argv[2], int(sys.argv[3])
inner = "<w:p><w:r><w:t>Deep</w:t></w:r></w:p>" if kind == "docx" else "<text:p>Deep</text:p>"
tag = "w:sdtContent" if kind == "docx" else "text:section"
body = ("<%s>" % tag) * depth + inner + ("</%s>" % tag) * depth
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    if kind == "docx":
        z.writestr("[Content_Types].xml", '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>')
        z.writestr("word/document.xml", '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>' + body + '</w:body></w:document>')
    else:
        z.writestr("mimetype", "application/vnd.oasis.opendocument.text")
        z.writestr("content.xml", '<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"><office:body><office:text>' + body + '</office:text></office:body></office:document-content>')
print("ok")
)PY";
        for (const QString &kind : {QStringLiteral("docx"), QStringLiteral("odt")})
            for (int depth : {1000, 1001, 100000}) {
                const QString f = ctx().work + QStringLiteral("/deep-%1-%2.%3").arg(kind).arg(depth).arg(kind);
                python(QString::fromUtf8(zipper), {f, kind, QString::number(depth)});
                importOne(QStringLiteral("%1 nested %2 deep").arg(kind.toUpper()).arg(depth), f);
            }
    }
    // ---- a .scrivx and a styles.xml nested 1,001 deep, an XML of 50 MB in one element
    {
        step(QStringLiteral("Scrivener files nested deeply or huge"));
        struct Bad {
            QString what;
            QString file;   // inside the .scriv
            QByteArray data;
        };
        QByteArray deep;
        for (int i = 0; i < 1001; ++i)
            deep += "<Binder>";
        for (int i = 0; i < 1001; ++i)
            deep += "</Binder>";
        const QList<Bad> bads = {
            {QStringLiteral("a .scrivx nested 1,001 deep"), QStringLiteral("*.scrivx"), "<?xml version=\"1.0\"?><ScrivenerProject>" + deep + "</ScrivenerProject>"},
            {QStringLiteral("a styles.xml nested 1,001 deep"), QStringLiteral("Files/styles.xml"), "<?xml version=\"1.0\"?><Styles>" + deep + "</Styles>"},
            {QStringLiteral("a .scrivx of 50 MB in one element"), QStringLiteral("*.scrivx"),
             "<?xml version=\"1.0\"?><ScrivenerProject><Binder><BinderItem UUID=\"00000000-0000-0000-0000-000000000001\" Type=\"Text\"><Title>"
                 + QByteArray(sized(50, 5) * 1024 * 1024, 'x') + "</Title></BinderItem></Binder></ScrivenerProject>"},
        };
        for (const Bad &b : bads) {
            const QString scriv = fixtureCopy(QStringLiteral("fx1-scriv"));
            QString target = scriv + QLatin1Char('/') + b.file;
            if (b.file.startsWith(QLatin1Char('*'))) {
                QDirIterator di(scriv, {b.file}, QDir::Files);
                target = di.hasNext() ? di.next() : scriv + QStringLiteral("/project.scrivx");
            }
            writeText(target, b.data);
            const qint64 rss0 = rssKb();
            QElapsedTimer t;
            t.start();
            QString e;
            auto r = scrivener::open(scriv, &e);
            const qint64 ms = t.elapsed();
            const qint64 grew = (rssKb() - rss0) / 1024;
            fact(QStringLiteral("C-41 %1").arg(b.what), QStringLiteral("%1, %2 ms, memory +%3 MB").arg(r.project ? QStringLiteral("opened") : QStringLiteral("refused: %1").arg(e.left(120))).arg(ms).arg(grew));
            check(ms < 2000 || b.what.contains(QLatin1String("50 MB")), QStringLiteral("%1: returns within 2 s").arg(b.what), QStringLiteral("%1 ms").arg(ms));
            check(grew < 400, QStringLiteral("%1: the memory grows less than 400 MB (proposed limit)").arg(b.what), QStringLiteral("%1 MB").arg(grew));
        }
    }
}

// ================================================================ C-42 Scrivener's lock at every save
void caseC42()
{
    const QByteArray lockData = "user=scrivener-writer\nhost=OTHER-PC\npid=4242\n";
    auto treeHash = [](const QString &scriv) {
        QStringList l;
        QDirIterator it(scriv, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (f.endsWith(QLatin1String("user.lock")) || f.endsWith(QLatin1String("quillbinder.lock")))
                continue;
            QFile q(f);
            q.open(QIODevice::ReadOnly);
            l << QDir(scriv).relativeFilePath(f) + QLatin1Char(' ') + QString::fromLatin1(QCryptographicHash::hash(q.readAll(), QCryptographicHash::Sha1).toHex());
        }
        l.sort();
        return l.join(QLatin1Char('\n'));
    };
    // (a) a lock that comes while the project is open
    {
        step(QStringLiteral("(a) the lock comes while the project is open"));
        const QString scriv = fixtureCopy(QStringLiteral("fx1-scriv"));
        QString err;
        auto r = scrivener::open(scriv, &err);
        if (!r.project) {
            check(false, QStringLiteral("(FX-1 in Scrivener format opens)"), err);
            return;
        }
        Project *p = r.project.get();
        BinderItem *it = textItems(p).value(3);
        QTextCursor c(p->docs()->text(it->id));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" Typed while Scrivener opened it (C-42)."));
        p->docs()->notes(it->id)->setPlainText(QStringLiteral("A note written while Scrivener had it (C-42)."));
        it->title += QStringLiteral(" C42");
        p->markIndexDirty();
        writeText(scriv + QStringLiteral("/Files/user.lock"), lockData);
        const QString before = treeHash(scriv);
        QString e1, e2;
        const bool quickOk = p->autosave(&e1);
        const bool fullOk = p->save(&e2);
        fact(QStringLiteral("C-42 (a) the quick write and the full write"), QStringLiteral("%1 / %2: %3").arg(quickOk).arg(fullOk).arg((e2.isEmpty() ? e1 : e2).left(200)));
        check(!fullOk && !p->scrivenerLockRefused.isEmpty(), QStringLiteral("(a) the save reports failure while Scrivener's lock is there"), e2.left(200));
        check((e2 + e1).contains(QLatin1String("scrivener-writer")) && (e2 + e1).contains(QLatin1String("OTHER-PC")),
              QStringLiteral("(a) the message names Scrivener's user and computer"), (e2 + e1).left(200));
        check(treeHash(scriv) == before, QStringLiteral("(a) nothing in the .scriv changes while the lock is there"), firstDifference(before, treeHash(scriv)));
        check(p->docs()->plainText(it->id).contains(QLatin1String("Typed while Scrivener opened it")), QStringLiteral("(a) the working copy keeps the change"));
        QFile::remove(scriv + QStringLiteral("/Files/user.lock"));
        QString e3;
        check(p->save(&e3), QStringLiteral("(a) after the lock is gone the save works"), e3);
        auto again = scrivener::open(scriv, &e3);
        bool found = false;
        if (again.project)
            for (BinderItem *x : textItems(again.project.get()))
                if (again.project->docs()->plainText(x->id).contains(QLatin1String("Typed while Scrivener opened it")))
                    found = true;
        check(found, QStringLiteral("(a) the .scriv read afresh holds the change"));
    }
    // (b) the lock was there at opening; then another one
    {
        step(QStringLiteral("(b) the lock was there at opening, then a new one"));
        const QString scriv = fixtureCopy(QStringLiteral("fx1-scriv"));
        writeText(scriv + QStringLiteral("/Files/user.lock"), lockData);
        QString err;
        auto r = scrivener::open(scriv, &err);
        fact(QStringLiteral("C-42 (b) opening with Scrivener's lock (component level)"), r.project ? QStringLiteral("opened") : QStringLiteral("refused: %1").arg(err.left(160)));
        if (r.project) {
            Project *p = r.project.get();
            BinderItem *it = textItems(p).value(5);
            QTextCursor c(p->docs()->text(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" With the lock of the opening (C-42)."));
            QString e1;
            const bool ok = p->save(&e1);
            check(ok, QStringLiteral("(b) the lock that was there at opening does not stop saving"), e1.left(200));
            settle(1100);
            writeText(scriv + QStringLiteral("/Files/user.lock"), lockData + "opened-again=1\n");
            c.insertText(QStringLiteral(" After the new lock (C-42)."));
            QString e2;
            check(!p->save(&e2), QStringLiteral("(b) a new lock stops saving"), e2.left(200));
            QFile::remove(scriv + QStringLiteral("/Files/user.lock"));
        }
    }
    // (c) a lock that comes and goes between two quick writes
    {
        step(QStringLiteral("(c) a lock that comes and goes"));
        const QString scriv = fixtureCopy(QStringLiteral("fx1-scriv"));
        QString err;
        auto r = scrivener::open(scriv, &err);
        if (r.project) {
            Project *p = r.project.get();
            BinderItem *it = textItems(p).value(7);
            QTextCursor c(p->docs()->text(it->id));
            c.insertText(QStringLiteral("Before. "));
            QString e;
            p->autosave(&e);
            writeText(scriv + QStringLiteral("/Files/user.lock"), lockData);
            c.insertText(QStringLiteral("During. "));
            const bool during = p->autosave(&e);
            QFile::remove(scriv + QStringLiteral("/Files/user.lock"));
            c.insertText(QStringLiteral("After. "));
            const bool after = p->autosave(&e);
            fact(QStringLiteral("C-42 (c) quick writes before, during, after the lock"), QStringLiteral("during %1, after %2").arg(during).arg(after));
            check(after && p->save(&e), QStringLiteral("(c) once the lock is gone, saving works again"), e.left(200));
        }
    }
}

// ================================================================ S-P09 Scrivener opens the project while it is open here
// The program's autosave: its timer emits saveFailed when the save fails, which the window answers
// (a direct Project::autosave() does not; 0.64.2)
static void autosaveAsProgram(Project *p)
{
    QString err;
    if (!p->autosave(&err))
        emit p->saveFailed(err);
}

void caseSP09()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
    Project *p = w->project();
    if (!p)
        return;
    const QString scriv = p->scrivenerPath;
    BinderItem *it = textItems(p).value(2);
    const QString itId = it->id;   // 0.64.2: the item is gone with the project closed at the end
    TextEditor *e = showDoc(w, it);
    if (!e)
        return;
    auto typeLine = [&](const QString &s) {
        e->setFocus();
        QTextCursor c = e->textCursor();
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
        keysInto(e, s);
        settle(200);
    };
    auto notices = [] {
        int n = 0;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (tl->isVisible() && tl->objectName() == QLatin1String("scrivenerLockNotice"))
                ++n;
        return n;
    };
    auto closeNotices = [&](QStringList *texts) {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible() && b->objectName() == QLatin1String("scrivenerLockNotice")) {
                if (texts)
                    *texts << b->text();
                b->accept();
            }
        settle(200);
    };
    typeLine(QStringLiteral(" Before Scrivener came."));
    QString err;
    p->save(&err);
    const QString lock = scriv + QStringLiteral("/Files/user.lock");
    writeText(lock, "user=scrivener-writer\nhost=OTHER-PC\n");
    step(QStringLiteral("a lock appears; typing goes on through two autosaves and File > Save"));
    QStringList texts;
    typeLine(QStringLiteral(" Typed while Scrivener has it."));
    autosaveAsProgram(p);
    settle(400);
    const int first = notices();
    closeNotices(&texts);
    typeLine(QStringLiteral(" More typing."));
    autosaveAsProgram(p);
    settle(400);
    const int second = notices();
    chooseByMenu(w, QStringLiteral("save"));
    settle(400);
    const int third = notices();
    closeNotices(&texts);
    const QString status = w->statusBar()->currentMessage();
    fact(QStringLiteral("S-P09 notices: first save, second, File > Save"), QStringLiteral("%1, %2, %3; status bar: %4").arg(first).arg(second).arg(third).arg(status));
    check(first == 1 && second == 0 && third == 0, QStringLiteral("the notice comes once per lock"), QStringLiteral("%1, %2, %3").arg(first).arg(second).arg(third));
    check(texts.join(QLatin1Char(' ')).contains(QLatin1String("scrivener-writer")) && texts.join(QLatin1Char(' ')).contains(QLatin1String("OTHER-PC")),
          QStringLiteral("the notice names Scrivener's user and computer"), texts.join(QLatin1String(" | ")).left(300));
    check(status.contains(QLatin1String("Scrivener")), QStringLiteral("later failed saves show in the status bar"), status);
    // a new lock brings the notice again
    step(QStringLiteral("a new lock"));
    settle(1100);
    writeText(lock, "user=scrivener-writer\nhost=OTHER-PC\nagain=1\n");
    typeLine(QStringLiteral(" Under the new lock."));
    autosaveAsProgram(p);
    settle(400);
    check(notices() == 1, QStringLiteral("a new lock brings the notice again"));
    closeNotices(&texts);
    // Close Project asks with its own text; No keeps the project open
    step(QStringLiteral("File > Close Project while Scrivener has it: No"));
    BoxText question;
    answerBoxLater(400, QMessageBox::No, &question);
    chooseByMenu(w, QStringLiteral("closeProject"));
    settle(800);
    fact(QStringLiteral("S-P09 the close question"), question.left(400));
    check(w->project() != nullptr, QStringLiteral("No at the close question keeps the project open"));
    check(question.contains(QLatin1String("working")) || question.contains(QLatin1String("Scrivener")),
          QStringLiteral("the close question has its own text about Scrivener"), question.left(200));
    // the lock goes; everything typed reaches the .scriv
    step(QStringLiteral("the lock goes; File > Save"));
    QFile::remove(lock);
    chooseByMenu(w, QStringLiteral("save"));
    settle(600);
    closeDialogsLater(1500);   // 0.64.2: the start window after closing (300 ms came before it)
    w->closeProject();
    settle(1800);
    QString e2;
    auto r = scrivener::open(scriv, &e2);
    QString all;
    if (r.project)
        all = r.project->docs()->plainText(itId);
    for (const QString &piece : {QStringLiteral("Before Scrivener came"), QStringLiteral("Typed while Scrivener has it"), QStringLiteral("More typing"),
                                 QStringLiteral("Under the new lock")})
        check(all.contains(piece), QStringLiteral("after the lock is gone the .scriv holds \"%1\"").arg(piece));
    delete w;
}

// ================================================================ S-I12 links that lead out of the program
void caseSI12()
{
    qunsetenv("QB_NO_DIALOGS");   // the questions are the point of this case
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    EditorPane *pane = visiblePane(w);
    QList<QUrl> opened;
    links::setOpenerForTests([&opened](const QUrl &u) { opened << u; });
    // targets
    const QString t = ctx().work + QStringLiteral("/targets");
    QDir().mkpath(t + QStringLiteral("/a folder"));
    writeText(t + QStringLiteral("/notes.txt"), "plain notes\n");
    writeText(t + QStringLiteral("/Größe Datei.txt"), "umlauts and spaces\n");
    QFile::copy(ctx().sourceDir + QStringLiteral("/tests/import/tower.png"), t + QStringLiteral("/picture.png"));
    writeText(t + QStringLiteral("/script.sh"), "#!/bin/sh\necho no\n");
    writeText(t + QStringLiteral("/start.desktop"), "[Desktop Entry]\nExec=false\n");
    QFile::copy(QStringLiteral("/bin/true"), t + QStringLiteral("/program"));
    writeText(t + QStringLiteral("/runme"), "#!/bin/sh\necho no\n");
    writeText(t + QStringLiteral("/winprog"), "MZ\x90\x00 not really\n");
    writeText(t + QStringLiteral("/setup.exe"), "MZ not really\n");
    for (const QString &x : {QStringLiteral("script.sh"), QStringLiteral("program"), QStringLiteral("runme"), QStringLiteral("winprog")})
        ::chmod(QFile::encodeName(t + QLatin1Char('/') + x).constData(), 0755);
    enum Kind { AtOnce, Ask, Refuse };
    struct Target {
        QString href;
        Kind kind;
    };
    const QList<Target> targets = {
        {QStringLiteral("https://example.org/page"), AtOnce},
        {QStringLiteral("http://example.org/"), AtOnce},
        {QStringLiteral("mailto:writer@example.org"), AtOnce},
        {QUrl::fromLocalFile(t + QStringLiteral("/notes.txt")).toString(), Ask},
        {QUrl::fromLocalFile(t + QStringLiteral("/picture.png")).toString(), Ask},
        {QUrl::fromLocalFile(t + QStringLiteral("/a folder")).toString(), Ask},
        {QUrl::fromLocalFile(t + QStringLiteral("/missing.txt")).toString(), Ask},
        {QUrl::fromLocalFile(t + QStringLiteral("/Größe Datei.txt")).toString(), Ask},
        {QStringLiteral("smb://server/share/file.txt"), Ask},
        {QUrl::fromLocalFile(t + QLatin1Char('/') + QString(260, QLatin1Char('n')) + QStringLiteral(".txt")).toString(), Ask},
        {QUrl::fromLocalFile(t + QStringLiteral("/script.sh")).toString(), Refuse},
        {QUrl::fromLocalFile(t + QStringLiteral("/start.desktop")).toString(), Refuse},
        {QUrl::fromLocalFile(t + QStringLiteral("/program")).toString(), Refuse},
        {QUrl::fromLocalFile(t + QStringLiteral("/runme")).toString(), Refuse},
        {QUrl::fromLocalFile(t + QStringLiteral("/winprog")).toString(), Refuse},
        {QUrl::fromLocalFile(t + QStringLiteral("/setup.exe")).toString(), Refuse},
    };
    QStringList wrong, questions;
    for (const Target &tg : targets) {
        const QUrl url = links::urlOf(tg.href);
        const links::Decision d = links::judge(url);
        const links::Decision want = tg.kind == AtOnce ? links::Decision::Open : tg.kind == Ask ? links::Decision::Ask : links::Decision::Refuse;
        if (d != want) {
            wrong << QStringLiteral("%1: judged %2").arg(tg.href.right(60)).arg(int(d));
            if (d == links::Decision::Ask || want == links::Decision::Ask)
                continue;   // a question nobody expects would block the case
        }
        if (tg.kind == AtOnce) {
            opened.clear();
            emit pane->linkActivated(pane, tg.href);
            settle(150);
            if (opened.size() != 1)
                wrong << QStringLiteral("%1: not opened at once").arg(tg.href);
            continue;
        }
        if (tg.kind == Refuse) {
            opened.clear();
            QString text;
            withModal<QMessageBox>(250, [&text](QMessageBox *b) {
                text = b->objectName() + QStringLiteral(": ") + b->text();
                b->accept();
            });
            emit pane->linkActivated(pane, tg.href);
            settle(400);
            if (!opened.isEmpty())
                wrong << QStringLiteral("%1: a program file reached the opener").arg(tg.href.right(60));
            if (!text.startsWith(QLatin1String("linkRefused")))
                wrong << QStringLiteral("%1: no refusal shown (%2)").arg(tg.href.right(40), text.left(60));
            continue;
        }
        // Ask: Esc, Enter (Cancel is the default), then the button that opens
        for (int answer = 0; answer < 3; ++answer) {
            opened.clear();
            QString text, name;
            withModal<QMessageBox>(250, [answer, &text, &name](QMessageBox *b) {
                name = b->objectName();
                text = b->text();
                if (answer == 0)
                    QTest::keyClick(b, Qt::Key_Escape);
                else if (answer == 1)
                    QTest::keyClick(b, Qt::Key_Return);
                else if (QAbstractButton *o = b->button(QMessageBox::Open))
                    o->click();
                else
                    b->reject();
            });
            emit pane->linkActivated(pane, tg.href);
            settle(300);
            if (answer == 0)
                questions << text.left(400);
            if (name != QLatin1String("linkQuestion"))
                wrong << QStringLiteral("%1: no question (%2)").arg(tg.href.right(40), name);
            const QString shown = QUrl(tg.href).isLocalFile() ? QUrl(tg.href).toLocalFile() : tg.href;
            if (answer == 0 && !text.contains(QFileInfo(shown).fileName().left(80)))
                wrong << QStringLiteral("%1: the question does not show the target").arg(tg.href.right(40));
            if (answer < 2 && !opened.isEmpty())
                wrong << QStringLiteral("%1: opened after %2").arg(tg.href.right(40), answer == 0 ? QStringLiteral("Esc") : QStringLiteral("Enter"));
            if (answer == 2 && (opened.size() != 1 || opened.first() != url))
                wrong << QStringLiteral("%1: the open button did not open exactly the target").arg(tg.href.right(40));
        }
    }
    links::setOpenerForTests(nullptr);
    writeText(ctx().dir + QStringLiteral("/u20-link-questions.txt"), questions.join(QStringLiteral("\n----\n")).toUtf8());
    check(wrong.isEmpty(), QStringLiteral("links: web and mail at once, other targets asked (Cancel the default), programs refused (0.64.1)"),
          wrong.mid(0, 10).join(QLatin1String("; ")));
    // a link to a document of the project still opens without a question
    {
        BinderItem *target = textItems(p).value(4);
        int boxes = 0;
        QTimer guard;
        QObject::connect(&guard, &QTimer::timeout, [&boxes] {
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible()) {
                    ++boxes;
                    b->reject();
                }
        });
        guard.start(100);
        emit pane->linkActivated(pane, QStringLiteral("qb://%1").arg(target->id));
        settle(400);
        guard.stop();
        // where it opens (this pane, the other one, a Quick Reference panel) is the program's choice: recorded
        bool shown = false;
        for (EditorPane *ep : visiblePanes(w))
            if (ep->currentItem() == target)
                shown = true;
        fact(QStringLiteral("S-I12 a link to a document of the project"), shown ? QStringLiteral("shown in a pane of the window") : QStringLiteral("not shown in a pane of the window"));
        check(boxes == 0, QStringLiteral("a link to a document of the project opens without a question"));
    }
    qputenv("QB_NO_DIALOGS", "1");
    finishProject(w);
    delete w;
}

// ================================================================ S-L08 a name of one's own for this computer
void caseSL08()
{
    qunsetenv("QB_DEVICE_NAME");   // it would win over the setting
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QString folder = p->folderPath();
    auto setName = [&](const QString &name) {
        bool found = false;
        withModal<OptionsDialog>(400, [name, &found](OptionsDialog *d) {
            if (auto *le = d->findChild<QLineEdit *>(QStringLiteral("optDeviceName"))) {
                found = true;
                le->setText(name);
            }
            // OK, as the writer presses it (accept() alone does not take the values: shakedown 2026-11-09)
            for (QDialogButtonBox *bb : d->findChildren<QDialogButtonBox *>())
                if (QPushButton *ok = bb->button(QDialogButtonBox::Ok)) {
                    ok->click();
                    return;
                }
            d->accept();
        });
        chooseByMenu(w, QStringLiteral("options"));
        settle(500);
        return found;
    };
    const QString name = QStringLiteral("Schreibtisch Größe");
    check(setName(name), QStringLiteral("File > Options > General > Saving has the field for this computer's name"));
    // the project opened again, so that its lock is written with the name
    const QString path = projectPathOf(p);
    w->closeProject();
    settle(300);
    w->openProject(path);
    settle(800);
    p = w->project();
    if (!p)
        return;
    const syncsafe::LockInfo li = syncsafe::readLock(folder + QStringLiteral("/settings/lock"));
    check(li.device == name && li.user.isEmpty() && !li.key.isEmpty(), QStringLiteral("the lock holds the name, no user name, and the machine key (0.64.1)"),
          QStringLiteral("device %1, user %2, key %3").arg(li.device, li.user, li.key));
    // the view state is filed under the name
    showDoc(w, textItems(p).value(6));
    w->closeProject();
    settle(300);
    QStringList uiFiles = QDir(folder + QStringLiteral("/settings")).entryList({QStringLiteral("ui-*.json")}, QDir::Files);
    fact(QStringLiteral("S-L08 view state files"), uiFiles.join(QLatin1String(", ")));
    // 0.81.0 (audit of 0.80.0, S-06): the view state is filed under the machine key, so a renamed computer keeps it
    check(uiFiles.contains(QStringLiteral("ui-%1.json").arg(syncsafe::machineKey())),
          QStringLiteral("the view state is filed under this computer's key (not its name, since 0.81.0)"), uiFiles.join(QLatin1String(", ")));
    // a name with a slash, then none
    w->openProject(path);
    settle(500);
    setName(QStringLiteral("Laptop/Küche"));
    w->closeProject();
    settle(300);
    uiFiles = QDir(folder + QStringLiteral("/settings")).entryList({QStringLiteral("ui-*.json")}, QDir::Files);
    fact(QStringLiteral("S-L08 view state files after a name with a slash"), uiFiles.join(QLatin1String(", ")));
    w->openProject(path);
    settle(500);
    const QString shownAfterRename = w->project() && visiblePane(w) && visiblePane(w)->currentItem() ? visiblePane(w)->currentItem()->title : QString();
    fact(QStringLiteral("S-L08 the document shown after the computer was renamed"), shownAfterRename);
    setName(QString());
    w->closeProject();
    settle(300);
    w->openProject(path);
    settle(500);
    const syncsafe::LockInfo li2 = syncsafe::readLock(folder + QStringLiteral("/settings/lock"));
    check(li2.device == syncsafe::deviceName() && li2.device != name, QStringLiteral("the field cleared: the computer's own host name again"), li2.device);
    // never a project's setting
    const QString ini = readText(folder + QStringLiteral("/settings/options.ini"));
    check(!ini.contains(QLatin1String("deviceName")), QStringLiteral("the computer's name is never stored in the project"));
    finishProject(w);
    delete w;
}

// ================================================================ S-A07 the warning before packages with program code
void caseSA07()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    const QString tool = ctx().sourceDir + QStringLiteral("/tools/make-package.py");
    struct Pkg {
        QString what;
        QStringList files;   // payload paths
        bool code;
    };
    const QList<Pkg> pkgs = {{QStringLiteral("themes only"), {QStringLiteral("themes/cs-plain.json")}, false},
                             {QStringLiteral("a library"), {QStringLiteral("lib/libcs.so")}, true},
                             {QStringLiteral("a versioned library"), {QStringLiteral("lib/libcs.so.60")}, true},
                             {QStringLiteral("plugins"), {QStringLiteral("plugins/cs-plugin.txt")}, true},
                             {QStringLiteral("templates"), {QStringLiteral("templates/cs-template.txt")}, true}};
    QMap<QString, QString> made;
    int k = 0;
    for (const Pkg &pk : pkgs) {
        const QString folder = ctx().work + QStringLiteral("/pkg%1").arg(k);
        QDir().mkpath(folder + QStringLiteral("/payload"));
        writeText(folder + QStringLiteral("/manifest.json"),
                  QStringLiteral("{\"id\":\"cs.pkg%1\",\"name\":\"CS %2\",\"version\":\"1.0\",\"description\":\"system test\",\"author\":\"test\",\"whatsNew\":\"\",\"minApp\":\"0.1.0\"}")
                      .arg(k)
                      .arg(pk.what)
                      .toUtf8());
        for (const QString &f : pk.files) {
            QDir().mkpath(QFileInfo(folder + QStringLiteral("/payload/") + f).absolutePath());
            writeText(folder + QStringLiteral("/payload/") + f, "{\"name\": \"cs\"}\n");
        }
        const QString out = ctx().work + QStringLiteral("/pkg%1.qbpkg").arg(k);
        runTool(QStringLiteral("python3"), {QStringLiteral("-I"), tool, QStringLiteral("addon"), folder, out});
        made.insert(pk.what, out);
        ++k;
    }
    // a program update (the test build's program; only shown, never installed) and a bundle
    const QString core = ctx().work + QStringLiteral("/core.qbpkg");
    runTool(QStringLiteral("python3"), {QStringLiteral("-I"), tool, QStringLiteral("core"), QStringLiteral("--binary"), ctx().buildDir + QStringLiteral("/quillbinder"),
                                        QStringLiteral("--version"), QStringLiteral("99.0.0"), QStringLiteral("--runtime"), QStringLiteral(QB_RUNTIME_ID), core});   // 0.80.0: the runtime of this build (another one is refused before the window)
    const QString bundle = ctx().work + QStringLiteral("/bundle.qbpkg");
    runTool(QStringLiteral("python3"), {QStringLiteral("-I"), tool, QStringLiteral("bundle"), QStringLiteral("--id"), QStringLiteral("cs.bundle"), QStringLiteral("--name"),
                                        QStringLiteral("CS bundle"), QStringLiteral("--version"), QStringLiteral("1.0"), made.value(QStringLiteral("themes only")),
                                        made.value(QStringLiteral("a library")), bundle});
    QList<QPair<QString, bool>> all;
    for (const Pkg &pk : pkgs)
        all << qMakePair(pk.what, pk.code);
    all << qMakePair(QStringLiteral("a program update"), true) << qMakePair(QStringLiteral("a bundle with one library"), true);
    made.insert(QStringLiteral("a program update"), core);
    made.insert(QStringLiteral("a bundle with one library"), bundle);
    QStringList wrong;
    const QString addonsDir = addons::addonsRoot();
    for (const auto &pr : all) {
        const QString file = made.value(pr.first);
        if (!QFileInfo::exists(file)) {
            note(QStringLiteral("(%1: the package could not be made)").arg(pr.first));
            continue;
        }
        for (int answer = 0; answer < 2; ++answer) {
            if (answer == 1 && pr.first == QLatin1String("a program update"))
                continue;   // never install a program update into the test's home
            // the add-ons there (their folders), not the state file the program may touch when it looks
            auto installedSet = [&addonsDir] {
                QStringList l = QDir(addonsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                l.sort();
                return l.join(QLatin1Char(','));
            };
            const QString before = installedSet();
            bool warned = false, seen = false, installDefault = false;
            withModal<QDialog>(300, [answer, &warned, &seen, &installDefault](QDialog *d) {
                seen = true;
                if (auto *l = d->findChild<QLabel *>(QStringLiteral("addonCodeWarning")))
                    warned = l->isVisible() || !l->isHidden();
                for (QPushButton *b : d->findChildren<QPushButton *>())
                    if (b->text().remove(QLatin1Char('&')) == QLatin1String("Install"))
                        installDefault = b->isDefault();
                if (answer == 0) {
                    d->reject();
                } else {
                    for (QPushButton *b : d->findChildren<QPushButton *>())
                        if (b->text().remove(QLatin1Char('&')) == QLatin1String("Install")) {
                            b->click();
                            return;
                        }
                    d->reject();
                }
            });
            bool restart = false;
            const bool ok = runInstallPackage(w, file, &restart);
            settle(300);
            if (!seen) {
                wrong << QStringLiteral("%1: no install window").arg(pr.first);
                break;
            }
            if (warned != pr.second)
                wrong << QStringLiteral("%1: warning %2").arg(pr.first, warned ? QStringLiteral("shown") : QStringLiteral("missing"));
            if (answer == 0 && (ok || installedSet() != before))
                wrong << QStringLiteral("%1: Cancel installed something").arg(pr.first);
            if (answer == 1)
                fact(QStringLiteral("S-A07 %1 after Install").arg(pr.first), ok ? QStringLiteral("installed") : QStringLiteral("not installed"));
            if (answer == 0)
                fact(QStringLiteral("S-A07 %1: Install is the default button").arg(pr.first), installDefault);
        }
    }
    check(wrong.isEmpty(), QStringLiteral("the warning shows for every package with program code and never without; Cancel installs nothing (0.64.1)"),
          wrong.join(QLatin1String("; ")));
    closeDialogsLater(300);
    settle(500);
    delete w;
}

// ================================================================ U-20 the questions that guard the writer
void caseU20()
{
    // the wordings, collected from the cases of the run that met them, and the windows opened here at two sizes
    QStringList lines;
    const QString runDir = QFileInfo(ctx().dir).absolutePath();
    lines << QStringLiteral("# U-20: the questions, warnings and notices of 0.64.1") << QString();
    const QString linkQs = readText(runDir + QStringLiteral("/S-I12/u20-link-questions.txt"));
    lines << QStringLiteral("## The question before a link leaves the program (S-I12)") << (linkQs.isEmpty() ? QStringLiteral("(S-I12 not in this run)") : linkQs) << QString();
    // facts of other cases
    for (const QString &c : {QStringLiteral("S-P09"), QStringLiteral("S-A07"), QStringLiteral("S-L06"), QStringLiteral("S-R03"), QStringLiteral("S-W15"), QStringLiteral("S-T04")}) {
        const QString facts = readText(runDir + QLatin1Char('/') + c + QStringLiteral("/facts.json"));
        lines << QStringLiteral("## Facts of %1").arg(c) << (facts.isEmpty() ? QStringLiteral("(not in this run)") : facts.left(6000)) << QString();
    }
    // the windows themselves, opened here at the writer's sizes
    qunsetenv("QB_NO_DIALOGS");
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    const QList<QSize> sizes = {QSize(1366, 768), QSize(1920, 1080)};
    int shots = 0;
    for (const QSize &sz : sizes) {
        w->resize(sz.width() - 40, sz.height() - 60);
        settle(300);
        // the link question with a long target
        withModal<QMessageBox>(300, [&shots, sz](QMessageBox *b) {
            shot(b, QStringLiteral("u20-linkQuestion-%1x%2").arg(sz.width()).arg(sz.height()));
            fact(QStringLiteral("U-20 link question %1x%2: size").arg(sz.width()).arg(sz.height()), QStringLiteral("%1 x %2").arg(b->width()).arg(b->height()));
            ++shots;
            b->reject();
        });
        links::setOpenerForTests([](const QUrl &) {});
        emit visiblePane(w)->linkActivated(visiblePane(w), QUrl::fromLocalFile(ctx().work + QLatin1Char('/') + QString(300, QLatin1Char('n')) + QStringLiteral(".txt")).toString());
        settle(400);
        // the refusal of a program file
        writeText(ctx().work + QStringLiteral("/u20.sh"), "#!/bin/sh\n");
        withModal<QMessageBox>(300, [&shots, sz](QMessageBox *b) {
            shot(b, QStringLiteral("u20-linkRefused-%1x%2").arg(sz.width()).arg(sz.height()));
            fact(QStringLiteral("U-20 link refused %1x%2: text").arg(sz.width()).arg(sz.height()), b->text().left(400));
            ++shots;
            b->accept();
        });
        emit visiblePane(w)->linkActivated(visiblePane(w), QUrl::fromLocalFile(ctx().work + QStringLiteral("/u20.sh")).toString());
        settle(400);
        links::setOpenerForTests(nullptr);
    }
    qputenv("QB_NO_DIALOGS", "1");
    fact(QStringLiteral("U-20 screenshots"), shots);
    check(shots >= 2, QStringLiteral("the link question and the refusal could be opened for the review"));
    writeText(ctx().dir + QStringLiteral("/usability-u20.md"), lines.join(QLatin1Char('\n')).toUtf8());
    QDir().mkpath(ctx().dir + QStringLiteral("/../usability"));
    QFile::copy(ctx().dir + QStringLiteral("/usability-u20.md"), runDir + QStringLiteral("/usability/u20-questions.md"));
    closeDialogsLater(200);
    settle(400);
    delete w;
}

} // namespace cs
