// The system test, stage 2: component cases C-11 to C-20 (section 6.1 of the procedure).
#include "cs.h"

#include "AppInfo.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/PdfExporter.h"
#include "compile/Progress.h"
#include "core/Addons.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/SettingsFile.h"
#include "core/Zip.h"
#include "import/Compare.h"
#include "import/CompareFiles.h"
#include "import/ScrivenerFormat.h"
#include "import/FileImporter.h"
#include "import/ScrivenerImporter.h"
#include "text/AutoCorrect.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/ScriptIO.h"
#include "text/Styles.h"
#include "text/TextUtil.h"

#include <QCryptographicHash>
#include <algorithm>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <random>

namespace cs {

using namespace qb;
using namespace qb::compile;
using qb::mindmap::Board;
using qb::mindmap::Note;

namespace {

BinderItem *addDoc(Project *p, const QString &title, const QStringList &paras, BinderItem *parent = nullptr)
{
    BinderItem *it = p->model()->createItem(parent ? parent : p->draft(), -1, ItemType::Text, title);
    fillText(p, it, paras);
    p->notifyItemChanged(it);
    return it;
}

Manuscript manuscriptOf(Project *p)
{
    Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
    AuthorInfo a;
    a.forename = QStringLiteral("Test");
    a.surname = QStringLiteral("Writer");
    Compiler cc(p, f, p->compile, a);
    return cc.build();
}

QByteArray sha256Hex(const QByteArray &b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex(); }

// an add-on package made by code: manifest.json and payload/
QString makePackage(const QString &file, addons::Manifest m, const QList<QPair<QString, QByteArray>> &payload,
                    const QList<QPair<QString, QByteArray>> &extraEntries = {})
{
    m.files.clear();
    for (const auto &f : payload) {
        addons::FileEntry e;
        e.path = f.first;
        e.sha256 = sha256Hex(f.second);
        e.size = f.second.size();
        m.files << e;
    }
    ZipWriter z(file);
    z.addFile(QStringLiteral("manifest.json"), QJsonDocument(m.toJson()).toJson());
    for (const auto &f : payload)
        z.addFile(QStringLiteral("payload/") + f.first, f.second);
    for (const auto &f : extraEntries)
        z.addFile(f.first, f.second);
    z.close();
    return file;
}

} // namespace

// ================================================================ C-11 compile of the large project
void caseC11()
{
    for (const QString &fx : {QStringLiteral("fx2-qb"), QStringLiteral("fx2-scriv")}) {
        QString openErr;
        std::unique_ptr<Project> p;
        if (fx.endsWith(QLatin1String("scriv")))
            p = std::move(scrivener::open(fixtureCopy(fx), &openErr).project);
        else
            p = Project::open(fixtureCopy(fx), &openErr);
        check(p != nullptr, QStringLiteral("%1 opens").arg(fx), openErr);
        if (!p)
            continue;
        // 0.64.1: the compile reports progress; the largest gap between two reports is the longest time
        // the program's window would not be drawn (the compile window lets events through at each report)
        QElapsedTimer pt;
        pt.start();
        qint64 lastTick = 0, largestGap = 0;
        qb::compile::progress::Scope scope([&](double) {
            const qint64 n = pt.elapsed();
            largestGap = std::max(largestGap, n - lastTick);
            lastTick = n;
        });
        QElapsedTimer t;
        t.start();
        const Manuscript m = manuscriptOf(p.get());
        fact(QStringLiteral("C-11 %1: the manuscript built (ms)").arg(fx), int(t.elapsed()));
        QString err;
        t.restart();
        PdfExporter pdf;
        const bool okPdf = pdf.exportManuscript(m, ctx().dir + QStringLiteral("/c11-") + fx + QStringLiteral(".pdf"), &err);
        const qint64 pdfMs = t.elapsed();
        t.restart();
        DocxExporter docx;
        const bool okDocx = docx.exportManuscript(m, ctx().dir + QStringLiteral("/c11-") + fx + QStringLiteral(".docx"), &err);
        const qint64 docxMs = t.elapsed();
        fact(QStringLiteral("C-11 %1: PDF and Word (ms)").arg(fx), QStringLiteral("%1 / %2").arg(pdfMs).arg(docxMs));
        largestGap = std::max(largestGap, pt.elapsed() - lastTick);
        fact(QStringLiteral("C-11 %1: the largest gap between two progress reports (ms)").arg(fx), int(largestGap));
        check(largestGap <= 1000, QStringLiteral("%1: the compile reports progress at least every second (0.64.1; proposed limit)").arg(fx),
              QStringLiteral("%1 ms").arg(largestGap));
        check(okPdf && okDocx && pdfMs + docxMs < 600000, QStringLiteral("%1: PDF and Word written within 10 minutes").arg(fx), err);
    }
}

// ================================================================ C-12 packages
void caseC12()
{
    QStringList log;
    QString err;
    bool restart = false;
    // valid add-ons of the build
    const QString dist = ctx().buildDir + QStringLiteral("/addons-dist");
    int valid = 0, installed = 0;
    for (const QString &name : QDir(dist).entryList({QStringLiteral("*.qbpkg")}, QDir::Files)) {
        if (name.startsWith(QLatin1String("media-all")) || name.startsWith(QLatin1String("speech")))
            continue;   // the bundle and the large speech add-on are installed by other cases
        ++valid;
        if (addons::install(dist + QLatin1Char('/') + name, &log, &err, &restart))
            ++installed;
        else
            note(QStringLiteral("%1: %2").arg(name, err));
    }
    if (valid == 0)
        note(QStringLiteral("[A] no add-on packages built: the valid installs are left out"));
    check(installed == valid, QStringLiteral("the add-ons of the build install (%1 of %2)").arg(installed).arg(valid));
    const QString addonsRoot = addons::addonsRoot();
    auto refused = [&](const QString &what, const QString &file) {
        const addons::PackageInfo pi = addons::inspect(file);
        QString e2;
        const bool ok = addons::install(file, &log, &e2, &restart);
        check(!ok && (!pi.isValid() || !e2.isEmpty()), QStringLiteral("%1: refused with a message").arg(what), pi.error.isEmpty() ? e2 : pi.error);
    };
    // a truncated package
    if (valid > 0) {
        const QString any = dist + QLatin1Char('/') + QDir(dist).entryList({QStringLiteral("*.qbpkg")}, QDir::Files).value(0);
        QByteArray b;
        QFile f(any);
        if (f.open(QIODevice::ReadOnly))
            b = f.readAll();
        const QString cut = ctx().work + QStringLiteral("/cut.qbpkg");
        writeText(cut, b.left(b.size() / 2));
        refused(QStringLiteral("a truncated package"), cut);
    }
    addons::Manifest m;
    m.id = QStringLiteral("cs-test");
    m.name = QStringLiteral("System test");
    m.version = QStringLiteral("1.0");
    m.kind = QStringLiteral("addon");
    // a path leaving the folder
    refused(QStringLiteral("a path leaving the add-on folder (../../x)"),
            makePackage(ctx().work + QStringLiteral("/escape.qbpkg"), m, {{QStringLiteral("../../escaped.txt"), QByteArray("x")}}));
    check(!QFileInfo::exists(QFileInfo(addonsRoot).absolutePath() + QStringLiteral("/escaped.txt")), QStringLiteral("... nothing was written outside the add-on folder"));
    // another system's libraries
    addons::Manifest win = m;
    win.id = QStringLiteral("cs-windows");
    win.platform = QStringLiteral("windows-x64");
    refused(QStringLiteral("an add-on for another system"),
            makePackage(ctx().work + QStringLiteral("/windows.qbpkg"), win, {{QStringLiteral("tool/thing.dll"), QByteArray("MZ")}}));
    // a program update for another runtime, and one of a lower version
    addons::Manifest core = m;
    core.id = QStringLiteral("core");
    core.kind = QStringLiteral("core");
    core.version = QStringLiteral("0.1.0");
    core.runtime = QStringLiteral("other-runtime-r9");
    core.binary = QStringLiteral("quillbinder");
    refused(QStringLiteral("a program update for another runtime"),
            makePackage(ctx().work + QStringLiteral("/core-other.qbpkg"), core, {{QStringLiteral("quillbinder"), QByteArray("\x7f" "ELF")}}));
    core.runtime = QStringLiteral(QB_RUNTIME_ID);
    refused(QStringLiteral("a program update of a lower version"),
            makePackage(ctx().work + QStringLiteral("/core-old.qbpkg"), core, {{QStringLiteral("quillbinder"), QByteArray("\x7f" "ELF")}}));
    fact(QStringLiteral("C-12 add-ons installed"), int(addons::installedAddons().size()));
}

// ================================================================ C-13 objects data
void caseC13()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    objects::Store *st = p->objects();
    objects::Schema schema = st->schema();
    objects::Category cat;
    cat.id = QStringLiteral("cs-cat");
    cat.name = QStringLiteral("Test category");
    cat.color = QColor(0x20, 0x80, 0x60);
    int f = 0;
    for (objects::FieldType t : objects::allTypes()) {
        objects::FieldDef d;
        d.id = QStringLiteral("f%1").arg(f++);
        d.label = objects::typeLabel(t);
        d.type = t;
        if (t == objects::FieldType::Choice)
            d.choices = {QStringLiteral("one"), QStringLiteral("two")};
        cat.fields << d;
    }
    schema.categories << cat;
    st->setSchema(schema);
    QStringList ids;
    for (int i = 0; i < 50; ++i) {
        const QString id = st->create(cat.id, QString(), QStringLiteral("Object %1 äöü").arg(i));
        objects::Object o = *st->object(id);
        o.values.insert(QStringLiteral("f0"), QStringLiteral("short %1").arg(i));
        o.values.insert(QStringLiteral("f1"), atParagraphs(quint32(i), 80).join(QLatin1Char('\n')));
        o.values.insert(QStringLiteral("f4"), i % 2 == 0);
        if (!ids.isEmpty())
            o.values.insert(QStringLiteral("f7"), QStringList{ids.last()});
        st->update(o);
        ids << id;
    }
    QString err;
    check(st->save(&err), QStringLiteral("50 objects with every field kind saved"), err);
    QJsonArray before;
    for (const objects::Object *o : st->objects(cat.id))
        before << o->toJson();
    const QString index = p->indexPath();
    p.reset();
    auto q = Project::open(index, &err);
    check(q != nullptr, QStringLiteral("the project opens again"), err);
    if (!q)
        return;
    QJsonArray after;
    for (const objects::Object *o : q->objects()->objects(cat.id))
        after << o->toJson();
    check(before == after, QStringLiteral("the objects are the same after loading"), QStringLiteral("%1 / %2").arg(before.size()).arg(after.size()));
    // damaged files: one object, the schema
    const QString dir = q->objects()->dir();
    // 0.64.2: the objects are in one folder per category (objects/characters/<ID>.json); the top folder holds
    // only schema.json, so before nothing was damaged
    QString victim;
    QDirIterator oit(dir, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
    while (oit.hasNext() && victim.isEmpty()) {
        const QString f = oit.next();
        if (QFileInfo(f).fileName() != QLatin1String("schema.json"))
            victim = f;
    }
    check(!victim.isEmpty(), QStringLiteral("(an object file to damage)"), dir);
    writeText(victim, "{ not json");
    objects::Store again(q->folderPath());
    again.load(&err);
    check(again.count() >= 49, QStringLiteral("a damaged object file: the rest loads (%1 objects)").arg(again.count()), again.warnings().join(QLatin1String("; ")));
    check(!again.warnings().isEmpty() || !err.isEmpty(), QStringLiteral("... and the damage is reported"));
    writeText(dir + QStringLiteral("/schema.json"), "[broken");
    objects::Store third(q->folderPath());
    third.load(&err);
    fact(QStringLiteral("C-13 a damaged schema.json"), QStringLiteral("%1 objects, warnings: %2").arg(third.count()).arg(third.warnings().join(QLatin1String("; ")).left(200)));
}

// ================================================================ C-14 mind map data
void caseC14()
{
    Board b;
    std::mt19937 r(ctx().seed);
    for (int i = 0; i < 500; ++i) {
        Note n;
        n.id = b.nextId();
        n.pos = QPointF(r() % 4000, r() % 3000);
        n.text = QStringLiteral("Node %1 äöü").arg(i);
        n.bold = i % 7 == 0;
        b.notes << n;
        if (i > 0)
            b.connect(n.id, b.notes.at(int(r() % quint32(i))).id, i % 3 == 0);
    }
    const QString file = ctx().work + QStringLiteral("/board.qbmap");
    QString err;
    check(b.save(file, &err), QStringLiteral("500 nodes with connections saved"), err);
    Board c;
    check(c.load(file, &err) && c.notes.size() == 500, QStringLiteral("loaded again: 500 nodes"), QString::number(c.notes.size()));
    int conn = 0, conn2 = 0;
    for (const Note &n : b.notes)
        conn += int(n.connected.size());
    for (const Note &n : c.notes)
        conn2 += int(n.connected.size());
    check(conn == conn2, QStringLiteral("the connections are the same"), QStringLiteral("%1 / %2").arg(conn).arg(conn2));
    QFile in(file);
    QByteArray data;
    if (in.open(QIODevice::ReadOnly))
        data = in.readAll();
    in.close();
    writeText(file, data.left(data.size() / 3));
    Board d;
    const bool ok = d.load(file, &err);
    check(!ok || d.notes.size() < 500, QStringLiteral("a damaged map file is reported, not loaded as if whole"), err);
}

// ================================================================ C-15 project settings files
void caseC15()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    const QString file = ctx().work + QStringLiteral("/all.qbsettings");
    QString err;
    check(settingsfile::save(file, p.get(), &err), QStringLiteral("every section saved to a settings file"), err);
    QJsonObject doc;
    check(settingsfile::load(file, &doc, &err), QStringLiteral("the file loads"), err);
    const QString second = ctx().work + QStringLiteral("/second");
    QDir().mkpath(second);
    auto q = Project::create(second, QStringLiteral("Second"), &err);
    if (!q)
        return;
    const QJsonObject sections = doc.value(QStringLiteral("sections")).toObject();
    QStringList notes;
    for (auto i = sections.begin(); i != sections.end(); ++i)
        for (int s = 0; s < int(settingsfile::SectionCount); ++s)
            if (settingsfile::key(settingsfile::Section(s)) == i.key())
                settingsfile::applyData(q.get(), settingsfile::Section(s), i.value(), &notes);
    const QJsonObject captured = settingsfile::capture(q.get());
    QStringList differ;
    for (auto i = sections.begin(); i != sections.end(); ++i)
        if (!settingsfile::same(i.value(), captured.value(i.key()))) {
            // 0.64.2: which keys of the section differ; references to documents and section types the new project
            // does not have stay as they were by design, and a note says so (Load Project Settings shows it)
            QStringList keys;
            const QJsonObject a = i.value().toObject(), b = captured.value(i.key()).toObject();
            for (auto k = a.begin(); k != a.end(); ++k)
                if (!settingsfile::same(k.value(), b.value(k.key())))
                    keys << k.key();
            const bool explained = !keys.isEmpty() && notes.join(QLatin1Char(' ')).contains(QLatin1String("Compile:"))
                                   && std::all_of(keys.begin(), keys.end(), [](const QString &k) {
                                          return k == QLatin1String("group") || k == QLatin1String("front") || k == QLatin1String("back")
                                                 || k == QLatin1String("assignments");
                                      });
            fact(QStringLiteral("C-15 section %1 differs in").arg(i.key()), keys.join(QLatin1String(", ")) + (explained ? QStringLiteral(" (explained by the notes)") : QString()));
            if (!explained)
                differ << QStringLiteral("%1 (%2)").arg(i.key(), keys.join(QLatin1String(", ")));
        }
    fact(QStringLiteral("C-15 notes"), notes.join(QLatin1String(" | ")).left(400));
    check(differ.isEmpty(), QStringLiteral("applied to a new project: every section equal"), differ.join(QLatin1String(", ")));
    // damaged and foreign files
    writeText(ctx().work + QStringLiteral("/bad.qbsettings"), "{\"sections\": [1,2");
    QJsonObject d2;
    check(!settingsfile::load(ctx().work + QStringLiteral("/bad.qbsettings"), &d2, &err), QStringLiteral("a damaged settings file is refused with a message"), err);
    writeText(ctx().work + QStringLiteral("/foreign.qbsettings"), "{\"program\":\"Other\",\"data\":{}}");
    QJsonObject d3;
    const bool foreign = settingsfile::load(ctx().work + QStringLiteral("/foreign.qbsettings"), &d3, &err);
    check(!foreign || d3.value(QStringLiteral("sections")).toObject().isEmpty(), QStringLiteral("a foreign file brings nothing"), err);
}

