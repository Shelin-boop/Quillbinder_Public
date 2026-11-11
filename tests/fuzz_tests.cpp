// The readers against damaged input (0.80.0; the external audit of 2026-11-09, ST-5): small valid inputs of each
// format are damaged in fixed-seed ways (bits flipped, bytes changed, put in, taken out, cut off, pieces repeated)
// and fed to the reader in this process (RTF: the writer too, since 0.81.1). A reader passes when every damaged input comes back: no crash, no
// exception, and no input slower than the time limit. RTF, Markdown, XML, ZIP, DOCX and ODT (the whole file
// damaged, and the XML inside damaged and packed again), the Word 97 reader (CFB) and Scrivener projects (.scrivx).
//
//   xvfb-run -a ./qb_fuzz_tests [shots dir]
//   QB_FUZZ_SEED=<n>    another seed (printed at the start; the default is fixed)
//   QB_FUZZ_ROUNDS=<n>  damaged inputs per reader and seed input (default 60)
//
// Before each input, the input is written to <shots dir>/fuzz-current.bin with its reader and round in
// fuzz-current.txt: after a crash, those two files repeat it. tests/fuzz/README.md: longer runs with libFuzzer.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTextDocument>
#include <cstdio>
#include <exception>
#include <functional>

#include "core/Project.h"
#include "core/ProjectTypes.h"
#include "core/Zip.h"
#include "import/FileImporter.h"
#include "import/ImporterApi.h"
#include "import/MiniXml.h"
#include "import/ScrivenerFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "WordReader.h"

using namespace qb;

static int failures = 0;
static QString outDir;

static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(2000)));
    }
    std::fflush(stdout);
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

// ---------------------------------------------------------------- damage

// One damaged copy of in: one to four changes of the kinds a broken download, a bad disk or a hostile file make.
static QByteArray damage(const QByteArray &in, QRandomGenerator &r)
{
    QByteArray d = in;
    const int changes = 1 + int(r.bounded(4));
    for (int c = 0; c < changes; ++c) {
        const int n = int(d.size());
        const int at = n > 0 ? int(r.bounded(n)) : 0;
        switch (r.bounded(8)) {
        case 0: // one bit
            if (n > 0)
                d[at] = char(d.at(at) ^ char(1 << r.bounded(8)));
            break;
        case 1: // one byte, often a special one
        {
            static const char special[] = {'\0', '\\', '{', '}', '<', '>', '&', '"', '\n', '\xff', '\x80', '#', '*'};
            if (n > 0)
                d[at] = r.bounded(2) ? special[r.bounded(int(sizeof special))] : char(r.bounded(256));
            break;
        }
        case 2: // bytes put in
        {
            QByteArray ins;
            const int len = 1 + int(r.bounded(16));
            for (int i = 0; i < len; ++i)
                ins.append(char(r.bounded(256)));
            d.insert(at, ins);
            break;
        }
        case 3: // bytes taken out
            if (n > 0)
                d.remove(at, 1 + int(r.bounded(qMin(64, n - at))));
            break;
        case 4: // cut off
            d.truncate(at);
            break;
        case 5: // a piece repeated (deep nesting, long runs)
            if (n > 0) {
                const int len = 1 + int(r.bounded(qMin(48, n - at)));
                const QByteArray piece = d.mid(at, len);
                const int times = 1 + int(r.bounded(40));
                QByteArray rep;
                for (int i = 0; i < times; ++i)
                    rep += piece;
                d.insert(at, rep);
            }
            break;
        case 6: // a number made huge or negative (lengths, counts, offsets)
            for (int i = at; i < n; ++i)
                if (d.at(i) >= '0' && d.at(i) <= '9') {
                    d.insert(i, r.bounded(2) ? QByteArray("99999999999") : QByteArray("-"));
                    break;
                }
            break;
        default: // four bytes of a binary length or offset set to an extreme
            if (n >= 4) {
                const int p = int(r.bounded(n - 3));
                const quint32 v = r.bounded(2) ? 0xffffffffu : 0x7fffffffu;
                for (int i = 0; i < 4; ++i)
                    d[p + i] = char((v >> (8 * i)) & 0xff);
            }
            break;
        }
    }
    return d;
}

// ---------------------------------------------------------------- one reader

struct Run {
    QString name;
    int inputs = 0;
    int problems = 0;
    qint64 slowestMs = 0;
    QString slowest;
    QStringList notes;
};

static const qint64 kLimitMs = 5000; // one input; the valid seeds take milliseconds

