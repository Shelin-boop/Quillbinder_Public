// Scripted run for milestone M14 (printing): File > Print Settings, File > Print
// Preview and File > Print Current Document for texts (single documents,
// Combined text, a selection, pages from..to), the index cards of the corkboard,
// the outliner, pictures, PDF files and web pages. Every printout goes into a
// PDF file (the printer's "Print to File") and is checked with poppler's tools
// (pdfinfo, pdftotext, pdffonts, pdfimages, pdftoppm); the pages are saved as
// pictures next to the screenshots in the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_m14_tests /tmp/shots
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/PrintSettings.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/Outliner.h"
#include "ui/PrintDialogs.h"
#include "ui/Printing.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDate>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <cstdio>
#include <functional>

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

static void info(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
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

static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *byKey(QWidget *w, const QString &key) { return w->findChild<QAction *>(key); }

template<typename T>
static void withModal(int delayMs, std::function<void(T *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        if (auto *m = qobject_cast<T *>(QApplication::activeModalWidget())) {
            fn(m);
            return;
        }
        info(QStringLiteral("(the expected window (%1) did not open)").arg(QString::fromLatin1(T::staticMetaObject.className())));
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

// ---------------------------------------------------------------- PDF tools (poppler-utils)

static QString run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(60000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}
static QString pdfText(const QString &pdf, int page = 0, bool layout = false)
{
    QStringList a;
    if (layout)
        a << QStringLiteral("-layout");
    if (page > 0)
        a << QStringLiteral("-f") << QString::number(page) << QStringLiteral("-l") << QString::number(page);
    return run(QStringLiteral("pdftotext"), a << pdf << QStringLiteral("-"));
}
static int pdfPages(const QString &pdf)
{
    static const QRegularExpression re(QStringLiteral("Pages:\\s+(\\d+)"));
    return re.match(run(QStringLiteral("pdfinfo"), {pdf})).captured(1).toInt();
}
static QSizeF pdfPageSize(const QString &pdf)
{
    static const QRegularExpression re(QStringLiteral("Page size:\\s+([\\d.]+) x ([\\d.]+) pts"));
    const auto m = re.match(run(QStringLiteral("pdfinfo"), {pdf}));
    return QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble());
}
static int pdfImageCount(const QString &pdf)
{
    const QStringList lines = run(QStringLiteral("pdfimages"), {QStringLiteral("-list"), pdf}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return qMax(0, int(lines.size()) - 2);
}
static QString pdfFonts(const QString &pdf) { return run(QStringLiteral("pdffonts"), {pdf}); }
// the pages as pictures next to the screenshots (looked at by hand)
static void pdfShots(const QString &pdf, const QString &name, int pages = 2)
{
    run(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("50"), QStringLiteral("-f"),
                                    QStringLiteral("1"), QStringLiteral("-l"), QString::number(pages), pdf,
                                    outDir + QLatin1Char('/') + name});
}
static QString squeeze(const QString &s) { return QString(s).simplified(); }

// ---------------------------------------------------------------- helpers

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *p : w.findChildren<EditorPane *>())
        if (p->isVisible())
            return p;
    return nullptr;
}

static void focusOn(MainWindow &w, QWidget *e)
{
    w.activateWindow();
    QApplication::setActiveWindow(&w);
    if (e)
        e->setFocus();
    settle(100);
}

static TextEditor *editorShowing(MainWindow &w, const QString &needle)
{
    for (TextEditor *e : w.findChildren<TextEditor *>())
        if (e->isVisible() && !e->isReadOnly() && e->item() && e->toPlainText().contains(needle))
            return e;
    return nullptr;
}

// Prints what the editor shows into a PDF file, the way File > Print Current
// Document does after the print window (range: 0 = all, -1 = the selection).
static bool printToPdf(MainWindow &w, const QString &pdf, const PrintSettings &s, int from = 0, int to = 0, int *pages = nullptr,
                       QString *error = nullptr)
{
    EditorPane *pane = visiblePane(w);
    auto *focus = qobject_cast<TextEditor *>(QApplication::focusWidget());
    const printing::Job job = printing::currentJob(pane, focus);
    QPrinter printer(QPrinter::HighResolution);
    printing::preparePrinter(&printer, job, s);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdf);
    if (from < 0) {
        printer.setPrintRange(QPrinter::Selection);
    } else if (from > 0) {
        printer.setPrintRange(QPrinter::PageRange);
        printer.setFromTo(from, to);
    }
    QFile::remove(pdf);
    QString err;
    const bool ok = printing::render(job, s, &printer, &err, pages);
    if (error)
        *error = err;
    return ok && QFileInfo(pdf).size() > 0;
}