// ================================================================ C-16 text helpers
void caseC16()
{
    const QList<QPair<QString, int>> words = {
        {QStringLiteral("The quick brown fox"), 4},
        {QStringLiteral("Straße Größe Übermaß ähnlich"), 4},
        {QStringLiteral("well-known self-made"), 2},
        {QStringLiteral("In 1999 there were 3,500 people"), 6},
        {QStringLiteral("Emoji 😀 here"), 2},
        {QStringLiteral("été café"), 2},
    };
    QStringList wrong;
    for (const auto &w : words) {
        const int n = text::countWords(w.first);
        if (n != w.second)
            wrong << QStringLiteral("\"%1\": %2 (expected %3)").arg(w.first).arg(n).arg(w.second);
    }
    check(wrong.isEmpty(), QStringLiteral("word counts: English, German, hyphens, numbers, emoji, combining accents"), wrong.join(QLatin1String("; ")));
    fact(QStringLiteral("C-16 words in Chinese and Arabic"), QStringLiteral("%1 / %2").arg(text::countWords(QStringLiteral("我们今天去公园"))).arg(text::countWords(QStringLiteral("مرحبا بالعالم الجميل"))));
    // titles from the first line
    QTextDocument d;
    QTextCursor c(&d);
    c.insertText(QString(90, QLatin1Char('x')) + QStringLiteral(" more"));
    check(text::firstLineTitle(&d).size() <= 60, QStringLiteral("a title from a long first line has at most 60 characters"));
    QTextDocument e;
    QTextCursor ce(&e);
    ce.insertText(QStringLiteral("   "));
    ce.insertBlock();
    ce.insertText(QStringLiteral("Second line"));
    fact(QStringLiteral("C-16 title of a document whose first line is only spaces"), text::firstLineTitle(&e));
    // the corrections while typing over 1,000 sentences
    autocorrect::Options o;
    o.capSentences = o.capI = o.urls = o.substitutions = true;
    o.substitutionList = {{QStringLiteral("(c)"), QStringLiteral("©")}, {QStringLiteral("teh"), QStringLiteral("the")}};
    QTextDocument t;
    QTextCursor tc(&t);
    int n = 0;
    for (const QString &para : atParagraphs(1600, 12000)) {
        for (const QString &w : para.split(QLatin1Char(' '))) {
            tc.insertText(w);
            autocorrect::afterWord(tc, o);
            tc.insertText(QStringLiteral(" "));
            ++n;
        }
        tc.insertBlock();
    }
    check(t.characterCount() > 1000, QStringLiteral("the corrections while typing over %1 words: no crash").arg(n));
}

