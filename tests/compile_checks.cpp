// Headless checks for compile part 2 (milestone M13): format settings and their
// files, style overrides, replacements, transformations, outline levels, the
// built-in PDF and EPUB writers, the additions to the Word writer. Part of
// qb_tests; compileChecks() returns the number of failed checks.
//
// Other programs used when they are installed: pdfinfo, pdftotext, pdfimages
// (poppler-utils), qpdf, python3 with pypdf (bookmarks), java with epubcheck
// (/usr/bin/epubcheck or EPUBCHECK_JAR), soffice (reads the Word file).
#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/EpubExporter.h"
#include "compile/PdfExporter.h"
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Zip.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <cstdio>

using namespace qb;
using namespace qb::compile;

namespace {

int failed = 0;
void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failed;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(1200)));
    }
}

void skip(const QString &what) { std::printf("      (skipped, program missing: %s)\n", qPrintable(what)); }

QString run(const QString &prog, const QStringList &args, int *rc = nullptr, int timeoutMs = 120000)
{
    QProcess pr;
    pr.setProcessChannelMode(QProcess::MergedChannels);
    pr.start(prog, args);
    if (!pr.waitForFinished(timeoutMs)) {
        pr.kill();
        if (rc)
            *rc = -1;
        return QString();
    }
    if (rc)
        *rc = pr.exitCode();
    return QString::fromUtf8(pr.readAll());
}

bool have(const QString &prog) { return !QStandardPaths::findExecutable(prog).isEmpty(); }

QString pdfPageText(const QString &pdf, int page)
{
    return run(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), QStringLiteral("-f"), QString::number(page), QStringLiteral("-l"),
                                             QString::number(page), pdf, QStringLiteral("-")});
}

