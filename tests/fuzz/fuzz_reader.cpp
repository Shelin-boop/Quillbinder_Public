// libFuzzer harness for long runs against one reader (0.80.0; the external audit of 2026-11-09, ST-5). The suite
// qb_fuzz_tests runs a fixed set of damaged inputs in every test run; this harness lets libFuzzer search for hours.
// Built only with -DQB_FUZZ=ON and clang (tests/fuzz/README.md); QB_FUZZ_TARGET picks the reader.
#include <QGuiApplication>
#include <QTextDocument>
#include <cstddef>
#include <cstdint>

#include "core/ProjectTypes.h"
#include "core/Zip.h"
#include "import/ImporterApi.h"
#include "import/MiniXml.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "WordReader.h"

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen"); // a tool of its own, not a test run: no display needed
    static QGuiApplication app(*argc, *argv);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QByteArray in(reinterpret_cast<const char *>(data), qsizetype(size));
    const qb::Formatting f;
#if QB_FUZZ_TARGET == 1
    QTextDocument doc;
    qb::rtf::read(in, &doc, f);
    (void)qb::rtf::write(&doc, f); // 0.81.1 (audit of 0.81.0, F-04): the writer on what the reader made
    (void)qb::rtf::toPlainText(in);
#elif QB_FUZZ_TARGET == 2
    QTextDocument doc;
    qb::md::read(QString::fromUtf8(in), &doc, f);
#elif QB_FUZZ_TARGET == 3
    qb::xml::Document d;
    if (d.parse(in) && d.documentElement())
        (void)d.serialize();
#elif QB_FUZZ_TARGET == 4
    qb::ZipReader z(in);
    if (z.isValid())
        for (const QString &name : z.fileNames())
            (void)z.fileData(name);
#elif QB_FUZZ_TARGET == 5
    qb::importers::Content c;
    QString err;
    (void)qbdoc::readWord97(in, &c, &err);
#else
#error "QB_FUZZ_TARGET: 1 RTF, 2 Markdown, 3 XML, 4 ZIP, 5 Word 97"
#endif
    return 0;
}