// ================================================================ C-17 styles and formatting by hand
void caseC17()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    BinderItem *it = addDoc(p.get(), QStringLiteral("Styles"), atParagraphs(1700, 3000));
    QTextDocument *d = p->docs()->text(it->id);
    const QStringList names = styles::names(p->formatting);
    for (const QString &s : names) {
        QTextCursor c(d);
        c.setPosition(0);
        display::applyParagraphStyle(c, s, p->formatting, display::StyleOver::Paragraphs);
        QTextCursor all(d);
        all.select(QTextCursor::Document);
        display::applyParagraphStyle(all, s, p->formatting, display::StyleOver::KeepOwn);
    }
    check(true, QStringLiteral("every paragraph style applied to one and to all paragraphs (%1 styles)").arg(names.size()));
    // a word in bold by hand keeps its bold through a style change
    QTextCursor b(d);
    b.setPosition(d->begin().position());
    b.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    b.mergeCharFormat(bold);
    QTextCursor all(d);
    all.select(QTextCursor::Document);
    const QString other = names.value(names.size() > 1 ? 1 : 0);
    display::applyParagraphStyle(all, other, p->formatting, display::StyleOver::Paragraphs);
    QTextCursor chk(d);
    chk.setPosition(d->begin().position() + 1);
    check(chk.charFormat().fontWeight() >= QFont::Bold, QStringLiteral("bold set by hand stays when a style is applied"));
    // Scrivener style marks and back
    QTextDocument m;
    QTextCursor mc(&m);
    mc.insertText(QStringLiteral("<$Scr_H::1>A heading<!$Scr_H::1>"));
    mc.insertBlock();
    mc.insertText(QStringLiteral("<$Scr_Ps::0>A quote<!$Scr_Ps::0>"));
    const int marks = ScrivenerImporter::stripMarkers(&m, p->formatting, {other});
    check(!m.toPlainText().contains(QLatin1String("<$Scr")), QStringLiteral("Scrivener style marks are taken out (%1)").arg(marks), m.toPlainText());
}