int pdfPages(const QString &pdf)
{
    const QString info = run(QStringLiteral("pdfinfo"), {pdf});
    const auto m = QRegularExpression(QStringLiteral("Pages:\\s+(\\d+)")).match(info);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

QString longText(int n, const QString &what)
{
    QStringList s;
    for (int i = 0; i < n; ++i)
        s << QStringLiteral("%1 sentence %2 tells how the keeper climbed the stairs again, counting each step as his "
                            "father had taught him, while the lamp turned its slow white eye over the water.")
                 .arg(what)
                 .arg(i + 1);
    return s.join(QLatin1Char(' '));
}

void addPara(QTextCursor &c, const QString &text, const QString &style = QString(), bool first = false)
{
    if (!first)
        c.insertBlock();
    QTextBlockFormat bf = c.blockFormat();
    bf.setProperty(props::Style, style);
    c.setBlockFormat(bf);
    c.insertText(text, QTextCharFormat());
}

struct Sample {
    std::unique_ptr<Project> p;
    QString chapterType, sceneType;
    QStringList chapterTitles;
};

// A small novel: three chapters of two scenes, footnotes, a picture, a list,
// a block quote, a web link, own styles, a heading inside a text.
Sample makeSample(const QString &dir)
{
    Sample s;
    QString err;
    s.p = Project::create(dir, QStringLiteral("The Lighthouse"), &err);
    Project *p = s.p.get();
    s.chapterType = newId();
    s.sceneType = newId();
    p->sectionTypes = {{s.chapterType, QStringLiteral("Chapter")}, {s.sceneType, QStringLiteral("Scene")}};
    p->structureDefaults.folders = {s.chapterType};
    p->structureDefaults.files = {s.sceneType};
    p->structureDefaults.fileGroups = {s.sceneType};
    // own styles: a paragraph style "Verse" (indented, italic) and a character style "Ship Name"
    StyleSheet sh = styles::sheet(p->formatting);
    Style verse;
    verse.name = QStringLiteral("Verse");
    verse.italic = true;
    verse.para.bits = direct::Left | direct::First;
    verse.para.left = 10 * direct::kPxPerMm;
    verse.para.first = 0;
    sh.styles << verse;
    Style ship;
    ship.name = QStringLiteral("Ship Name");
    ship.character = true;
    ship.chars.caps = 1;
    sh.styles << ship;
    Style note;
    note.name = QStringLiteral("Editor Note");
    note.para.bits = direct::Align;
    note.para.align = Qt::AlignRight;
    sh.styles << note;
    p->formatting = styles::withSheet(p->formatting, sh);
    const QStringList titles = {QStringLiteral("The Storm"), QStringLiteral("The \"Mary Rose\""), QStringLiteral("Morning")};
    s.chapterTitles = titles;
    for (int c = 0; c < 3; ++c) {
        BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, titles[c]);
        for (int k = 0; k < 2; ++k) {
            BinderItem *sc = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Scene %1.%2").arg(c + 1).arg(k + 1));
            QTextDocument *d = p->docs()->text(sc->id);
            QTextCursor cur(d);
            addPara(cur, longText(5, QStringLiteral("Chapter %1 scene %2, first").arg(c + 1).arg(k + 1)), QString(), true);
            if (c == 0 && k == 0) {
                cur.insertText(QStringLiteral(" The keeper's log"), QTextCharFormat());
                cur.insertText(QStringLiteral("Kept since 1887 in a tin box under the stairs."),
                               text::noteFormat(NoteKind::InlineFootnote, QStringLiteral("f1")));
                cur.insertText(QStringLiteral(" was \"wet\" -- and ... it said <$PROJECTTITLE>.  Twice.   "), QTextCharFormat());
                QTextCharFormat it;
                it.setFontItalic(true);
                cur.insertText(QStringLiteral("softly"), it);
            }
            if (c == 1 && k == 0) {
                cur.insertText(QStringLiteral(" The "), QTextCharFormat());
                QTextCharFormat shipFmt;
                shipFmt.setProperty(props::CharStyle, QStringLiteral("Ship Name"));
                shipFmt.setFontCapitalization(QFont::SmallCaps);
                cur.insertText(QStringLiteral("Mary Rose"), shipFmt);
                cur.insertText(QStringLiteral(" came in; see "), QTextCharFormat());
                cur.insertText(QStringLiteral("the harbour pages"), text::linkFormat(QStringLiteral("https://example.org/harbour")));
                cur.insertText(QStringLiteral("."), QTextCharFormat());
                cur.insertText(QStringLiteral("Second note, about the ship."), text::noteFormat(NoteKind::InlineFootnote, QStringLiteral("f2")));
                addPara(cur, QStringLiteral("Wind on the water"), QStringLiteral("Heading 1"));
                addPara(cur, QStringLiteral("Out of the dark the bell, / over the rocks the light."), QStringLiteral("Verse"));
                addPara(cur, QStringLiteral("TODO: check the tide tables."), QStringLiteral("Editor Note"));
                addPara(cur, QStringLiteral("We came to the light of the world."), QStringLiteral("Block Quote"));
            }
            if (c == 2 && k == 1) {
                // a picture and a list
                QImage img(400, 200, QImage::Format_RGB32);
                img.fill(QColor(30, 90, 160));
                const QString imgName = p->docs()->importImage(sc->id, img); // stored with the document
                addPara(cur, QString());
                QTextImageFormat imf;
                imf.setName(imgName);
                imf.setWidth(200);
                imf.setHeight(100);
                cur.insertImage(imf);
                addPara(cur, QStringLiteral("Lamp oil"));
                QTextListFormat lf;
                lf.setStyle(QTextListFormat::ListDisc);
                cur.createList(lf);
                addPara(cur, QStringLiteral("Wicks"));
                // the list ends here
                cur.insertBlock();
                if (QTextList *l = cur.currentList())
                    l->remove(cur.block());
                QTextBlockFormat nb = cur.blockFormat();
                nb.setIndent(0);
                cur.setBlockFormat(nb);
                cur.insertText(QStringLiteral("After the list."), QTextCharFormat());
            }
            addPara(cur, longText(6, QStringLiteral("Chapter %1 scene %2, second").arg(c + 1).arg(k + 1)));
            addPara(cur, longText(4, QStringLiteral("Chapter %1 scene %2, third").arg(c + 1).arg(k + 1)));
            // the look of the styles, as the editor gives it
            display::normalizeDocument(d, p->formatting);
        }
    }
    // a picture document as cover
    BinderItem *cover = p->model()->createItem(p->research(), -1, ItemType::Image, QStringLiteral("Cover"));
    cover->fileExt = QStringLiteral("png");
    QDir().mkpath(p->contentDir(cover->id));
    QImage ci(600, 900, QImage::Format_RGB32);
    ci.fill(QColor(200, 160, 60));
    ci.save(p->mediaFilePath(cover));
    p->compile.coverId = cover->id;
    p->compile.publisher = QStringLiteral("Harbour Press");
    p->compile.subject = QStringLiteral("Lighthouses");
    p->compile.keywords = QStringLiteral("sea, lamp");
    p->compile.description = QStringLiteral("A keeper and a storm.");
    p->compile.language = QStringLiteral("en-GB");
    p->save(&err);
    return s;
}

Manuscript build(Project *p, const Format &f)
{
    AuthorInfo a;
    a.forename = QStringLiteral("Ann");
    a.surname = QStringLiteral("Keeper");
    Compiler c(p, f, p->compile, a);
    return c.build();
}

QString allText(const Manuscript &m)
{
    QStringList out;
    for (const Paragraph &p : m.paragraphs)
        out << p.plainText();
    return out.join(QLatin1Char('\n'));
}

const Paragraph *findPara(const Manuscript &m, const QString &start)
{
    for (const Paragraph &p : m.paragraphs)
        if (p.plainText().contains(start))
            return &p;
    return nullptr;
}

QString epubcheck(const QString &file, int *rc)
{
    QString jar = qEnvironmentVariable("EPUBCHECK_JAR");
    if (jar.isEmpty() && QFile::exists(QStringLiteral("/usr/bin/epubcheck")))
        jar = QStringLiteral("/usr/bin/epubcheck");
    if (jar.isEmpty() || !have(QStringLiteral("java"))) {
        *rc = -2;
        return QString();
    }
    return run(QStringLiteral("java"), {QStringLiteral("-jar"), jar, file}, rc, 300000);
}

} // namespace