// Feeds every damaged copy of every seed to read; read may return false (refusing is right), it must come back.
static Run fuzz(const QString &name, const QList<QByteArray> &seeds, int rounds, quint32 seed,
                const std::function<void(const QByteArray &)> &read)
{
    Run run;
    run.name = name;
    QRandomGenerator r(seed ^ qHash(name));
    for (int s = 0; s < seeds.size(); ++s) {
        for (int i = 0; i <= rounds; ++i) {
            const QByteArray input = i == 0 ? seeds.at(s) : damage(seeds.at(s), r); // round 0: the valid seed
            writeFile(outDir + QStringLiteral("/fuzz-current.bin"), input);
            writeFile(outDir + QStringLiteral("/fuzz-current.txt"),
                      QStringLiteral("%1 seed input %2 round %3\n").arg(name).arg(s).arg(i).toUtf8());
            QElapsedTimer t;
            t.start();
            try {
                read(input);
            } catch (const std::exception &e) {
                ++run.problems;
                run.notes << QStringLiteral("seed input %1 round %2: exception %3").arg(s).arg(i).arg(QLatin1String(e.what()));
            } catch (...) {
                ++run.problems;
                run.notes << QStringLiteral("seed input %1 round %2: exception").arg(s).arg(i);
            }
            const qint64 ms = t.elapsed();
            if (ms > run.slowestMs) {
                run.slowestMs = ms;
                run.slowest = QStringLiteral("seed input %1 round %2").arg(s).arg(i);
            }
            if (ms > kLimitMs) {
                ++run.problems;
                run.notes << QStringLiteral("seed input %1 round %2: %3 ms").arg(s).arg(i).arg(ms);
                writeFile(outDir + QStringLiteral("/fuzz-slow-%1-%2-%3.bin").arg(name).arg(s).arg(i), input);
            }
            ++run.inputs;
        }
    }
    return run;
}

static void report(const Run &run)
{
    check(run.problems == 0 && run.inputs > 0,
          QStringLiteral("%1: %2 inputs (valid and damaged) read without a crash, an exception or a hang (slowest %3 ms)")
              .arg(run.name).arg(run.inputs).arg(run.slowestMs),
          run.notes.mid(0, 12).join(QLatin1String("; ")));
}

// ---------------------------------------------------------------- seed inputs

static QByteArray zipOf(const QList<QPair<QString, QByteArray>> &files, const QString &tmp)
{
    const QString path = tmp + QStringLiteral("/pack.zip");
    QFile::remove(path);
    {
        ZipWriter z(path);
        for (const auto &f : files)
            z.addFile(f.first, f.second, f.first != QLatin1String("mimetype"));
    }
    return readFile(path);
}

static QByteArray docxXml()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
        "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Chapter One</w:t></w:r></w:p>"
        "<w:p><w:r><w:rPr><w:b/><w:i/></w:rPr><w:t xml:space=\"preserve\">The gulls </w:t></w:r>"
        "<w:r><w:t>circled the harbor &amp; the boats.</w:t></w:r><w:r><w:tab/><w:t>Tab.</w:t></w:r></w:p>"
        "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>cell</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>two</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
        "<w:p><w:r><w:br w:type=\"page\"/><w:t>After the break.</w:t></w:r></w:p>"
        "</w:body></w:document>");
}

static QByteArray docxOf(const QByteArray &documentXml, const QString &tmp)
{
    return zipOf({{QStringLiteral("[Content_Types].xml"),
                   QByteArrayLiteral("<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                                     "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                                     "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-"
                                     "officedocument.wordprocessingml.document.main+xml\"/></Types>")},
                  {QStringLiteral("_rels/.rels"),
                   QByteArrayLiteral("<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                     "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                                     "officeDocument\" Target=\"word/document.xml\"/></Relationships>")},
                  {QStringLiteral("word/document.xml"), documentXml}},
                 tmp);
}

static QByteArray odtXml()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" office:version=\"1.3\"><office:body><office:text>"
        "<text:h text:outline-level=\"1\">Chapter One</text:h>"
        "<text:p>The gulls <text:span>circled</text:span> the harbor<text:tab/>and <text:s text:c=\"3\"/>the boats.</text:p>"
        "<text:list><text:list-item><text:p>one</text:p></text:list-item><text:list-item><text:p>two</text:p></text:list-item></text:list>"
        "<table:table><table:table-row><table:table-cell><text:p>cell</text:p></table:table-cell></table:table-row></table:table>"
        "</office:text></office:body></office:document-content>");
}

static QByteArray odtOf(const QByteArray &contentXml, const QString &tmp)
{
    return zipOf({{QStringLiteral("mimetype"), QByteArrayLiteral("application/vnd.oasis.opendocument.text")},
                  {QStringLiteral("META-INF/manifest.xml"),
                   QByteArrayLiteral("<?xml version=\"1.0\"?><manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\">"
                                     "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
                                     "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>")},
                  {QStringLiteral("content.xml"), contentXml}},
                 tmp);
}