// ================================================================ C-18 file importer
void caseC18()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    QStringList files;
    for (const QString &dir : {ctx().sourceDir + QStringLiteral("/tests/import"), ctx().sourceDir + QStringLiteral("/tests/media")})
        for (const QFileInfo &fi : QDir(dir).entryInfoList(QDir::Files))
            files << fi.absoluteFilePath();
    // empty files of each kind, a 50 MB text, a garbage .docx, a cut PDF, a picture with the wrong suffix
    const QString w = ctx().work + QStringLiteral("/import");
    QDir().mkpath(w);
    for (const QString &suffix : {QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("rtf"), QStringLiteral("docx"), QStringLiteral("odt"),
                                  QStringLiteral("pdf"), QStringLiteral("png"), QStringLiteral("html")}) {
        writeText(w + QStringLiteral("/empty.") + suffix, QByteArray());
        files << w + QStringLiteral("/empty.") + suffix;
    }
    {
        QFile big(w + QStringLiteral("/big.txt"));
        if (big.open(QIODevice::WriteOnly)) {
            const QByteArray line = atParagraphs(1800, 200).join(QLatin1Char(' ')).toUtf8() + '\n';
            while (big.size() < 50 * 1024 * 1024)
                big.write(line);
        }
        files << big.fileName();
    }
    writeText(w + QStringLiteral("/garbage.docx"), QByteArray("PK\x03\x04 this is no zip at all"));
    files << w + QStringLiteral("/garbage.docx");
    QStringList pdfs = QDir(ctx().sourceDir + QStringLiteral("/tests/import")).entryList({QStringLiteral("*.pdf")}, QDir::Files);
    if (!pdfs.isEmpty()) {
        QFile pf(ctx().sourceDir + QStringLiteral("/tests/import/") + pdfs.first());
        if (pf.open(QIODevice::ReadOnly))
            writeText(w + QStringLiteral("/cut.pdf"), pf.readAll().left(2000));
        files << w + QStringLiteral("/cut.pdf");
    }
    testPicture(64, 48).save(w + QStringLiteral("/picture.txt"), "PNG");
    files << w + QStringLiteral("/picture.txt");
    int done = 0;
    QStringList slow;
    for (const QString &f : files) {
        step(QStringLiteral("import %1").arg(QFileInfo(f).fileName()));
        QStringList warn;
        QElapsedTimer t;
        t.start();
        const QList<BinderItem *> got = importer::importFiles(p.get(), {f}, p->draft(), -1, &warn);
        if (t.elapsed() > 60000)
            slow << QFileInfo(f).fileName();
        if (!got.isEmpty() || !warn.isEmpty())
            ++done;
        else
            note(QStringLiteral("%1: neither imported nor reported").arg(QFileInfo(f).fileName()));
    }
    check(done == files.size(), QStringLiteral("each of %1 files is imported or reported").arg(files.size()), QString::number(done));
    check(slow.isEmpty(), QStringLiteral("no import takes more than a minute"), slow.join(QLatin1String(", ")));
}