// QB_TEST_KEEP=<dir>: the files of this run are copied there
void keepFile(const QString &path)
{
    const QString dir = qEnvironmentVariable("QB_TEST_KEEP");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QFile::remove(QDir(dir).filePath(QFileInfo(path).fileName()));
    QFile::copy(path, QDir(dir).filePath(QFileInfo(path).fileName()));
}

int compileChecks()
{
    failed = 0;
    QTemporaryDir tmp;
    QTemporaryDir data;
    qputenv("XDG_DATA_HOME", data.path().toUtf8()); // own formats of this run

    // ---------------------------------------------------------------- format settings and files
    {
        Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
        f.name = QStringLiteral("Test Book");
        StyleOverride o;
        o.style = QStringLiteral("Verse");
        o.mode = StyleOverride::Restyle;
        o.fontFamily = QStringLiteral("DejaVu Sans");
        o.fontSize = 10;
        o.bold = 1;
        o.align = 1;
        o.leftIndentMm = 20;
        o.prefix = QStringLiteral("\u00BB ");
        o.suffix = QStringLiteral(" \u00AB");
        o.uppercase = true;
        o.color = QColor(0x80, 0, 0);
        f.styleOverrides << o;
        f.replacements << Replacement{QStringLiteral("keeper"), QStringLiteral("warden"), false, false, true, true};
        f.transform.smartQuotes = true;
        f.toc = true;
        f.tocLevels = 3;
        f.footnotePlacement = 1;
        f.page.footerLeft = QStringLiteral("<$p>");
        f.page.pageNumberStyle = 1;
        f.page.firstPageNumber = 5;
        const Format back = Format::fromJson(f.toJson());
        const StyleOverride bo = back.styleOverrides.value(0);
        check(back.styleOverrides.size() == 1 && bo.fontFamily == QLatin1String("DejaVu Sans") && bo.bold == 1 && bo.italic == -1
                  && bo.color.rgb() == QColor(0x80, 0, 0).rgb() && bo.prefix == QStringLiteral("\u00BB ") && bo.uppercase
                  && qAbs(bo.leftIndentMm - 20) < 0.01 && bo.align == 1,
              QStringLiteral("format file: style overrides come back"),
              QStringLiteral("%1 %2 %3 %4 %5 [%6]").arg(back.styleOverrides.size()).arg(bo.fontFamily).arg(bo.bold).arg(bo.italic)
                  .arg(bo.color.name(), bo.prefix));
        check(back.replacements.size() == 1 && back.replacements[0].wholeWords && !back.replacements[0].matchCase
                  && back.transform.smartQuotes && back.toc && back.tocLevels == 3 && back.footnotePlacement == 1,
              QStringLiteral("format file: replacements, transformations, contents, notes"));
        check(back.page.footerLeft == QLatin1String("<$p>") && back.page.pageNumberStyle == 1 && back.page.firstPageNumber == 5
                  && back.page.chaptersOnRightPage && back.page.mirrorHeaders && !back.page.headerOnChapterPages,
              QStringLiteral("format file: page settings (and the paperback's book settings)"));
        const QString file = tmp.filePath(QStringLiteral("book.qbformat"));
        QString err;
        Format imported;
        check(exportFormat(f, file, &err) && importFormat(file, &imported, &err) && imported.name == QLatin1String("Test Book")
                  && imported.styleOverrides.size() == 1,
              QStringLiteral("format: export to a .qbformat file and import it again"), err);
        QFile bad(tmp.filePath(QStringLiteral("bad.qbformat")));
        bad.open(QIODevice::WriteOnly);
        bad.write("not json");
        bad.close();
        check(!importFormat(bad.fileName(), &imported, &err) && !err.isEmpty(), QStringLiteral("format: a broken file is refused with a message"));
        check(formatByName(QStringLiteral("E-book")).toc && formatByName(QStringLiteral("E-book")).name == QLatin1String("E-book"),
              QStringLiteral("built-in format \"E-book\" with a table of contents"));
    }

    Sample s = makeSample(tmp.path());
    Project *p = s.p.get();
    Format book = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
    p->compile.layoutAssignments.insert(book.name + QLatin1Char('|') + s.chapterType, QStringLiteral("Chapter with Title"));
    book.toc = true;

    // ---------------------------------------------------------------- compiler
    {
        const Manuscript m = build(p, book);
        const Paragraph *ch1 = findPara(m, QStringLiteral("Chapter One"));
        check(ch1 && ch1->outlineLevel == 1 && ch1->tocText == QLatin1String("Chapter One: The Storm") && !ch1->anchor.isEmpty(),
              QStringLiteral("outline: a two-line chapter title is one entry \"Chapter One: The Storm\""),
              ch1 ? ch1->tocText : QString());
        const Paragraph *h = findPara(m, QStringLiteral("Wind on the water"));
        check(h && h->outlineLevel == 2 && h->styleName == QLatin1String("Heading 1"),
              QStringLiteral("outline: a heading inside a chapter's text is one level below the chapter"),
              h ? QString::number(h->outlineLevel) : QString());
        QSet<QString> anchors;
        bool unique = true;
        for (const Paragraph &x : m.paragraphs) {
            if (x.anchor.isEmpty())
                continue;
            if (anchors.contains(x.anchor))
                unique = false;
            anchors.insert(x.anchor);
        }
        check(unique && anchors.size() >= 4, QStringLiteral("outline: every entry has an anchor of its own"));
        const Paragraph *v = findPara(m, QStringLiteral("Out of the dark"));
        check(v && v->styleName == QLatin1String("Verse") && (v->direct & Paragraph::DLeft) && qAbs(v->leftIndentMm - 10) < 0.2,
              QStringLiteral("own paragraph style: named and its look kept"));
        bool shipNamed = false;
        for (const Paragraph &x : m.paragraphs)
            for (const Run &r : x.runs)
                shipNamed |= r.charStyle == QLatin1String("Ship Name") && r.text == QLatin1String("Mary Rose");
        check(shipNamed, QStringLiteral("own character style: the run is named"));
        QStringList named;
        for (const NamedStyle &n : m.styles)
            named << n.name;
        check(named.contains(QStringLiteral("Verse")) && named.contains(QStringLiteral("Ship Name")),
              QStringLiteral("the manuscript lists the own styles it uses, with their look"), named.join(QLatin1String(", ")));
        check(allText(m).contains(QLatin1String("it said The Lighthouse")), QStringLiteral("placeholders in capitals: <$PROJECTTITLE>"));
        check(m.options.publisher == QLatin1String("Harbour Press") && !m.options.cover.isNull() && m.language == QLatin1String("en-GB")
                  && m.metadata.value(QStringLiteral("keywords")) == QLatin1String("sea, lamp") && m.options.toc,
              QStringLiteral("metadata of Compile > Metadata reach the manuscript (cover from the binder)"));
    }
    {
        Format f = book;
        StyleOverride gone;
        gone.style = QStringLiteral("Editor Note");
        gone.mode = StyleOverride::Remove;
        StyleOverride body;
        body.style = QStringLiteral("Verse");
        body.mode = StyleOverride::AsBody;
        StyleOverride ship;
        ship.style = QStringLiteral("Ship Name");
        ship.mode = StyleOverride::Restyle;
        ship.bold = 1;
        ship.capitals = 2;
        StyleOverride head;
        head.style = QStringLiteral("Heading 1");
        head.mode = StyleOverride::Restyle;
        head.prefix = QStringLiteral("\u00A7 ");
        head.align = 0;
        f.styleOverrides = {gone, body, ship, head};
        const Manuscript m = build(p, f);
        check(!allText(m).contains(QLatin1String("TODO: check")), QStringLiteral("style override \"leave out\": the editor's notes are gone"));
        const Paragraph *v = findPara(m, QStringLiteral("Out of the dark"));
        check(v && v->styleName.isEmpty() && !(v->direct & Paragraph::DLeft), QStringLiteral("style override \"body text\": no indent, no name"));
        bool shipBold = false;
        for (const Paragraph &x : m.paragraphs)
            for (const Run &r : x.runs)
                if (r.text == QLatin1String("Mary Rose"))
                    shipBold = r.bold && r.capitals == 2;
        check(shipBold, QStringLiteral("style override for a character style: bold, all capitals"));
        const Paragraph *h = findPara(m, QStringLiteral("Wind on the water"));
        check(h && h->plainText() == QStringLiteral("\u00A7 Wind on the water") && (h->direct & Paragraph::DAlign) && h->align == Qt::AlignLeft,
              QStringLiteral("style override for Heading 1: text before, left aligned"),
              h ? QStringLiteral("[%1] direct %2 align %3").arg(h->plainText()).arg(h->direct).arg(int(h->align)) : QString());
        // the chapter titles of the layout use "Heading 1" as well
        const Paragraph *t = findPara(m, QStringLiteral("Chapter Two"));
        check(t && t->plainText().startsWith(QStringLiteral("\u00A7 ")), QStringLiteral("style override reaches the titles of section layouts"),
              t ? QStringLiteral("[%1] style %2").arg(t->plainText()).arg(int(t->style)) : QStringLiteral("not found"));
    }
    {
        Format f = book;
        f.replacements = {Replacement{QStringLiteral("keeper"), QStringLiteral("warden"), false, false, true, true},
                          Replacement{QStringLiteral("(\\d{4})"), QStringLiteral("the year \\1"), true, true, false, true},
                          Replacement{QStringLiteral("lamp"), QStringLiteral("C:\\1"), false, true, false, true},
                          Replacement{QStringLiteral("stairs"), QStringLiteral("steps"), false, true, false, false}};
        f.transform.smartQuotes = true;
        f.transform.dashesToHyphens = false;
        f.transform.collapseSpaces = true;
        f.transform.italicsToUnderline = true;
        const Manuscript m = build(p, f);
        const QString t = allText(m);
        QString notes;
        for (const Footnote &fn : m.footnotes)
            for (const Paragraph &x : fn.paragraphs)
                notes += x.plainText() + QLatin1Char('\n');
        check(t.contains(QStringLiteral("The warden\u2019s log")) && !t.contains(QLatin1String("keeper"), Qt::CaseInsensitive),
              QStringLiteral("replacement: whole word, any case"), t.left(300));
        check(notes.contains(QLatin1String("the year 1887")), QStringLiteral("replacement: regular expression with \\1 (also in footnotes)"), notes);
        check(t.contains(QLatin1String("C:\\1")), QStringLiteral("replacement without regular expression: \\1 is text"));
        check(t.contains(QLatin1String("stairs")) && !t.contains(QLatin1String("steps")), QStringLiteral("a replacement switched off does nothing"));
        check(t.contains(QStringLiteral("was \u201Cwet\u201D")), QStringLiteral("transformation: typographic quotes"));
        check(t.contains(QLatin1String("The Lighthouse. Twice. softly")) && !t.contains(QLatin1String("Twice.   ")),
              QStringLiteral("transformation: one space, none at the end"));
        bool underlined = false;
        for (const Paragraph &x : m.paragraphs)
            for (const Run &r : x.runs)
                if (r.text == QLatin1String("softly"))
                    underlined = r.underline && !r.italic;
        check(underlined, QStringLiteral("transformation: italics become underlining"));
        Format g = book;
        g.transform.dashesToHyphens = true;
        g.transform.straightQuotes = true;
        g.transform.plainText = true;
        const Manuscript m2 = build(p, g);
        bool plain = true;
        for (const Paragraph &x : m2.paragraphs)
            for (const Run &r : x.runs)
                plain = plain && !r.bold && !r.italic && r.capitals == 0 && r.charStyle.isEmpty();
        check(plain, QStringLiteral("transformation: plain text has no character formatting"));
    }

    // ---------------------------------------------------------------- PDF
    const bool poppler = have(QStringLiteral("pdfinfo")) && have(QStringLiteral("pdftotext"));
    {
        Format f = book;
        f.page.footerCenter = QStringLiteral("<$p>");
        const Manuscript m = build(p, f);
        const QString pdf = tmp.filePath(QStringLiteral("book.pdf"));
        PdfReport rep;
        QString err;
        check(writePdf(m, pdf, &err, &rep) && rep.pages > 6, QStringLiteral("PDF: written (%1 pages)").arg(rep.pages), err);
        keepFile(pdf);
        check(rep.outline.size() >= 4 && rep.outline.first().title == QLatin1String("Chapter One: The Storm"),
              QStringLiteral("PDF: bookmarks for chapters and headings"), rep.outline.isEmpty() ? QString() : rep.outline.first().title);
        check(rep.footnotes == 2, QStringLiteral("PDF: both footnotes at the bottom of their pages"), QString::number(rep.footnotes));
        check(rep.links >= 3, QStringLiteral("PDF: the contents link to the chapters"), QString::number(rep.links));
        if (poppler) {
            const QString info = run(QStringLiteral("pdfinfo"), {pdf});
            check(info.contains(QLatin1String("432 x 648 pts")), QStringLiteral("PDF: page size 6 x 9 in"), info);
            check(info.contains(QLatin1String("Title:           The Lighthouse")) && info.contains(QLatin1String("Author:          Ann Keeper"))
                      && info.contains(QLatin1String("Subject:         Lighthouses")) && info.contains(QLatin1String("Keywords:        sea, lamp")),
                  QStringLiteral("PDF: title, author, subject and keywords"), info);
            const int pages = pdfPages(pdf);
            check(pages == rep.pages, QStringLiteral("PDF: pdfinfo counts the same pages"));
            // contents on page 1, its numbers lead to the chapters
            const QString p1 = pdfPageText(pdf, 1);
            check(p1.contains(QLatin1String("Contents")) && p1.contains(QLatin1String("Chapter One: The Storm")),
                  QStringLiteral("PDF: the contents come first"), p1.left(400));
            bool numbersRight = true;
            QStringList found;
            for (const PdfReport::Entry &e : rep.outline) {
                if (e.level > 2)
                    continue;
                const QString pageText = pdfPageText(pdf, e.page);
                const QString first = e.title.section(QLatin1Char(':'), 0, 0);
                const bool here = pageText.contains(first);
                numbersRight = numbersRight && here;
                found << QStringLiteral("%1 -> %2 %3").arg(e.title).arg(e.page).arg(here);
                // the number printed in the contents
                const auto mm = QRegularExpression(QRegularExpression::escape(e.title.left(20)) + QStringLiteral("[ .]*(\\d+)")).match(p1);
                if (e.level == 1 && mm.hasMatch())
                    numbersRight = numbersRight && mm.captured(1).toInt() == e.page;
            }
            check(numbersRight, QStringLiteral("PDF: every chapter is on the page the contents and bookmarks name"), found.join(QLatin1String("; ")));
            // chapters on right-hand pages, no header on chapter pages, facing headers
            bool odd = true, noHeader = true, facing = true;
            for (const PdfReport::Entry &e : rep.outline) {
                if (e.level != 1)
                    continue;
                odd = odd && e.page % 2 == 1;
                const QString t = pdfPageText(pdf, e.page);
                const QString top = t.section(QLatin1Char('\n'), 0, 1);
                noHeader = noHeader && !top.contains(QLatin1String("Ann Keeper")) && !top.contains(QLatin1String("The Lighthouse"));
            }
            for (int pg = 2; pg <= qMin(pages, 9); ++pg) {
                const QString t = pdfPageText(pdf, pg);
                const QString top = t.section(QLatin1Char('\n'), 0, 1);
                if (top.contains(QLatin1String("Chapter")) || top.trimmed().isEmpty() || t.contains(QLatin1String("Contents")))
                    continue;
                if (pg % 2 == 0)
                    facing = facing && top.contains(QLatin1String("Ann Keeper")) && !top.contains(QLatin1String("The Lighthouse"));
                else
                    facing = facing && top.contains(QLatin1String("The Lighthouse")) && !top.contains(QLatin1String("Ann Keeper"));
            }
            check(odd, QStringLiteral("PDF: chapters start on right-hand (odd) pages"));
            check(noHeader, QStringLiteral("PDF: no header on pages where a chapter starts"));
            check(facing, QStringLiteral("PDF: facing pages: author on left-hand pages, title on right-hand pages"));
            // footnote on the page of its reference
            bool fnOk = false;
            for (int pg = 1; pg <= pages; ++pg) {
                const QString t = pdfPageText(pdf, pg);
                if (t.contains(QLatin1String("keeper's log")))
                    fnOk = t.contains(QLatin1String("Kept since 1887 in a tin box"));
            }
            check(fnOk, QStringLiteral("PDF: the footnote is on the page of its number"));
            // page numbers in the footer
            check(pdfPageText(pdf, 4).contains(QRegularExpression(QStringLiteral("\\n\\s+4\\s*\\n"))),
                  QStringLiteral("PDF: page number in the footer"));
            // the heading inside a text is not the last line of a page
            bool keep = true;
            for (int pg = 1; pg <= pages; ++pg) {
                if (pdfPageText(pdf, pg).contains(QLatin1String("Contents")))
                    continue;
                const QStringList lines = pdfPageText(pdf, pg).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                QStringList body;
                for (const QString &l : lines)
                    if (!l.trimmed().isEmpty())
                        body << l.trimmed();
                if (body.size() >= 2 && body.at(body.size() - 2).contains(QLatin1String("Wind on the water")))
                    keep = false;
            }
            check(keep, QStringLiteral("PDF: a heading is kept with its text"));
            const QString all = run(QStringLiteral("pdftotext"), {pdf, QStringLiteral("-")});
            check(all.contains(QStringLiteral("\u2022")) && all.contains(QLatin1String("Lamp oil")), QStringLiteral("PDF: list with bullets"));
            if (have(QStringLiteral("pdfimages"))) {
                const QString imgs = run(QStringLiteral("pdfimages"), {QStringLiteral("-list"), pdf});
                check(imgs.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size() >= 3, QStringLiteral("PDF: the picture is in it"), imgs);
            }
        } else {
            skip(QStringLiteral("pdfinfo / pdftotext"));
        }
        const QByteArray raw = [&] {
            QFile f(pdf);
            f.open(QIODevice::ReadOnly);
            return f.readAll();
        }();
        check(raw.contains("/URI (https://example.org/harbour)"), QStringLiteral("PDF: the web link can be clicked"));
        check(raw.contains("/Outlines") && raw.contains("/PageMode /UseOutlines") && raw.count("%%EOF") == 2,
              QStringLiteral("PDF: bookmarks added as an update at the end of the file"));
        if (have(QStringLiteral("qpdf"))) {
            int rc = 0;
            const QString out = run(QStringLiteral("qpdf"), {QStringLiteral("--check"), pdf}, &rc);
            check(rc == 0 && out.contains(QLatin1String("No syntax or stream encoding errors")), QStringLiteral("PDF: qpdf finds no errors"), out);
        } else {
            skip(QStringLiteral("qpdf"));
        }
        if (have(QStringLiteral("python3"))) {
            int rc = 0;
            const QString out = run(QStringLiteral("python3"),
                                    {QStringLiteral("-c"),
                                     QStringLiteral("import sys\ntry:\n import pypdf\nexcept ImportError:\n print('NOPYPDF'); sys.exit(0)\n"
                                                    "r=pypdf.PdfReader(sys.argv[1])\n"
                                                    "def walk(o,d=0):\n for x in o:\n  if isinstance(x,list): walk(x,d+1)\n  else: print(d, r.get_destination_page_number(x)+1, x.title)\nwalk(r.outline)\n"),
                                     pdf},
                                    &rc);
            if (out.contains(QLatin1String("NOPYPDF")))
                skip(QStringLiteral("pypdf"));
            else
                check(rc == 0 && out.contains(QLatin1String("0 ")) && out.contains(QLatin1String("Chapter One: The Storm"))
                          && out.contains(QLatin1String("1 ")) && out.contains(QLatin1String("Wind on the water")),
                      QStringLiteral("PDF: another reader (pypdf) sees the bookmarks, nested"), out);
        }
    }
    {
        // notes at the end, roman page numbers, no contents
        Format f = book;
        f.toc = false;
        f.footnotePlacement = 2;
        f.page.footerCenter = QStringLiteral("<$p>");
        f.page.pageNumberStyle = 1;
        f.page.chaptersOnRightPage = false;
        const Manuscript m = build(p, f);
        const QString pdf = tmp.filePath(QStringLiteral("endnotes.pdf"));
        PdfReport rep;
        QString err;
        check(writePdf(m, pdf, &err, &rep) && rep.footnotes == 0, QStringLiteral("PDF: endnotes leave the page bottoms free"), err);
        if (poppler) {
            const QString all = run(QStringLiteral("pdftotext"), {pdf, QStringLiteral("-")});
            const int notes = all.lastIndexOf(QLatin1String("Notes"));
            check(notes > 0 && all.indexOf(QLatin1String("1. Kept since 1887"), notes) > notes,
                  QStringLiteral("PDF: \"Notes\" at the end of the text with the numbered notes"));
            check(pdfPageText(pdf, 2).contains(QRegularExpression(QStringLiteral("\\n\\s+ii\\s*\\n"))),
                  QStringLiteral("PDF: page numbers i, ii, iii"));
        }
    }
    {
        // a page too small for its margins
        Format f = book;
        f.page.marginLeftMm = f.page.marginRightMm = 80;
        const Manuscript m = build(p, f);
        QString err;
        check(!writePdf(m, tmp.filePath(QStringLiteral("tiny.pdf")), &err) && !err.isEmpty(), QStringLiteral("PDF: margins wider than the page: a message"));
    }

    // ---------------------------------------------------------------- EPUB
    for (int placement = 0; placement < 3; ++placement) {
        Format f = formatByName(QStringLiteral("E-book"));
        f.footnotePlacement = placement;
        f.tocPage = placement == 1;
        p->compile.layoutAssignments.insert(f.name + QLatin1Char('|') + s.chapterType, QStringLiteral("Chapter with Title"));
        const Manuscript m = build(p, f);
        const QString epub = tmp.filePath(QStringLiteral("book%1.epub").arg(placement));
        EpubReport rep;
        QString err;
        const bool written = writeEpub(m, epub, &err, &rep);
        keepFile(epub);
        check(written && rep.files >= 3 && rep.notes == 2,
              QStringLiteral("EPUB (notes %1): written, %2 files").arg(placement).arg(rep.files), err);
        int rc = 0;
        const QString out = epubcheck(epub, &rc);
        if (rc == -2)
            skip(QStringLiteral("java / epubcheck"));
        else
            check(rc == 0 && out.contains(QLatin1String("No errors or warnings detected")),
                  QStringLiteral("EPUB (notes %1): epubcheck finds no errors or warnings").arg(placement), out.right(1200));
        ZipReader z(epub);
        check(z.fileNames().value(0) == QLatin1String("mimetype") && z.fileData(QStringLiteral("mimetype")) == "application/epub+zip",
              QStringLiteral("EPUB (notes %1): mimetype first").arg(placement));
        if (placement == 0) {
            const QString opf = QString::fromUtf8(z.fileData(QStringLiteral("OEBPS/content.opf")));
            check(opf.contains(QLatin1String("<dc:title>The Lighthouse</dc:title>")) && opf.contains(QLatin1String("<dc:creator id=\"creator\">Ann Keeper"))
                      && opf.contains(QLatin1String("<dc:language>en-GB")) && opf.contains(QLatin1String("<dc:publisher>Harbour Press"))
                      && opf.contains(QLatin1String("properties=\"cover-image\"")),
                  QStringLiteral("EPUB: title, author, language, publisher, cover"));
            const QString nav = QString::fromUtf8(z.fileData(QStringLiteral("OEBPS/nav.xhtml")));
            check(nav.contains(QLatin1String("Chapter One: The Storm")) && nav.contains(QLatin1String("Wind on the water"))
                      && nav.count(QLatin1String("<ol>")) >= 2,
                  QStringLiteral("EPUB: table of contents with chapters and a heading below them"));
            QString text;
            for (const QString &n : z.fileNames())
                if (n.startsWith(QLatin1String("OEBPS/text/part")))
                    text += QString::fromUtf8(z.fileData(n));
            check(text.contains(QLatin1String("epub:type=\"noteref\"")) && text.contains(QLatin1String("epub:type=\"footnote\"")),
                  QStringLiteral("EPUB: pop-up footnotes"));
            check(text.contains(QLatin1String("class=\"s-verse\"")) && text.contains(QLatin1String("<blockquote>"))
                      && text.contains(QLatin1String("<ul>")) && text.contains(QLatin1String("<img src=\"../images/")),
                  QStringLiteral("EPUB: own style as class, block quote, list, picture"));
            check(QString::fromUtf8(z.fileData(QStringLiteral("OEBPS/styles/book.css"))).contains(QLatin1String("p.s-verse")),
                  QStringLiteral("EPUB: the style sheet has the own styles"));
            check(text.contains(QLatin1String("href=\"https://example.org/harbour\"")), QStringLiteral("EPUB: web link"));
        }
    }

    // ---------------------------------------------------------------- Word
    {
        Format f = book;
        f.toc = true;
        f.page.pageNumberStyle = 2;
        f.page.footerLeft = QStringLiteral("<$p>");
        const Manuscript m = build(p, f);
        const QString docx = tmp.filePath(QStringLiteral("book.docx"));
        DocxExporter dx;
        QString err;
        check(dx.exportManuscript(m, docx, &err), QStringLiteral("Word: written"), err);
        keepFile(docx);
        ZipReader z(docx);
        bool wellFormed = true;
        QStringList bad;
        for (const QString &n : z.fileNames()) {
            if (!n.endsWith(QLatin1String(".xml")) && !n.endsWith(QLatin1String(".rels")))
                continue;
            QDomDocument d;
            if (!d.setContent(z.fileData(n))) {
                wellFormed = false;
                bad << n;
            }
        }
        check(wellFormed, QStringLiteral("Word: every part is well-formed XML"), bad.join(QLatin1String(", ")));
        const QString doc = QString::fromUtf8(z.fileData(QStringLiteral("word/document.xml")));
        const QString sty = QString::fromUtf8(z.fileData(QStringLiteral("word/styles.xml")));
        check(sty.contains(QLatin1String("w:styleId=\"OwnVerse\"")) && sty.contains(QLatin1String("<w:name w:val=\"Ship Name\"/>"))
                  && doc.contains(QLatin1String("<w:pStyle w:val=\"OwnVerse\"/>")) && doc.contains(QLatin1String("<w:rStyle w:val=\"OwnShipName\"/>")),
              QStringLiteral("Word: own styles are named styles"));
        check(doc.contains(QLatin1String("TOC \\o \"1-2\"")) && QString::fromUtf8(z.fileData(QStringLiteral("word/settings.xml"))).contains(QLatin1String("updateFields")),
              QStringLiteral("Word: table of contents as a field Word fills in"));
        check(z.fileNames().contains(QStringLiteral("word/header2.xml")) && doc.contains(QLatin1String("w:type=\"even\""))
                  && QString::fromUtf8(z.fileData(QStringLiteral("word/settings.xml"))).contains(QLatin1String("evenAndOddHeaders")),
              QStringLiteral("Word: facing pages have headers of their own"));
        check(doc.contains(QLatin1String("w:fmt=\"upperRoman\"")), QStringLiteral("Word: page numbers I, II, III"));
        check(QString::fromUtf8(z.fileData(QStringLiteral("docProps/core.xml"))).contains(QLatin1String("<cp:keywords>sea, lamp</cp:keywords>")),
              QStringLiteral("Word: keywords and subject in the document properties"));
        if (have(QStringLiteral("soffice"))) {
            QTemporaryDir lo;
            int rc = 0;
            run(QStringLiteral("soffice"), {QStringLiteral("-env:UserInstallation=file://") + lo.filePath(QStringLiteral("profile")),
                                            QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("txt:Text"),
                                            QStringLiteral("--outdir"), lo.path(), docx},
                &rc, 240000);
            QFile t(lo.filePath(QStringLiteral("book.txt")));
            const QString txt = t.open(QIODevice::ReadOnly) ? QString::fromUtf8(t.readAll()) : QString();
            check(txt.contains(QLatin1String("Chapter One")) && txt.contains(QLatin1String("Out of the dark the bell")),
                  QStringLiteral("Word: LibreOffice opens the file and reads its text"), txt.left(300));
        } else {
            skip(QStringLiteral("soffice"));
        }
    }

    // ---------------------------------------------------------------- the list of outputs
    {
        PluginManager::instance().load();
        QStringList ids;
        for (ExporterInterface *e : PluginManager::instance().exporters())
            ids << e->id();
        check(ids.contains(QStringLiteral("pdf")) && ids.contains(QStringLiteral("epub")) && ids.indexOf(QStringLiteral("pdf")) < 3,
              QStringLiteral("Compile for: built-in PDF and e-book next to Word"), ids.join(QLatin1String(", ")));
    }
    qunsetenv("XDG_DATA_HOME");
    return failed;
}