static QByteArray scrivx()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<ScrivenerProject Template=\"No\" Version=\"2.0\" Identifier=\"5A1C8A6E-6F0B-4C55-9E52-0F3C2B3F7C11\" Creator=\"SCRWIN-3.1.5.1\" Device=\"FUZZ\">\n"
        "    <Binder>\n"
        "        <BinderItem UUID=\"3B0E1D7C-6E36-4D4B-9C1F-2A0D6B8E7F10\" Type=\"DraftFolder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "            <Title>Draft</Title>\n"
        "            <Children>\n"
        "                <BinderItem UUID=\"8C2E4F61-1A2B-4C3D-8E9F-0A1B2C3D4E5F\" Type=\"Text\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "                    <Title>Scene One</Title>\n"
        "                    <MetaData><IncludeInCompile>Yes</IncludeInCompile><LabelID>7</LabelID></MetaData>\n"
        "                    <TextSettings><TextSelection>12,0</TextSelection></TextSettings>\n"
        "                </BinderItem>\n"
        "                <BinderItem UUID=\"9D3F5072-2B3C-4D4E-9FA0-1B2C3D4E5F60\" Type=\"Folder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "                    <Title>Part</Title>\n"
        "                    <Children><BinderItem UUID=\"AE406183-3C4D-4E5F-A0B1-2C3D4E5F6071\" Type=\"Text\" Created=\"2025-09-02 03:48:53 -0700\" "
        "Modified=\"2025-09-02 03:48:53 -0700\"><Title>Scene Two</Title></BinderItem></Children>\n"
        "                </BinderItem>\n"
        "            </Children>\n"
        "        </BinderItem>\n"
        "        <BinderItem UUID=\"BFCA4BFB-2E93-4067-A9AA-5505F344F096\" Type=\"ResearchFolder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\"><Title>Research</Title></BinderItem>\n"
        "        <BinderItem UUID=\"94AB659E-F7E8-4131-B1BA-C358110EA09E\" Type=\"TrashFolder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\"><Title>Trash</Title></BinderItem>\n"
        "    </Binder>\n"
        "    <LabelSettings><Title>Label</Title><DefaultLabelID>-1</DefaultLabelID><Labels><Label ID=\"-1\">No Label</Label>"
        "<Label ID=\"7\" Color=\"0.993495 0.701207 0.732587\">Red</Label></Labels></LabelSettings>\n"
        "    <StatusSettings><Title>Status</Title><DefaultStatusID>-1</DefaultStatusID><StatusItems><Status ID=\"-1\">No Status</Status></StatusItems></StatusSettings>\n"
        "</ScrivenerProject>\n");
}

static const char kRtf[] =
    "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\froman Times New Roman;}{\\f1\\fswiss Arial;}}"
    "{\\colortbl;\\red255\\green0\\blue0;}{\\stylesheet{\\s1 Heading 1;}}"
    "\\pard\\s1\\b Chapter One\\b0\\par"
    "\\pard\\fi360\\f0\\fs24 The gulls {\\i circled} the harbor \\u8212? and {\\cf1 the boats}.\\par"
    "{\\field{\\*\\fldinst HYPERLINK \"scrivcmt://ABC\"}{\\fldrslt note}}"
    "\\trowd\\cellx2000\\cellx4000 a\\cell b\\cell\\row"
    "{\\pict\\pngblip\\picw10\\pich10 89504e470d0a1a0a}"
    "\\pard\\'e4\\'f6\\'fc \\tab end\\par}";

static const char kMarkdown[] =
    "# Chapter One\n\nThe gulls *circled* the **harbor** and `the boats`.\n\n"
    "> a quote\n> - a list in it\n\n1. one\n2. two\n   - nested\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
    "[a link](http://example.org \"title\") ![picture](lens.jpg)\n\n```\ncode\n```\n\n---\n\nText with<br>html and &amp; entities.\n";