static QString markdownDoc(Project *p, BinderItem *parent, const QString &title, const QString &md, BinderItem **out = nullptr)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    templates::setMarkdown(p, it, md);
    if (out)
        *out = it;
    return it->id;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderPrintTest"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderPrintTest"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("DejaVu Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.forename = QStringLiteral("Ada");
    as.surname = QStringLiteral("Keeper");
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    const QString pdfDir = home.path() + QLatin1String("/pdf");
    QDir().mkpath(pdfDir);
    auto pdfPath = [&pdfDir](const QString &name) { return pdfDir + QLatin1Char('/') + name + QLatin1String(".pdf"); };

    // ------------------------------------------------------------ the project
    QTemporaryDir work;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("The Lighthouse"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    BinderModel *m = p->model();
    for (BinderItem *c : QList<BinderItem *>(p->draft()->children))
        m->deletePermanently(c);
    BinderItem *ch1 = m->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter One"));
    BinderItem *storm = nullptr, *calm = nullptr, *ships = nullptr, *longDoc = nullptr;
    markdownDoc(p, ch1, QStringLiteral("The Storm"),
                QStringLiteral("# Night Watch\n"
                               "The wind rose in the night and the sea came up over the rocks below the lamp.^[The lamp was "
                               "lit at dusk by the keeper himself.]\n"
                               "The keeper climbed the {==ninety-nine steps==}{>>Check the number of steps.<<} twice before "
                               "midnight.\n"
                               "He wrote **storm** in the book and *waited*."),
                &storm);
    markdownDoc(p, ch1, QStringLiteral("The Calm"),
                QStringLiteral("By morning the boats were gone and the keeper counted the gulls on the rail.\n"
                               "A single sail stood far out on the grey water."),
                &calm);
    markdownDoc(p, ch1, QStringLiteral("The Ships"),
                QStringLiteral("Three ships passed the reef at noon; the keeper wrote their names in the book."), &ships);
    {
        QString text;
        for (int i = 1; i <= 120; ++i)
            text += QStringLiteral("Paragraph %1 of the long log: the light turned, the fog came and went, and the keeper "
                                   "kept writing in the book of the lighthouse.\n")
                        .arg(i);
        markdownDoc(p, p->draft(), QStringLiteral("The Long Log"), text, &longDoc);
    }
    storm->synopsis = QStringLiteral("The storm reaches the lighthouse.");
    calm->synopsis = QStringLiteral("Morning after the storm.");
    p->docs()->notes(storm->id)->setPlainText(QStringLiteral("Remember the lamp oil."));
    // labels, status, keywords for the cards and the outliner
    if (!p->labels.isEmpty())
        storm->label = p->labels.first().id;
    if (!p->statuses.isEmpty())
        storm->status = p->statuses.first().id;
    p->keywords.append({QStringLiteral("kw-sea"), QStringLiteral("Sea"), QColor(40, 120, 200), QString()});
    storm->keywords << QStringLiteral("kw-sea");
    // a folder of cards
    BinderItem *cardsFolder = m->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Cards"));
    QList<BinderItem *> cardItems;
    for (int i = 1; i <= 8; ++i) {
        BinderItem *c = nullptr;
        markdownDoc(p, cardsFolder, QStringLiteral("Card %1").arg(i), QStringLiteral("Text of card %1.").arg(i), &c);
        c->synopsis = QStringLiteral("Synopsis of card number %1, which tells what happens.").arg(i);
        cardItems << c;
    }
    cardItems[1]->synopsis.clear(); // shows the first lines of its text
    // a closed group for the outliner
    BinderItem *inner = m->createItem(ch1, -1, ItemType::Folder, QStringLiteral("Inner Group"));
    markdownDoc(p, inner, QStringLiteral("Hidden Scene"), QStringLiteral("Only seen when closed groups are printed."));
    // research: a picture, a PDF of two pages, a web page, an audio file
    BinderItem *pic = m->createItem(p->research(), -1, ItemType::Image, QStringLiteral("Lighthouse Picture"));
    {
        QImage img(600, 400, QImage::Format_RGB32);
        img.fill(QColor(40, 90, 160));
        QPainter pp(&img);
        pp.fillRect(250, 80, 100, 300, QColor(240, 240, 240));
        pp.end();
        pic->fileExt = QStringLiteral("png");
        QDir().mkpath(p->contentDir(pic->id));
        img.save(p->mediaFilePath(pic));
    }
    BinderItem *pdfItem = m->createItem(p->research(), -1, ItemType::Pdf, QStringLiteral("Tide Tables"));
    {
        pdfItem->fileExt = QStringLiteral("pdf");
        QDir().mkpath(p->contentDir(pdfItem->id));
        QPdfWriter pw(p->mediaFilePath(pdfItem));
        pw.setPageSize(QPageSize(QPageSize::A5));
        QPainter pp(&pw);
        pp.setFont(QFont(QStringLiteral("DejaVu Sans"), 20));
        pp.drawText(QPointF(400, 800), QStringLiteral("Tide table page one"));
        pw.newPage();
        pp.drawText(QPointF(400, 800), QStringLiteral("Tide table page two"));
        pp.end();
    }
    BinderItem *web = m->createItem(p->research(), -1, ItemType::WebPage, QStringLiteral("Lighthouse History"));
    {
        web->fileExt = QStringLiteral("html");
        QDir().mkpath(p->contentDir(web->id));
        QFile f(p->mediaFilePath(web));
        f.open(QIODevice::WriteOnly);
        f.write("<html><body><h1>History of the Lighthouse</h1><p>The tower was built of granite blocks.</p></body></html>");
    }
    BinderItem *audio = m->createItem(p->research(), -1, ItemType::Media, QStringLiteral("Foghorn"));
    audio->fileExt = QStringLiteral("ogg");
    BinderItem *emptyFolder = m->createItem(p->research(), -1, ItemType::Folder, QStringLiteral("Empty Folder"));
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);

    // ============================================================ menus without a project
    {
        QStringList fileEntries;
        QMenu *file = nullptr;
        for (QAction *a : w.menuBar()->actions())
            if (a->menu() && a->text().remove(QLatin1Char('&')) == QLatin1String("File"))
                file = a->menu();
        if (file)
            for (QAction *a : file->actions())
                if (!a->isSeparator())
                    fileEntries << a->text().remove(QLatin1Char('&'));
        const int iSettings = int(fileEntries.indexOf(QStringLiteral("Print Settings…")));
        const int iPreview = int(fileEntries.indexOf(QStringLiteral("Print Preview…")));
        const int iPrint = int(fileEntries.indexOf(QStringLiteral("Print Current Document…")));
        check(iSettings >= 0 && iPreview == iSettings + 1 && iPrint == iPreview + 1,
              QStringLiteral("File menu: Print Settings…, Print Preview…, Print Current Document… (in this order, after Compile)"),
              fileEntries.join(QLatin1String(" | ")));
        check(byKey(&w, QStringLiteral("print"))->shortcut() == QKeySequence(QKeySequence::Print),
              QStringLiteral("Print Current Document has Ctrl+P"));
        check(!byKey(&w, QStringLiteral("print"))->isEnabled() && !byKey(&w, QStringLiteral("printPreview"))->isEnabled()
                  && !byKey(&w, QStringLiteral("printSettings"))->isEnabled(),
              QStringLiteral("without a project the three print commands are off"));
    }

    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(400);
    auto *binder = w.findChild<BinderPanel *>();
    EditorPane *pane = visiblePane(w);
    check(binder && pane, QStringLiteral("(window parts found)"));
    if (!binder || !pane)
        return 1;
    auto show = [&](const QList<BinderItem *> &items) {
        binder->select(items, true);
        settle(350);
    };
    PrintSettings base = PrintSettings::ofProject(p);
    base.page.paper = QStringLiteral("A4");
    base.page.width = 210;
    base.page.height = 297;
    base.page.landscape = false;
    base.page.left = base.page.right = base.page.top = base.page.bottom = 20;

    // ============================================================ texts
    {
        show({storm});
        TextEditor *e = editorShowing(w, QStringLiteral("wind rose"));
        focusOn(w, e);
        check(byKey(&w, QStringLiteral("print"))->isEnabled() && byKey(&w, QStringLiteral("printPreview"))->isEnabled()
                  && byKey(&w, QStringLiteral("printSettings"))->isEnabled(),
              QStringLiteral("a text in the editor: Print Current Document, Print Preview and Print Settings are on"));
        const printing::Job job = printing::currentJob(pane, e);
        check(job.kind == printing::Kind::Text && job.items == QList<BinderItem *>{storm} && job.title == QLatin1String("The Storm"),
              QStringLiteral("what is printed: the text of the document shown"));

        const QString pdf = pdfPath(QStringLiteral("storm"));
        int pages = 0;
        QString why;
        const bool ok = printToPdf(w, pdf, base, 0, 0, &pages, &why);
        const QString text = pdfText(pdf, 0, true);
        info(QStringLiteral("storm.pdf: %1 page(s)").arg(pdfPages(pdf)));
        check(ok && pdfPages(pdf) == 1 && pages == 1, QStringLiteral("a short text prints on one page"), why);
        check(text.contains(QLatin1String("The Storm")) && text.contains(QLatin1String("Night Watch"))
                  && text.contains(QLatin1String("The wind rose in the night")),
              QStringLiteral("... with the document's title, its heading and its text"), text.left(800));
        // header: <$title> left, <$projecttitle> right; footer: the page number
        QStringList lines;
        for (const QString &l : text.split(QLatin1Char('\n')))
            if (!l.simplified().isEmpty())
                lines << l;
        const QString firstLine = lines.value(0), lastLine = lines.isEmpty() ? QString() : lines.last();
        check(firstLine.contains(QLatin1String("The Storm")) && firstLine.contains(QLatin1String("The Lighthouse")),
              QStringLiteral("... the header: the document's title and the project's title"), firstLine);
        check(squeeze(lastLine) == QLatin1String("1"), QStringLiteral("... the footer: the page number"), lastLine);
        // the footnote at the bottom of the page, after the text
        const int fnAt = int(text.indexOf(QLatin1String("The lamp was lit at dusk")));
        const int textAt = int(text.indexOf(QLatin1String("He wrote storm")));
        check(fnAt > textAt && textAt > 0, QStringLiteral("... the footnote is at the bottom of the page, below the text"),
              QStringLiteral("text at %1, note at %2").arg(textAt).arg(fnAt));
        check(!text.contains(QLatin1String("Check the number of steps")),
              QStringLiteral("... comments are left out (the standard setting)"));
        check(pdfPageSize(pdf).width() > 594 && pdfPageSize(pdf).width() < 597, QStringLiteral("... on A4 paper (595 × 842 pt)"),
              QStringLiteral("%1 × %2").arg(pdfPageSize(pdf).width()).arg(pdfPageSize(pdf).height()));
        pdfShots(pdf, QStringLiteral("m14-01-text"), 1);

        // comments as notes, synopsis and document notes
        PrintSettings s = base;
        s.comments = true;
        s.synopses = true;
        s.notes = true;
        printToPdf(w, pdf, s);
        const QString t2 = pdfText(pdf);
        check(t2.contains(QLatin1String("Comment: Check the number of steps.")),
              QStringLiteral("Print Settings > Text: comments as notes at the bottom of the page"), t2.left(1200));
        check(t2.contains(QLatin1String("The storm reaches the lighthouse.")) && t2.contains(QLatin1String("Remember the lamp oil.")),
              QStringLiteral("... the synopsis below the title and the document notes after the text"));
        // footnotes at the end
        s = base;
        s.endnotes = true;
        printToPdf(w, pdf, s);
        const QString t3 = pdfText(pdf);
        check(t3.indexOf(QLatin1String("Notes")) > t3.indexOf(QLatin1String("He wrote storm"))
                  && t3.indexOf(QLatin1String("The lamp was lit")) > t3.indexOf(QLatin1String("Notes")),
              QStringLiteral("... footnotes at the end of the text under \"Notes\""), t3.left(1200));
        // no titles
        s = base;
        s.titles = false;
        s.headerLeft.clear();
        printToPdf(w, pdf, s);
        check(!pdfText(pdf).contains(QLatin1String("The Storm")), QStringLiteral("... without titles (and an empty header) the title is not printed"));
        // one font, double spacing
        s = base;
        s.editorFont = false;
        s.fontFamily = QStringLiteral("DejaVu Serif");
        s.fontSize = 14;
        printToPdf(w, pdf, s);
        const QString fonts = pdfFonts(pdf);
        check(fonts.contains(QLatin1String("DejaVuSerif")), QStringLiteral("... one font for the text (DejaVu Serif)"), fonts);
    }

    // ---- a long text: pages, page range, line spacing
    {
        show({longDoc});
        const QString pdf = pdfPath(QStringLiteral("long"));
        PrintSettings s = base;
        s.footerCenter = QStringLiteral("Page <$p> of <$pages>");
        int pages = 0;
        printToPdf(w, pdf, s, 0, 0, &pages);
        const int all = pdfPages(pdf);
        info(QStringLiteral("long.pdf: %1 pages").arg(all));
        check(all >= 3 && pages == all, QStringLiteral("a long text runs over several pages"), QString::number(all));
        check(pdfText(pdf, 2).contains(QStringLiteral("Page 2 of %1").arg(all)), QStringLiteral("... <$p> and <$pages> in the footer"),
              pdfText(pdf, 2).right(200));
        printToPdf(w, pdf, s, 2, 2);
        check(pdfPages(pdf) == 1 && pdfText(pdf).contains(QStringLiteral("Page 2 of %1").arg(all)),
              QStringLiteral("... pages 2 to 2 print page 2 alone"));
        s.lineSpacing = 2.0;
        printToPdf(w, pdf, s);
        check(pdfPages(pdf) > all, QStringLiteral("... double line spacing takes more pages (room for corrections)"),
              QStringLiteral("%1 > %2").arg(pdfPages(pdf)).arg(all));
        pdfShots(pdf, QStringLiteral("m14-02-long-double"), 1);
    }

    // ---- Combined text: every document with its title
    {
        show({ch1});
        if (pane->mode() != ViewMode::Combined)
            pane->setViewMode(ViewMode::Combined);
        settle(300);
        const QString pdf = pdfPath(QStringLiteral("combined-text"));
        printToPdf(w, pdf, base);
        const QString t = pdfText(pdf);
        const int a = int(t.indexOf(QLatin1String("The Storm"))), b = int(t.indexOf(QLatin1String("The Calm"))),
                  c = int(t.indexOf(QLatin1String("The Ships")));
        check(pane->mode() == ViewMode::Combined && a >= 0 && a < b && b < c && t.contains(QLatin1String("Hidden Scene")),
              QStringLiteral("Combined text: every text of the group, each with its title, in the binder's order"), t.left(600));
        check(t.contains(QLatin1String("Chapter One")), QStringLiteral("... the header names the group (<$title>)"));
        pdfShots(pdf, QStringLiteral("m14-03-combined-text"), 1);
    }

    // ---- a selection of text
    {
        show({calm});
        TextEditor *e = editorShowing(w, QStringLiteral("counted the gulls"));
        QString t;
        if (e) {
            focusOn(w, e);
            QTextCursor c = e->textCursor();
            const int at = int(e->toPlainText().indexOf(QLatin1String("A single sail")));
            c.setPosition(at);
            c.setPosition(at + int(QStringLiteral("A single sail stood far out").size()), QTextCursor::KeepAnchor);
            e->setTextCursor(c);
            settle(100);
            const QString pdf = pdfPath(QStringLiteral("selection"));
            printToPdf(w, pdf, base, -1);
            t = pdfText(pdf);
        }
        check(t.contains(QLatin1String("A single sail stood far out")) && !t.contains(QLatin1String("counted the gulls"))
                  && !t.contains(QLatin1String("grey water")),
              QStringLiteral("\"Selection\" prints the selected text alone"), t);
        // the print window offers "Selection" only with a selection
        bool offered = false, notOffered = false;
        withModal<QPrintDialog>(500, [&offered](QPrintDialog *d) {
            offered = d->testOption(QAbstractPrintDialog::PrintSelection) && d->testOption(QAbstractPrintDialog::PrintPageRange);
            d->grab().save(outDir + QLatin1String("/m14-04-print-window.png"));
            d->reject();
        });
        byKey(&w, QStringLiteral("print"))->trigger();
        settle(300);
        if (e) {
            QTextCursor c = e->textCursor();
            c.clearSelection();
            e->setTextCursor(c);
            focusOn(w, e);
        }
        withModal<QPrintDialog>(500, [&notOffered](QPrintDialog *d) {
            notOffered = !d->testOption(QAbstractPrintDialog::PrintSelection);
            d->reject();
        });
        byKey(&w, QStringLiteral("print"))->trigger();
        settle(300);
        check(offered && notOffered,
              QStringLiteral("the print window (File > Print Current Document) offers \"Selection\" while text is selected, and pages from..to"));
    }

    // ---- the print window itself: "Print to File" through the dialog's own fields
    {
        show({ships});
        TextEditor *e = editorShowing(w, QStringLiteral("Three ships"));
        focusOn(w, e);
        const QString pdf = pdfPath(QStringLiteral("dialog"));
        QFile::remove(pdf);
        QString seen;
        withModal<QPrintDialog>(500, [&seen, pdf](QPrintDialog *d) {
            // without a printer of the system the window prints to a file; its name goes into the window's field
            for (QComboBox *cb : d->findChildren<QComboBox *>())
                if (cb->objectName() == QLatin1String("printers"))
                    seen = QStringLiteral("printers: ") + QString::number(cb->count()) + QLatin1Char(' ') + cb->currentText();
            if (auto *fn = d->findChild<QLineEdit *>(QStringLiteral("filename")))
                fn->setText(pdf);
            d->printer()->setOutputFileName(pdf);
            d->accept();
        });
        byKey(&w, QStringLiteral("print"))->trigger();
        settle(800);
        info(seen);
        check(QFileInfo::exists(pdf) && pdfText(pdf).contains(QLatin1String("Three ships passed the reef")),
              QStringLiteral("File > Print Current Document through the print window (Print to File) gives the text"), seen);
    }

    // ============================================================ index cards
    {
        show({cardsFolder});
        pane->setViewMode(ViewMode::Corkboard);
        settle(400);
        check(pane->mode() == ViewMode::Corkboard && byKey(&w, QStringLiteral("print"))->isEnabled(),
              QStringLiteral("a corkboard with cards: Print Current Document is on"));
        const printing::Job job = printing::currentJob(pane, nullptr);
        check(job.kind == printing::Kind::Cards && job.items == cardItems,
              QStringLiteral("what is printed: the cards of the corkboard in their order"));
        const QString pdf = pdfPath(QStringLiteral("cards"));
        PrintSettings s = base; // 3 × 5 in cards on A4 with 20 mm margins: one column, three rows
        int pages = 0;
        printToPdf(w, pdf, s, 0, 0, &pages);
        const QString t = pdfText(pdf);
        bool allTitles = true;
        for (BinderItem *c : cardItems)
            allTitles = allTitles && t.contains(c->title);
        check(pdfPages(pdf) == 3 && pages == 3, QStringLiteral("8 cards of 3 × 5 in on A4: three on a sheet, three sheets"),
              QString::number(pdfPages(pdf)));
        check(allTitles && t.contains(QLatin1String("Synopsis of card number 1")) && t.contains(QLatin1String("Text of card 2.")),
              QStringLiteral("... every card with its title and synopsis (the first lines of the text when there is none)"), t.left(900));
        pdfShots(pdf, QStringLiteral("m14-05-cards"), 1);
        // landscape: two cards on a sheet
        s.page.landscape = true;
        printToPdf(w, pdf, s);
        check(pdfPages(pdf) == 4, QStringLiteral("... landscape A4: two cards on a sheet (4 sheets)"), QString::number(pdfPages(pdf)));
        // one card on each sheet: the sheet is as large as the card
        s = base;
        s.cardPerPage = true;
        printToPdf(w, pdf, s);
        const QSizeF size = pdfPageSize(pdf);
        check(pdfPages(pdf) == 8 && qAbs(size.width() - 360) < 1.5 && qAbs(size.height() - 216) < 1.5,
              QStringLiteral("... one card on each sheet: 8 sheets of 3 × 5 in (360 × 216 pt)"),
              QStringLiteral("%1 pages, %2 × %3").arg(pdfPages(pdf)).arg(size.width()).arg(size.height()));
        pdfShots(pdf, QStringLiteral("m14-06-card-per-sheet"), 1);
        // only the selected cards
        pane->corkboard()->selectItems({cardItems[2], cardItems[5]});
        settle(200);
        printToPdf(w, pdf, base, -1);
        const QString ts = pdfText(pdf);
        check(pdfPages(pdf) == 1 && ts.contains(QLatin1String("Card 3")) && ts.contains(QLatin1String("Card 6"))
                  && !ts.contains(QLatin1String("Card 1")),
              QStringLiteral("... \"Selection\": the selected cards alone"), ts);
        // what a card shows
        s = base;
        s.cardSynopsis = false;
        printToPdf(w, pdf, s);
        check(!pdfText(pdf).contains(QLatin1String("Synopsis of card")), QStringLiteral("... without synopses the cards show their titles only"));
    }
    {
        // label, status, keywords, a picture card
        show({ch1, p->research()});
        pane->setViewMode(ViewMode::Corkboard);
        settle(400);
        const QString pdf = pdfPath(QStringLiteral("cards2"));
        PrintSettings s = base;
        s.cardWidth = 105;
        s.cardHeight = 74;
        s.cardSize = QStringLiteral("A7");
        printToPdf(w, pdf, s);
        const QString t = pdfText(pdf);
        const QString status = p->statusById(storm->status) ? p->statusById(storm->status)->name.toUpper() : QString();
        check(t.contains(QLatin1String("Sea")) && (status.isEmpty() || t.contains(status)),
              QStringLiteral("cards: keywords and the status on the card"), t.left(600));
        check(pdfImageCount(pdf) >= 1, QStringLiteral("... a picture document is printed as its picture"),
              QString::number(pdfImageCount(pdf)));
        pdfShots(pdf, QStringLiteral("m14-07-cards-label-picture"), 1);
    }
    {
        show({emptyFolder});
        pane->setViewMode(ViewMode::Corkboard);
        settle(300);
        check(!byKey(&w, QStringLiteral("print"))->isEnabled() && !byKey(&w, QStringLiteral("printPreview"))->isEnabled(),
              QStringLiteral("an empty corkboard: Print Current Document and Print Preview are off"));
    }

    // ============================================================ outliner
    {
        show({p->draft()});
        pane->setViewMode(ViewMode::Outliner);
        settle(400);
        Outliner *o = pane->outliner();
        o->expandAll();
        // close the inner group
        for (int r = 0; r < o->model()->rowCount(o->rootIndex()); ++r) {
            const QModelIndex i = o->model()->index(r, 0, o->rootIndex());
            for (int k = 0; k < o->model()->rowCount(i); ++k) {
                const QModelIndex j = o->model()->index(k, 0, i);
                if (j.data().toString() == QLatin1String("Inner Group"))
                    o->collapse(j);
            }
        }
        settle(200);
        const printing::Job job = printing::currentJob(pane, nullptr);
        check(job.kind == printing::Kind::Outliner && job.columns.size() >= 2 && !job.rows.isEmpty(),
              QStringLiteral("the outliner: its columns and rows are what is printed"),
              QStringLiteral("%1 columns, %2 rows").arg(job.columns.size()).arg(job.rows.size()));
        const QString pdf = pdfPath(QStringLiteral("outliner"));
        printToPdf(w, pdf, base);
        const QString t = pdfText(pdf);
        check(t.contains(QLatin1String("Title and Synopsis")) && t.contains(QLatin1String("Chapter One"))
                  && t.contains(QLatin1String("The Storm")) && t.contains(QLatin1String("The storm reaches the lighthouse.")),
              QStringLiteral("... the column titles, the rows with their synopses"), t.left(800));
        check(!t.contains(QLatin1String("Hidden Scene")), QStringLiteral("... the rows of a closed group are left out"));
        pdfShots(pdf, QStringLiteral("m14-08-outliner"), 1);
        PrintSettings s = base;
        s.outlinerAllRows = true;
        s.outlinerSynopses = 2;
        printToPdf(w, pdf, s);
        const QString t2 = pdfText(pdf);
        check(t2.contains(QLatin1String("Hidden Scene")) && !t2.contains(QLatin1String("The storm reaches the lighthouse.")),
              QStringLiteral("... \"Also the rows inside closed groups\" and synopses \"Never\""), t2.left(600));
        // many rows: the column titles on every page
        s = base;
        s.outlinerAllRows = true;
        s.outlinerSynopses = 1;
        s.outlinerFontSize = 14;
        printToPdf(w, pdf, s);
        pdfShots(pdf, QStringLiteral("m14-08b-outliner-large"), 2);
        const int pages = pdfPages(pdf);
        check(pages >= 2 && pdfText(pdf, 2).contains(QLatin1String("Title and Synopsis")),
              QStringLiteral("... over several pages the column titles are at the top of every page"), QString::number(pages));
        // the selected rows
        o->selectItems({storm, calm});
        settle(200);
        printToPdf(w, pdf, base, -1);
        const QString t3 = pdfText(pdf);
        check(t3.contains(QLatin1String("The Storm")) && t3.contains(QLatin1String("The Calm")) && !t3.contains(QLatin1String("The Ships")),
              QStringLiteral("... \"Selection\": the selected rows alone"), t3.left(400));
    }

    // ============================================================ picture, PDF, web page, audio
    {
        show({pic});
        const QString pdf = pdfPath(QStringLiteral("picture"));
        check(byKey(&w, QStringLiteral("print"))->isEnabled(), QStringLiteral("a picture in the editor: Print Current Document is on"));
        printToPdf(w, pdf, base);
        check(pdfPages(pdf) == 1 && pdfImageCount(pdf) == 1, QStringLiteral("... the picture is printed on one page"));
        show({pdfItem});
        printToPdf(w, pdf, base);
        check(pdfPages(pdf) == 2, QStringLiteral("a PDF document prints its two pages"), QString::number(pdfPages(pdf)));
        pdfShots(pdf, QStringLiteral("m14-09-pdf-document"), 1);
        show({web});
        printToPdf(w, pdf, base);
        check(pdfText(pdf).contains(QLatin1String("History of the Lighthouse")) && pdfText(pdf).contains(QLatin1String("granite")),
              QStringLiteral("a web page document prints its text"), pdfText(pdf));
        show({audio});
        check(!byKey(&w, QStringLiteral("print"))->isEnabled() && !byKey(&w, QStringLiteral("printPreview"))->isEnabled(),
              QStringLiteral("an audio file: Print Current Document and Print Preview are off"));
    }

    // ============================================================ File > Print Settings
    {
        show({storm});
        bool textTab = false, stored = false;
        QString detail;
        withModal<PrintSettingsDialog>(500, [&](PrintSettingsDialog *d) {
            auto *tabs = d->findChild<QTabWidget *>(QStringLiteral("print_tabs"));
            textTab = tabs && tabs->currentIndex() == PrintSettingsDialog::TextTab;
            for (int i = 0; tabs && i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                shot(d, QStringLiteral("m14-1%1-print-settings-%2").arg(i).arg(tabs->tabText(i).toLower().replace(QLatin1Char(' '), QLatin1Char('-'))));
            }
            d->findChild<QCheckBox *>(QStringLiteral("print_comments"))->setChecked(true);
            d->findChild<QComboBox *>(QStringLiteral("print_lineSpacing"))->setCurrentIndex(3);
            d->findChild<QLineEdit *>(QStringLiteral("print_footerCenter"))->setText(QStringLiteral("- <$p> -"));
            auto *size = d->findChild<QComboBox *>(QStringLiteral("print_cardSize"));
            size->setCurrentIndex(size->findData(QStringLiteral("4 x 6 in")));
            emit size->activated(size->currentIndex());
            detail = d->findChild<QLabel *>(QStringLiteral("print_cardInfo"))->text();
            d->accept();
        });
        byKey(&w, QStringLiteral("printSettings"))->trigger();
        settle(300);
        const PrintSettings now = PrintSettings::ofProject(p);
        stored = now.comments && qAbs(now.lineSpacing - 2.0) < 0.01 && now.footerCenter == QLatin1String("- <$p> -")
                 && now.cardSize == QLatin1String("4 x 6 in") && qAbs(now.cardWidth - 152.4) < 0.1;
        check(textTab, QStringLiteral("File > Print Settings opens at the Text tab while a text is shown"));
        check(stored, QStringLiteral("... OK keeps the settings with the project (comments, double spacing, footer, card size)"),
              QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(now.toVariant())).toJson(QJsonDocument::Compact)));
        info(QStringLiteral("card info: %1").arg(detail));
        check(detail.contains(QLatin1String("on each sheet")), QStringLiteral("... the Index Cards tab says how many cards go on a sheet"), detail);
        // reopening shows them; Cancel changes nothing
        bool shown = false;
        withModal<PrintSettingsDialog>(500, [&shown](PrintSettingsDialog *d) {
            shown = d->findChild<QCheckBox *>(QStringLiteral("print_comments"))->isChecked()
                    && d->findChild<QLineEdit *>(QStringLiteral("print_footerCenter"))->text() == QLatin1String("- <$p> -");
            d->findChild<QCheckBox *>(QStringLiteral("print_comments"))->setChecked(false);
            d->reject();
        });
        byKey(&w, QStringLiteral("printSettings"))->trigger();
        settle(300);
        check(shown && PrintSettings::ofProject(p).comments, QStringLiteral("... the window shows the stored settings; Cancel keeps them"));
        // the corkboard opens the Index Cards tab, the outliner the Outliner tab
        int corkTab = -1, outTab = -1;
        show({cardsFolder});
        pane->setViewMode(ViewMode::Corkboard);
        settle(300);
        withModal<PrintSettingsDialog>(500, [&corkTab](PrintSettingsDialog *d) {
            corkTab = d->findChild<QTabWidget *>(QStringLiteral("print_tabs"))->currentIndex();
            d->reject();
        });
        byKey(&w, QStringLiteral("printSettings"))->trigger();
        settle(300);
        pane->setViewMode(ViewMode::Outliner);
        settle(300);
        withModal<PrintSettingsDialog>(500, [&outTab](PrintSettingsDialog *d) {
            outTab = d->findChild<QTabWidget *>(QStringLiteral("print_tabs"))->currentIndex();
            d->reject();
        });
        byKey(&w, QStringLiteral("printSettings"))->trigger();
        settle(300);
        check(corkTab == PrintSettingsDialog::CardsTab && outTab == PrintSettingsDialog::OutlinerTab,
              QStringLiteral("... at the Index Cards tab for a corkboard, at the Outliner tab for an outliner"));
        // new projects
        withModal<PrintSettingsDialog>(500, [](PrintSettingsDialog *d) {
            d->findChild<QCheckBox *>(QStringLiteral("print_newProjects"))->setChecked(true);
            d->findChild<QCheckBox *>(QStringLiteral("print_titles"))->setChecked(false);
            d->accept();
        });
        byKey(&w, QStringLiteral("printSettings"))->trigger();
        settle(300);
        QTemporaryDir other;
        auto np = templates::createProject(blank, other.path(), QStringLiteral("Second"), &err);
        check(np && !PrintSettings::ofProject(np.get()).titles && PrintSettings::ofProject(np.get()).comments,
              QStringLiteral("... \"Use these settings for new projects as well\": a new project starts with them"));
        // the paper of the page view
        PageSetup view = p->docs()->pageSetup();
        view.paper = QStringLiteral("A5");
        view.width = 148;
        view.height = 210;
        view.left = 15;
        p->docs()->setPageSetup(view);
        bool tookPaper = false;
        withModal<PrintSettingsDialog>(500, [&tookPaper](PrintSettingsDialog *d) {
            d->findChild<QPushButton *>(QStringLiteral("print_pageViewPaper"))->click();
            tookPaper = d->settings().page.paper == QLatin1String("A5") && qAbs(d->settings().page.left - 15) < 0.1;
            d->reject();
        });
        byKey(&w, QStringLiteral("printSettings"))->trigger();
        settle(300);
        check(tookPaper, QStringLiteral("... \"Paper of the Page View\" takes the paper and margins of the page view"));
    }

    // ============================================================ File > Print Preview
    {
        show({longDoc});
        PrintSettings s = base;
        PrintSettings::storeInProject(p, s);
        int previewPages = 0;
        withModal<QPrintPreviewDialog>(800, [&previewPages](QPrintPreviewDialog *d) {
            if (auto *pw = d->findChild<QPrintPreviewWidget *>())
                previewPages = pw->pageCount();
            shot(d, QStringLiteral("m14-20-print-preview-text"));
            d->reject();
        });
        byKey(&w, QStringLiteral("printPreview"))->trigger();
        settle(500);
        const QString pdf = pdfPath(QStringLiteral("long-again"));
        printToPdf(w, pdf, s);
        check(previewPages > 1 && previewPages == pdfPages(pdf),
              QStringLiteral("File > Print Preview shows the pages that will be printed"),
              QStringLiteral("%1 in the preview, %2 printed").arg(previewPages).arg(pdfPages(pdf)));
        show({cardsFolder});
        pane->setViewMode(ViewMode::Corkboard);
        settle(300);
        withModal<QPrintPreviewDialog>(800, [&previewPages](QPrintPreviewDialog *d) {
            if (auto *pw = d->findChild<QPrintPreviewWidget *>())
                previewPages = pw->pageCount();
            shot(d, QStringLiteral("m14-21-print-preview-cards"));
            d->reject();
        });
        byKey(&w, QStringLiteral("printPreview"))->trigger();
        settle(500);
        check(previewPages == 3, QStringLiteral("... of the index cards as well"), QString::number(previewPages));
        pane->setViewMode(ViewMode::Outliner);
        settle(300);
        withModal<QPrintPreviewDialog>(800, [](QPrintPreviewDialog *d) {
            shot(d, QStringLiteral("m14-22-print-preview-outliner"));
            d->reject();
        });
        byKey(&w, QStringLiteral("printPreview"))->trigger();
        settle(500);
        // the Preview button of the print settings shows the settings of the window
        bool fromSettings = false;
        withModal<PrintSettingsDialog>(500, [&fromSettings](PrintSettingsDialog *d) {
            d->findChild<QCheckBox *>(QStringLiteral("print_outlinerHeader"))->setChecked(false);
            withModal<QPrintPreviewDialog>(800, [&fromSettings](QPrintPreviewDialog *pd) {
                fromSettings = true;
                pd->reject();
            });
            d->findChild<QPushButton *>(QStringLiteral("print_preview"))->click();
            d->reject();
        });
        byKey(&w, QStringLiteral("printSettings"))->trigger();
        settle(800);
        check(fromSettings && PrintSettings::ofProject(p).outlinerHeader,
              QStringLiteral("Print Settings > Preview… shows the printout with the window's settings (not stored on Cancel)"));
    }

    // ============================================================ the page view does not reach the paper
    {
        show({longDoc});
        byKey(&w, QStringLiteral("pageView"))->setChecked(false);
        byKey(&w, QStringLiteral("pageView"))->trigger();
        settle(400);
        const QString pdf = pdfPath(QStringLiteral("pageview"));
        printToPdf(w, pdf, base);
        const int withView = pdfPages(pdf);
        byKey(&w, QStringLiteral("pageView"))->trigger();
        settle(400);
        printToPdf(w, pdf, base);
        check(withView == pdfPages(pdf) && withView > 0, QStringLiteral("page view on or off: the same printout (the paper of the print settings)"),
              QStringLiteral("%1 / %2").arg(withView).arg(pdfPages(pdf)));
    }

    // ============================================================ settings round trip
    {
        PrintSettings s;
        s.cardSize = QStringLiteral("Custom");
        s.cardWidth = 90;
        s.cardHeight = 60;
        s.outlinerSynopses = 1;
        s.headerRight = QStringLiteral("<$date>");
        const PrintSettings back = PrintSettings::fromVariant(s.toVariant(), PrintSettings());
        check(back == s && back.cardWidth == 90 && back.outlinerSynopses == 1,
              QStringLiteral("print settings: kept as they were set (custom card size, synopses, header)"));
        QString dated = printing::fillPlaceholders(QStringLiteral("<$date> / <$author> / <$p>"), printing::Job(), 4, 9);
        check(dated == QDate::currentDate().toString(Qt::ISODate) + QStringLiteral(" / Ada Keeper / 4"),
              QStringLiteral("header and footer: <$date>, <$author>, <$p>"), dated);
    }

    w.closeProject();
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