// ================================================================ C-19 compare engine
void caseC19()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    QString err;
    const QString docx = ctx().work + QStringLiteral("/novel.docx");
    DocxExporter ex;
    if (!ex.exportManuscript(manuscriptOf(p.get()), docx, &err)) {
        check(false, QStringLiteral("(the Word file of FX-1)"), err);
        return;
    }
    compare::Result same;
    check(compare::compareWithFile(p.get(), docx, &same, &err), QStringLiteral("FX-1 compared with its own Word file"), err);
    fact(QStringLiteral("C-19 identical file: changes found"), same.changeCount());
    for (const compare::Pair &pr : same.pairs)   // 0.64.2: what they are
        if (pr.kind != compare::Pair::Matched)
            fact(QStringLiteral("C-19 a section only in the %1").arg(pr.kind == compare::Pair::OnlyInFile ? QStringLiteral("file") : QStringLiteral("project")),
                 QStringLiteral("%1 | file title %2 | %3 paragraph(s) in the file, %4 in the project; first: %5")
                     .arg(pr.title, pr.fileTitle).arg(pr.file.size()).arg(pr.project.size())
                     .arg((pr.file.isEmpty() ? (pr.project.isEmpty() ? QString() : pr.project.first().text) : pr.file.first().text).left(100)));
    for (const compare::Pair &pr : same.pairs)
        for (const compare::Change &c : pr.changes) {
            QStringList pt, ft;
            for (int k = c.pFirst; k < c.pFirst + c.pCount && k < pr.project.size(); ++k)
                pt << pr.project.at(k).text.left(80);
            for (int k = c.fFirst; k < c.fFirst + c.fCount && k < pr.file.size(); ++k)
                ft << pr.file.at(k).text.left(80);
            fact(QStringLiteral("C-19 change in %1 (kind %2, pair kind %3)").arg(pr.title).arg(int(c.kind)).arg(int(pr.kind)),
                 QStringLiteral("project: %1 | file: %2").arg(pt.join(QLatin1String(" / ")), ft.join(QLatin1String(" / "))));
        }
    // 0.64.2: the title page Compile writes (title, "by", the author) is in the file and not in the project's texts;
    // the engine shows it as a section of the file only and leaves it out of the transfer (defaultIncluded:
    // under 100 words). The check: no text differences, and nothing the transfer would take by default.
    int textChanges = 0, taken = 0;
    for (const compare::Pair &pr : same.pairs) {
        if (pr.kind == compare::Pair::Matched)
            textChanges += int(pr.changes.size());
        else if (pr.kind == compare::Pair::OnlyInProject)
            ++textChanges;
        if (compare::defaultIncluded(pr, compare::Direction::IntoProject) || compare::defaultIncluded(pr, compare::Direction::IntoFile))
            ++taken;
    }
    check(textChanges == 0 && taken == 0, QStringLiteral("no differences for the identical file (the title page of Compile is no change to take)"),
          QStringLiteral("%1 text change(s), %2 taken by default").arg(textChanges).arg(taken));
    // 50 edits in the file
    auto ft = compare::openFile(docx, p->formatting, &err);
    if (ft && ft->canWrite()) {
        std::mt19937 r(ctx().seed);
        int edits = 0;
        QSet<int> used;
        for (int i = 0; i < 200 && edits < 50 && !ft->paras.isEmpty(); ++i) {
            const int para = int(r() % quint32(ft->paras.size()));
            if (used.contains(para))
                continue;
            used.insert(para);
            const QString t = ft->paras.at(para).text;
            if (t.size() < 10)
                continue;
            const QString word = QStringLiteral("edited%1").arg(i);
            if (ft->replace(para, 0, 0, word + QLatin1Char(' '), QVector<quint8>(word.size() + 1, 0)))
                ++edits;
        }
        const QString edited = ctx().work + QStringLiteral("/edited.docx");
        ft->save(edited, &err);
        compare::Result changed;
        compare::compareWithFile(p.get(), edited, &changed, &err);
        check(changed.changeCount() > 0, QStringLiteral("the %1 edits in the file are found (%2 changes)").arg(edits).arg(changed.changeCount()));
    }
    // a file of another project, an empty file
    writeText(ctx().work + QStringLiteral("/other.md"), atParagraphs(1900, 500).join(QLatin1String("\n\n")).toUtf8());
    compare::Result other;
    compare::compareWithFile(p.get(), ctx().work + QStringLiteral("/other.md"), &other, &err);
    fact(QStringLiteral("C-19 a file of another project: related %"), other.related);
    writeText(ctx().work + QStringLiteral("/empty.md"), QByteArray());
    compare::Result empty;
    compare::compareWithFile(p.get(), ctx().work + QStringLiteral("/empty.md"), &empty, &err);
    check(true, QStringLiteral("a file of another project and an empty file: no crash"));
}