// ---------------------------------------------------------------- main

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath() + QStringLiteral("/qb-fuzz");
    QDir().mkpath(outDir);
    const quint32 seed = qEnvironmentVariableIsSet("QB_FUZZ_SEED") ? quint32(qEnvironmentVariableIntValue("QB_FUZZ_SEED")) : 80u;
    const int rounds = qEnvironmentVariableIsSet("QB_FUZZ_ROUNDS") ? qMax(1, qEnvironmentVariableIntValue("QB_FUZZ_ROUNDS")) : 60;
    std::printf("seed %u, %d damaged inputs per seed input\n", seed, rounds);
    QTemporaryDir tmp;
    const QString t = tmp.path();
    const Formatting f;

    // RTF: the reader of Scrivener's documents, notes and synopses, and (0.81.1, audit of 0.81.0, F-04) the writer on
    // what the reader made of it: a damaged file once gave a table the writer did not return from. Seed inputs: a valid
    // file, and the auditor's input of that finding (tests/fuzz/inputs).
    const QByteArray auditInput = readFile(QStringLiteral(QB_TEST_SOURCE_DIR "/fuzz/inputs/rtf-figure-in-table-row.rtf"));
    check(auditInput.size() == 2710, QStringLiteral("the RTF input of the audit of 0.81.0 (F-04) is in tests/fuzz/inputs"));
    report(fuzz(QStringLiteral("RTF"), {QByteArray(kRtf), auditInput}, rounds * 3, seed, [&](const QByteArray &in) {
        QTextDocument doc;
        rtf::read(in, &doc, f);
        (void)rtf::write(&doc, f);
        (void)rtf::toPlainText(in);
    }));

    // Markdown (import, and the writing tools' exchange)
    report(fuzz(QStringLiteral("Markdown"), {QByteArray(kMarkdown)}, rounds * 3, seed, [&](const QByteArray &in) {
        QTextDocument doc;
        md::read(QString::fromUtf8(in), &doc, f);
    }));

    // XML (the reader of .scrivx, DOCX and ODT parts)
    report(fuzz(QStringLiteral("XML"), {scrivx(), docxXml(), odtXml()}, rounds, seed, [&](const QByteArray &in) {
        xml::Document d;
        if (d.parse(in) && d.documentElement())
            (void)d.serialize();
    }));

    // ZIP (DOCX, ODT, EPUB, backups, packages)
    const QByteArray docx = docxOf(docxXml(), t);
    const QByteArray odt = odtOf(odtXml(), t);
    report(fuzz(QStringLiteral("ZIP"), {docx, odt}, rounds * 2, seed, [&](const QByteArray &in) {
        ZipReader z(in);
        if (z.isValid())
            for (const QString &name : z.fileNames()) {
                bool ok = false;
                (void)z.fileData(name, &ok);
            }
    }));

    // DOCX and ODT: the whole file damaged, then the XML inside damaged and packed again
    const QString docxPath = t + QStringLiteral("/in.docx"), odtPath = t + QStringLiteral("/in.odt");
    auto readDocx = [&](const QByteArray &in) {
        writeFile(docxPath, in);
        QTextDocument doc;
        QString err;
        importer::readDocx(docxPath, &doc, f, &err);
    };
    auto readOdt = [&](const QByteArray &in) {
        writeFile(odtPath, in);
        QTextDocument doc;
        QString err;
        importer::readOdt(odtPath, &doc, f, &err);
    };
    report(fuzz(QStringLiteral("DOCX file"), {docx}, rounds, seed, readDocx));
    report(fuzz(QStringLiteral("DOCX content"), {docxXml()}, rounds * 2, seed,
                [&](const QByteArray &in) { readDocx(docxOf(in, t)); }));
    report(fuzz(QStringLiteral("ODT file"), {odt}, rounds, seed, readOdt));
    report(fuzz(QStringLiteral("ODT content"), {odtXml()}, rounds * 2, seed,
                [&](const QByteArray &in) { readOdt(odtOf(in, t)); }));

    // Word 97 (CFB container and the Word binary inside): the sample files of tests/import
    QList<QByteArray> docs;
    for (const char *n : {"chapters.doc", "features.doc"}) {
        const QByteArray d = readFile(QStringLiteral(QB_TEST_SOURCE_DIR "/import/") + QLatin1String(n));
        if (!d.isEmpty())
            docs << d;
    }
    check(docs.size() == 2, QStringLiteral("the Word 97 samples of tests/import are there"));
    report(fuzz(QStringLiteral("Word 97"), docs, rounds, seed, [&](const QByteArray &in) {
        importers::Content c;
        QString err;
        (void)qbdoc::readWord97(in, &c, &err);
    }));

    // Scrivener projects: the .scrivx damaged in a fresh .scriv with one document
    int scrivRun = 0;
    report(fuzz(QStringLiteral("Scrivener .scrivx"), {scrivx()}, rounds, seed, [&](const QByteArray &in) {
        const QString scriv = t + QStringLiteral("/p%1.scriv").arg(scrivRun++);
        QDir().mkpath(scriv + QStringLiteral("/Files/Data/8C2E4F61-1A2B-4C3D-8E9F-0A1B2C3D4E5F"));
        writeFile(scriv + QStringLiteral("/Files/Data/8C2E4F61-1A2B-4C3D-8E9F-0A1B2C3D4E5F/content.rtf"), QByteArray(kRtf));
        writeFile(scriv + QStringLiteral("/fuzz.scrivx"), in);
        QString err;
        scrivener::OpenResult r = scrivener::open(scriv, &err);
        r.project.reset();
        QDir(scriv).removeRecursively();
    }));

    QFile::remove(outDir + QStringLiteral("/fuzz-current.bin"));
    QFile::remove(outDir + QStringLiteral("/fuzz-current.txt"));
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