// ================================================================ C-20 script engine
void caseC20()
{
    QString fountain = QStringLiteral("Title: Test Play\nAuthor: Test Writer\n\n");
    for (int s = 0; s < 30; ++s) {
        fountain += QStringLiteral("INT. ROOM %1 - DAY\n\nA room with a window.\n\nANNA\n(quietly)\nWhere were you?\n\nBEN\nOut.\n\nCUT TO:\n\n").arg(s + 1);
    }
    const script::Screenplay sp = script::fromFountain(fountain);
    const QString back = script::toFountain(sp);
    const script::Screenplay sp2 = script::fromFountain(back);
    check(script::toFountain(sp2) == back, QStringLiteral("a 30-scene screenplay: Fountain round trip is stable"));
    const QByteArray fdx = script::toFdx(sp);
    script::Screenplay sp3;
    QString err;
    check(script::fromFdx(fdx, &sp3, &err) && script::toFountain(sp3).count(QStringLiteral("INT. ROOM")) == 30,
          QStringLiteral("Final Draft round trip keeps the 30 scenes"), err);
    // malformed input
    script::fromFountain(QStringLiteral("((((\n\n>>>\n\n~~~\n\n==="));
    script::Screenplay bad;
    script::fromFdx(QByteArray("<FinalDraft><Content><Paragraph Type=\"X\">"), &bad, &err);
    check(true, QStringLiteral("malformed Fountain and FDX return"));
}

} // namespace cs
