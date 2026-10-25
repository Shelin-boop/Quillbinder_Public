// Scripted run for import and export (milestone M12): the importer
// interface, the add-ons "Word 97–2003 Import" and "Web Page Import"
// (against a local HTTP and HTTPS server), File > Import > Web Page, web
// links dropped on the binder, and the new options of File > Export > Files.
// Saves screenshots into the folder given as first argument. Exit code =
// number of failed checks.
//
//   packaging/build-addons.sh build build/addons-dist
//   QT_QPA_PLATFORM=offscreen ./qb_m12_tests /tmp/shots
#include "AppInfo.h"
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "import/FileImporter.h"
#include "import/ImportedContent.h"
#include "text/DirectFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/MediaView.h"
#include "ui/Theme.h"
#include "ui/ToolDialogs.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QStyleFactory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextList>
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
            std::printf("      %s\n", qPrintable(detail));
    }
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

static bool waitFor(const std::function<bool()> &cond, int ms = 8000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (cond())
            return true;
        settle(40);
    }
    return cond();
}

static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QString menuText(QString t)
{
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.remove(QLatin1Char('&'));
    return t;
}

static QAction *menuPath(QMainWindow *w, const QStringList &path)
{
    QList<QAction *> level = w->menuBar()->actions();
    QAction *found = nullptr;
    for (int i = 0; i < path.size(); ++i) {
        found = nullptr;
        for (QAction *a : level)
            if (menuText(a->text()) == path[i]) {
                found = a;
                break;
            }
        if (!found)
            return nullptr;
        if (i + 1 < path.size()) {
            if (!found->menu())
                return nullptr;
            emit found->menu()->aboutToShow();
            level = found->menu()->actions();
        }
    }
    return found;
}

// Runs fn on the first visible top-level window that `match` accepts, after delayMs.
static void whenWindow(int delayMs, const std::function<bool(QWidget *)> &match, const std::function<void(QWidget *)> &fn,
                       int tries = 100)
{
    QTimer::singleShot(delayMs, qApp, [=] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (tl->isVisible() && match(tl)) {
                fn(tl);
                return;
            }
        if (tries > 0) {
            whenWindow(100, match, fn, tries - 1);
            return;
        }
        std::printf("      (the window did not open)\n");
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

static bool isMessageBox(QWidget *w) { return qobject_cast<QMessageBox *>(w) != nullptr; }
static bool isWebDialog(QWidget *w)
{
    return qobject_cast<QDialog *>(w) && w->findChild<QLineEdit *>(QStringLiteral("webAddress"));
}

// Closes message boxes that turn up (and keeps their texts), from the web part on.
static QStringList g_boxes;
static void closeBoxes()
{
    static QTimer *t = nullptr;
    if (t)
        return;
    t = new QTimer(qApp);
    QObject::connect(t, &QTimer::timeout, qApp, [] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *mb = qobject_cast<QMessageBox *>(tl))
                if (mb->isVisible()) {
                    g_boxes << mb->text() + QLatin1Char(' ') + mb->informativeText();
                    mb->done(QMessageBox::Close);
                }
    });
    t->start(100);
}

static QString fileText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// ---------------------------------------------------------------- a small web server
class TestServer : public QTcpServer
{
public:
    struct Resp {
        int status = 200;
        QByteArray type;
        QByteArray body;
        QByteArray extra;  // more header lines
    };
    QHash<QString, Resp> pages;
    QStringList requests;
    QSslCertificate cert;
    QSslKey key;
    bool tls = false;

    void put(const QString &path, const QByteArray &type, const QByteArray &body) { pages.insert(path, {200, type, body, {}}); }

protected:
    void incomingConnection(qintptr fd) override
    {
        QTcpSocket *s;
        if (tls) {
            auto *ss = new QSslSocket(this);
            ss->setSocketDescriptor(fd);
            ss->setLocalCertificate(cert);
            ss->setPrivateKey(key);
            ss->startServerEncryption();
            s = ss;
        } else {
            s = new QTcpSocket(this);
            s->setSocketDescriptor(fd);
        }
        auto *buf = new QByteArray;
        connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
            *buf += s->readAll();
            if (!buf->contains("\r\n\r\n"))
                return;
            const QList<QByteArray> line = buf->left(buf->indexOf("\r\n")).split(' ');
            const QString path = line.size() > 1 ? QString::fromLatin1(line.at(1)) : QString();
            requests << path + QLatin1Char(' ') + QString::fromLatin1(headerOf(*buf, "user-agent").left(20));
            const Resp r = pages.value(path, Resp{404, "text/html", "<html><body>Not found</body></html>", {}});
            QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + (r.status == 200 ? " OK" : r.status == 301 ? " Moved" : " Not Found")
                             + "\r\nContent-Type: " + r.type + "\r\nContent-Length: " + QByteArray::number(r.body.size())
                             + "\r\nConnection: close\r\n" + r.extra + "\r\n" + r.body;
            s->write(out);
            s->disconnectFromHost();
            buf->clear();
        });
        connect(s, &QTcpSocket::disconnected, s, [s, buf] {
            delete buf;
            s->deleteLater();
        });
    }
    static QByteArray headerOf(const QByteArray &req, const char *name)
    {
        for (const QByteArray &l : req.split('\n'))
            if (l.toLower().startsWith(QByteArray(name) + ':'))
                return l.mid(l.indexOf(':') + 1).trimmed();
        return QByteArray();
    }
};

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// What a text document holds, for the checks.
struct Look {
    QStringList blocks;            // text per block
    QHash<QString, QString> style; // first block with the text -> style
    int images = 0;
    QStringList links;
    QStringList footnotes, comments; // texts
    QStringList commented;           // anchor texts of comments
    bool boldWord = false, italicWord = false, underlineWord = false, strikeWord = false, superWord = false, subWord = false;
    int bullets = 0, numbers = 0, nested = 0;
    bool pageBreakBeforeLast = false;
    QString monoFamily;
    QColor redColor;
};

static Look look(QTextDocument *d)
{
    Look l;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        l.blocks << b.text();
        if (!l.style.contains(b.text()))
            l.style.insert(b.text(), b.blockFormat().stringProperty(props::Style));
        if (QTextList *list = b.textList()) {
            if (list->format().style() == QTextListFormat::ListDecimal)
                ++l.numbers;
            else
                ++l.bullets;
            if (list->format().indent() > 1)
                ++l.nested;
        }
        if (b.blockFormat().boolProperty(props::PageBreak) && b.text().contains(QLatin1String("After a page break")))
            l.pageBreakBeforeLast = true;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            const QString t = fr.text();
            if (cf.isImageFormat()) {
                ++l.images;
                continue;
            }
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            if (kind == NoteKind::InlineFootnote)
                l.footnotes << t;
            if (kind == NoteKind::Comment) {
                l.comments << cf.stringProperty(props::NoteText);
                l.commented << t;
            }
            if (cf.isAnchor())
                l.links << cf.anchorHref();
            if (t.contains(QLatin1String("bold")) && cf.fontWeight() >= QFont::Bold)
                l.boldWord = true;
            if (t.contains(QLatin1String("italic")) && cf.fontItalic())
                l.italicWord = true;
            if (t.contains(QLatin1String("underlined")) && cf.fontUnderline())
                l.underlineWord = true;
            if (t.contains(QLatin1String("struck")) && cf.fontStrikeOut())
                l.strikeWord = true;
            if (t == QLatin1String("2") && cf.verticalAlignment() == QTextCharFormat::AlignSuperScript)
                l.superWord = true;
            if (t == QLatin1String("2") && cf.verticalAlignment() == QTextCharFormat::AlignSubScript)
                l.subWord = true;
            if (t.contains(QLatin1String("mono")))
                l.monoFamily = direct::charSpec(cf).family;
            if (t == QLatin1String("red"))
                l.redColor = direct::charSpec(cf).color;
        }
    }
    return l;
}

static QString styleOf(const Look &l, const QString &startsWith)
{
    for (auto it = l.style.begin(); it != l.style.end(); ++it)
        if (it.key().startsWith(startsWith))
            return it.value();
    return QStringLiteral("(no such block)");
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    // the test pages come from this process: no proxy
    for (const char *v : {"http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"})
        qunsetenv(v);
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    const bool without = args.contains(QStringLiteral("--without-addons"));
    outDir = argc > 1 && !QString::fromLocal8Bit(argv[1]).startsWith(QLatin1String("--")) ? QString::fromLocal8Bit(argv[1])
                                                                                          : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    const QString buildDir = QStringLiteral(QB_TEST_BUILD_DIR);
    const QString data = QStringLiteral(QB_TEST_DATA_DIR);
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.smartQuotes = false;
    theme::apply();

    // ---------------------------------------------------------------- the web server
    TestServer server;
    check(server.listen(QHostAddress::LocalHost), QStringLiteral("test web server listens"));
    const QString site = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    server.put(QStringLiteral("/news/keeper.html"), "text/html; charset=utf-8", readFile(data + QStringLiteral("/article.html")));
    server.put(QStringLiteral("/img/tower.png"), "image/png", readFile(data + QStringLiteral("/tower.png")));
    server.put(QStringLiteral("/img/lens.jpg"), "image/jpeg", readFile(data + QStringLiteral("/lens.jpg")));
    server.put(QStringLiteral("/img/lens-small.jpg"), "image/jpeg", readFile(data + QStringLiteral("/lens-small.jpg")));
    server.put(QStringLiteral("/pixel.gif"), "image/gif", readFile(data + QStringLiteral("/pixel.gif")));
    server.put(QStringLiteral("/de/gruesse.html"), "text/html", readFile(data + QStringLiteral("/latin1.html")));
    server.put(QStringLiteral("/app/story"), "text/html; charset=utf-8", readFile(data + QStringLiteral("/script-only.html")));
    server.put(QStringLiteral("/tides.txt"), "text/plain; charset=utf-8", readFile(data + QStringLiteral("/notes.txt")));
    server.put(QStringLiteral("/photo.png"), "image/png", readFile(data + QStringLiteral("/tower.png")));
    server.put(QStringLiteral("/paper.pdf"), "application/pdf", "%PDF-1.4\n%%EOF\n");
    server.pages.insert(QStringLiteral("/old-address"), {301, "text/html", "moved", "Location: /news/keeper.html\r\n"});

    if (!without) {
        // the checks without add-ons run in a process of their own (plugins are loaded once, at the start)
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(), {outDir, QStringLiteral("--without-addons")});
        child.waitForFinished(300000);
        for (const QByteArray &l : child.readAll().split('\n'))
            if (l.startsWith("PASS") || l.startsWith("FAIL") || l.startsWith("      "))
                std::printf("%s\n", l.constData());
        if (child.exitStatus() != QProcess::NormalExit)
            check(false, QStringLiteral("the run without add-ons ended normally"));
        failures += child.exitCode();
        std::fflush(stdout);
        // ================================================================ the add-ons
        QStringList problems;
        for (const QString &id : {QStringLiteral("import-word97"), QStringLiteral("import-web")}) {
            const QString pkg = buildDir + QStringLiteral("/addons-dist/") + id + QStringLiteral(".qbpkg");
            const addons::PackageInfo info = addons::inspect(pkg);
            QStringList log;
            QString e;
            bool restart = false;
            if (!info.isValid() || info.manifest.minApp != QLatin1String("0.13.0") || !addons::install(pkg, &log, &e, &restart))
                problems << id + QLatin1String(": ") + info.error + e + QLatin1String(" minApp ") + info.manifest.minApp;
            if (QFileInfo(pkg).size() > 30 * 1024 * 1024)
                problems << id + QLatin1String(" is larger than 30 MiB");
        }
        check(problems.isEmpty(), QStringLiteral("the two add-on packages are valid, need 0.13.0 and install"), problems.join(QLatin1String("; ")));
        bool tlsPlugin = false;
        for (const QString &d : addons::contentDirs(QStringLiteral("qtplugins")))
            tlsPlugin = tlsPlugin || QFileInfo::exists(d + QStringLiteral("/tls/libqopensslbackend.so"));
        check(tlsPlugin, QStringLiteral("the web add-on brings Qt's TLS plugin (qtplugins/tls/libqopensslbackend.so)"));
        compile::PluginManager::instance().load();
    }

    // ---------------------------------------------------------------- project and window
    QTemporaryDir projectsTmp;
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto p = templates::createProject(novel, projectsTmp.path(), QStringLiteral("Skerry Point"), &err);
    check(p != nullptr, QStringLiteral("create project"), err);
    if (!p)
        return 1;
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(p)), QStringLiteral("open project in main window"));
    Project *proj = w.project();
    BinderModel *m = proj->model();
    m->moveToTrash(QList<BinderItem *>(proj->draft()->children));
    BinderItem *keeper = m->createItem(proj->draft(), 0, ItemType::Text, QStringLiteral("The Keeper"));
    templates::setMarkdown(proj, keeper, QStringLiteral("The keeper climbed the tower.\n"));
    m->refreshAll();
    proj->save();
    settle(300);
    auto *binder = w.findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    auto *pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    check(binder && pane0, QStringLiteral("window parts found"));
    if (!binder || !pane0)
        return 1;

    // ================================================================ without the add-ons
    QAction *webAction = w.findChild<QAction *>(QStringLiteral("importWebPage"));
    if (without) {
        check(menuPath(&w, {"File", "Import", "Web Page…"}) && menuPath(&w, {"File", "Import", "Files…"})
                  && menuPath(&w, {"File", "Import", "Import and Split…"}) && menuPath(&w, {"File", "Export", "Files…"}),
              QStringLiteral("File > Import > Web Page… (as in the tutorial) next to Files and Import and Split"));
        check(webAction && webAction->isEnabled(), QStringLiteral("the command Import Web Page exists and is on with a project open"));
        QString text;
        whenWindow(200, isMessageBox, [&](QWidget *mb) {
            text = static_cast<QMessageBox *>(mb)->text() + QLatin1Char(' ') + static_cast<QMessageBox *>(mb)->informativeText();
            mb->close();
        });
        if (webAction)
            webAction->trigger();
        settle(200);
        check(text.contains(QLatin1String("Web Page Import")) && text.contains(QLatin1String("Add-on Manager")),
              QStringLiteral("without the add-on, Import Web Page says which add-on is needed"), text);
        check(!importer::textFileFilters().first().contains(QRegularExpression(QStringLiteral("\\*\\.doc[ )]"))),
              QStringLiteral("without the add-on, .doc is not among the text files of the file dialog"));
        QStringList warnings;
        const auto items = importer::importFiles(proj, {data + QStringLiteral("/features.doc")}, proj->research(), -1, &warnings);
        check(items.size() == 1 && items.first()->type == ItemType::File && warnings.join(QLatin1Char(' ')).contains(QStringLiteral("Word 97–2003 Import")),
              QStringLiteral("without the add-on, a .doc is added as a file, with a hint at the add-on"),
              QStringLiteral("%1 items, type %2: ").arg(items.size()).arg(items.isEmpty() ? -1 : int(items.first()->type)) + warnings.join(QLatin1Char(' ')));
        // ".doc" files that are really RTF or Word XML: read by the program itself
        QFile rtf(home.path() + QStringLiteral("/letter.doc"));
        rtf.open(QIODevice::WriteOnly);
        rtf.write("{\\rtf1\\ansi{\\fonttbl{\\f0 Times;}}\\f0 Dear {\\b Margaret},\\par thank you for the logbooks.\\par}");
        rtf.close();
        warnings.clear();
        const auto r1 = importer::importFiles(proj, {rtf.fileName()}, proj->research(), -1, &warnings);
        const auto r2 = importer::importFiles(proj, {data + QStringLiteral("/docx-saved-as.doc")}, proj->research(), -1, &warnings);
        check(r1.size() == 1 && r1.first()->type == ItemType::Text && proj->docs()->plainText(r1.first()->id).contains(QLatin1String("Dear Margaret"))
                  && r2.size() == 1 && r2.first()->type == ItemType::Text
                  && proj->docs()->plainText(r2.first()->id).contains(QLatin1String("The Lighthouse Keeper")),
              QStringLiteral("a “.doc” that is really RTF or a .docx becomes text without any add-on"));
        // a web link dropped without the add-on
        text.clear();
        whenWindow(200, isMessageBox, [&](QWidget *mb) {
            text = static_cast<QMessageBox *>(mb)->text();
            mb->close();
        });
        const auto none = w.importDropped(proj->research(), -1, {QUrl(site + QStringLiteral("/news/keeper.html"))});
        settle(200);
        check(none.isEmpty() && text.contains(QLatin1String("Web Page Import")), QStringLiteral("a web link dropped without the add-on: the same hint"));
        std::fflush(stdout);
        return failures;
    }

    {
        auto &pm = compile::PluginManager::instance();
        QStringList ids;
        for (importers::ImporterInterface *i : pm.importers())
            ids << i->id();
        ids.sort();
        check(ids == QStringList({QStringLiteral("import-web"), QStringLiteral("import-word97")}) && pm.importerFor(QStringLiteral("doc"))
                  && pm.importerFor(QStringLiteral("DOT")) && pm.webImporter() && pm.webImporter()->id() == QLatin1String("import-web"),
              QStringLiteral("the plugin manager loads both importers: .doc / .dot and web pages"),
              ids.join(QLatin1Char(',')) + QLatin1String(" / ") + pm.messages().join(QLatin1String("; ")));
        check(importer::textFileFilters().first().contains(QRegularExpression(QStringLiteral("\\*\\.doc[ )]"))) && importer::textFileFilters().join(QLatin1Char(' ')).contains(QStringLiteral("Word 97–2003 documents (*.doc *.dot)")),
              QStringLiteral("with the add-on, the file dialogs offer .doc and .dot (also as a filter of their own)"));
        check(importer::classify(data + QStringLiteral("/features.doc")) == importer::FileKind::Text,
              QStringLiteral("a .doc counts as text now (the Draft takes it)"));
        check(pm.exporter(QStringLiteral("docx")) != nullptr, QStringLiteral("exporters still load next to the importers"));
    }

    // ================================================================ Word 97–2003
    {
        QStringList warnings;
        const auto items = importer::importFiles(proj, {data + QStringLiteral("/features.doc")}, proj->draft(), -1, &warnings);
        check(items.size() == 1 && items.first()->type == ItemType::Text && items.first()->title == QLatin1String("features"),
              QStringLiteral("a .doc imported into the Draft becomes a text document named after the file"), warnings.join(QLatin1Char(' ')));
        if (!items.isEmpty()) {
            QTextDocument *d = proj->docs()->text(items.first()->id);
            const Look l = look(d);
            check(styleOf(l, QStringLiteral("The Lighthouse Keeper")) == QLatin1String("Title")
                      && styleOf(l, QStringLiteral("Chapter One")) == QLatin1String("Heading 1")
                      && styleOf(l, QStringLiteral("Second Part")) == QLatin1String("Heading 2"),
                  QStringLiteral(".doc: Title, Heading 1 and Heading 2 become the project's styles"),
                  styleOf(l, QStringLiteral("The Lighthouse Keeper")) + QLatin1String(" / ") + styleOf(l, QStringLiteral("Chapter One")));
            check(l.boldWord && l.italicWord && l.underlineWord && l.strikeWord && l.superWord && l.subWord,
                  QStringLiteral(".doc: bold, italic, underline, strikethrough, superscript, subscript"));
            check(l.blocks.join(QLatin1Char('\n')).contains(QStringLiteral("Grüße, café, Ελληνικά, 日本語 – “quotes” …")),
                  QStringLiteral(".doc: umlauts, Greek, Japanese, dashes and quotation marks"));
            check(l.links.contains(QStringLiteral("https://example.org/page?a=1&b=2")), QStringLiteral(".doc: a hyperlink field becomes a link"),
                  l.links.join(QLatin1Char(' ')));
            check(l.footnotes.contains(QStringLiteral("The footnote text.")) && l.footnotes.contains(QStringLiteral("The endnote text.")),
                  QStringLiteral(".doc: footnote and endnote become footnotes of the program"), l.footnotes.join(QLatin1String(" | ")));
            check(l.comments.contains(QStringLiteral("Jane Doe: Check this phrase.")) && l.commented.join(QString()) == QLatin1String("a commented phrase"),
                  QStringLiteral(".doc: a comment keeps its author and the commented words"),
                  l.comments.join(QLatin1String(" | ")) + QLatin1String(" / ") + l.commented.join(QLatin1String(" | ")));
            QTextBlock centered;
            for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                if (b.text() == QLatin1String("Centered line"))
                    centered = b;
            check(centered.isValid() && (centered.blockFormat().alignment() & Qt::AlignHCenter),
                  QStringLiteral(".doc: a centered paragraph stays centered"));
            check(styleOf(l, QStringLiteral("A quotation")) == QLatin1String("Block Quote"),
                  QStringLiteral(".doc: the quotations style becomes Block Quote"), styleOf(l, QStringLiteral("A quotation")));
            check(l.bullets == 3 && l.nested == 1 && l.numbers == 2, QStringLiteral(".doc: bullets (one nested) and a numbered list"),
                  QStringLiteral("bullets %1 nested %2 numbers %3").arg(l.bullets).arg(l.nested).arg(l.numbers));
            check(l.blocks.contains(QStringLiteral("Name\tRole\tAge")) && l.blocks.contains(QStringLiteral("Tom\tKeeper and cook\t52")),
                  QStringLiteral(".doc: a table becomes a line per row, cells separated by tabs"));
            check(l.images == 1, QStringLiteral(".doc: the inline picture is kept"));
            check(l.pageBreakBeforeLast, QStringLiteral(".doc: the page break before the last paragraph is kept"));
            proj->save();
            const QString stored = fileText(proj->docs()->textPath(items.first()->id));
            check(stored.contains(QLatin1String("**bold**")) && stored.contains(QLatin1String("# Chapter One")),
                  QStringLiteral(".doc: the text is stored in the project's Markdown"), stored.left(300));
            pane0->showItems({items.first()});
            shot(&w, QStringLiteral("m12-doc-imported"));
        }
        // Import and Split
        QString e;
        warnings.clear();
        const auto parts = importer::importAndSplit(proj, data + QStringLiteral("/chapters.doc"), QStringLiteral("#"), true, proj->draft(), -1,
                                                    &e, &warnings);
        QStringList titles;
        for (BinderItem *it : parts)
            titles << it->title;
        check(titles == QStringList({QStringLiteral("The Arrival"), QStringLiteral("The Storm"), QStringLiteral("The Light")}),
              QStringLiteral("Import and Split of a .doc at “#” lines, first lines as titles"), titles.join(QLatin1String(", ")) + e);
        // dropped on the binder (from a file manager)
        auto *md = new QMimeData;
        md->setUrls({QUrl::fromLocalFile(data + QStringLiteral("/chapters.doc"))});
        const int before = proj->research()->children.size();
        m->dropMimeData(md, Qt::CopyAction, -1, 0, m->indexFor(proj->research()));
        delete md;
        settle(200);
        check(proj->research()->children.size() == before + 1 && proj->research()->children.last()->type == ItemType::Text,
              QStringLiteral("a .doc dropped on the binder becomes a text document"));
        // a Word 6 file: LibreOffice when there is one
        QTemporaryDir fake;
        const QString script = fake.path() + QStringLiteral("/soffice");
        QFile sf(script);
        sf.open(QIODevice::WriteOnly);
        // a stand-in for LibreOffice: puts a known .docx where LibreOffice would
        sf.write(QStringLiteral("#!/bin/sh\nfor a; do last=$a; done\nprev=\"\"\nfor a; do [ \"$prev\" = \"--outdir\" ] && out=$a; prev=$a; done\n"
                                "cp '%1' \"$out/document.docx\"\n")
                     .arg(data + QStringLiteral("/docx-saved-as.doc"))
                     .toUtf8());
        sf.close();
        sf.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("QB_SOFFICE", script.toUtf8());
        warnings.clear();
        const auto old = importer::importFiles(proj, {data + QStringLiteral("/word6-fake.doc")}, proj->research(), -1, &warnings);
        check(old.size() == 1 && old.first()->type == ItemType::Text
                  && proj->docs()->plainText(old.first()->id).contains(QLatin1String("The Lighthouse Keeper"))
                  && warnings.join(QLatin1Char(' ')).contains(QLatin1String("LibreOffice")),
              QStringLiteral("a Word 6.0 / 95 file is converted with LibreOffice (here a stand-in), and the writer is told"),
              warnings.join(QLatin1Char(' ')));
        qputenv("QB_SOFFICE", "none");
        warnings.clear();
        const auto none = importer::importFiles(proj, {data + QStringLiteral("/word6-fake.doc")}, proj->research(), -1, &warnings);
        check(none.isEmpty() && warnings.join(QLatin1Char(' ')).contains(QLatin1String("sudo apt install libreoffice-writer")),
              QStringLiteral("without LibreOffice: a clear message what to install"), warnings.join(QLatin1Char(' ')));
        qunsetenv("QB_SOFFICE");
        // damaged files do not bring the program down
        QByteArray doc = readFile(data + QStringLiteral("/features.doc"));
        QFile cut(home.path() + QStringLiteral("/cut.doc"));
        cut.open(QIODevice::WriteOnly);
        cut.write(doc.left(doc.size() / 2));
        cut.close();
        QFile junk(home.path() + QStringLiteral("/junk.doc"));
        junk.open(QIODevice::WriteOnly);
        QByteArray noise = doc.left(512);
        for (int i = 0; i < 20000; ++i)
            noise += char((i * 7919) % 251);
        junk.write(noise);
        junk.close();
        qputenv("QB_SOFFICE", "none");
        warnings.clear();
        importer::importFiles(proj, {cut.fileName(), junk.fileName()}, proj->research(), -1, &warnings);
        qunsetenv("QB_SOFFICE");
        check(true, QStringLiteral("a .doc cut in half and one full of noise: no crash"), warnings.join(QLatin1Char(' ')).left(300));
        // an RTF project keeps fonts and colors
        auto rp = Project::create(projectsTmp.path(), QStringLiteral("RTF Project"), &e);
        if (rp) {
            rp->docs()->convertStorage(StorageFormat::Rtf, &e);
            const auto ri = importer::importFiles(rp.get(), {data + QStringLiteral("/features.doc")}, rp->research(), -1, &warnings);
            const Look rl = ri.isEmpty() ? Look() : look(rp->docs()->text(ri.first()->id));
            check(rl.monoFamily == QLatin1String("DejaVu Sans Mono") && rl.redColor == QColor(0xc0, 0, 0),
                  QStringLiteral(".doc in an RTF project: fonts and colors set by hand are kept"), rl.monoFamily + QLatin1Char(' ') + rl.redColor.name());
        }
    }

    // ================================================================ web pages
    // HTTPS with a certificate of our own (the test trusts it)
    QTemporaryDir certDir;
    bool haveTls = false;
    TestServer tlsServer;
    {
        QProcess ossl;
        ossl.start(QStringLiteral("openssl"), {QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"), QStringLiteral("rsa:2048"),
                                               QStringLiteral("-nodes"), QStringLiteral("-keyout"), certDir.filePath(QStringLiteral("key.pem")),
                                               QStringLiteral("-out"), certDir.filePath(QStringLiteral("cert.pem")), QStringLiteral("-days"),
                                               QStringLiteral("2"), QStringLiteral("-subj"), QStringLiteral("/CN=127.0.0.1"), QStringLiteral("-addext"),
                                               QStringLiteral("subjectAltName=IP:127.0.0.1")});
        ossl.waitForFinished(30000);
        const QList<QSslCertificate> certs = QSslCertificate::fromPath(certDir.filePath(QStringLiteral("cert.pem")));
        QFile kf(certDir.filePath(QStringLiteral("key.pem")));
        if (!certs.isEmpty() && kf.open(QIODevice::ReadOnly)) {
            tlsServer.cert = certs.first();
            tlsServer.key = QSslKey(kf.readAll(), QSsl::Rsa);
            tlsServer.tls = true;
            tlsServer.pages = server.pages;
            haveTls = tlsServer.listen(QHostAddress::LocalHost);
            QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
            conf.addCaCertificate(tlsServer.cert);
            QSslConfiguration::setDefaultConfiguration(conf);
        }
    }
    {
        // File > Import > Web Page with the dialog
        binder->select({proj->research()}, true);
        settle(100);
        QString status, preview, previewWhole, statusAfterTypo;
        bool imported = false, enterStaysOpen = false;
        whenWindow(200, isWebDialog, [&](QWidget *dlg) {
            auto *addr = dlg->findChild<QLineEdit *>(QStringLiteral("webAddress"));
            auto *load = dlg->findChild<QPushButton *>(QStringLiteral("webLoad"));
            auto *st = dlg->findChild<QLabel *>(QStringLiteral("webStatus"));
            auto *pv = dlg->findChild<QTextBrowser *>(QStringLiteral("webPreview"));
            auto *only = dlg->findChild<QCheckBox *>(QStringLiteral("webArticleOnly"));
            addr->setText(QStringLiteral("not an address at all"));
            load->click();
            statusAfterTypo = st->text();
            addr->setText(site + QStringLiteral("/old-address"));
            addr->setFocus();
            QTest::keyClick(addr, Qt::Key_Return); // Enter loads; it fetches with an event loop inside
            waitFor([&] { return st->text().contains(QLatin1String("word")); }, 15000);
            enterStaysOpen = dlg->isVisible();
            status = st->text();
            preview = pv->toPlainText();
            shot(dlg, QStringLiteral("m12-web-dialog"));
            only->setChecked(false);
            waitFor([&] { return pv->toPlainText().contains(QLatin1String("Most read")); }, 8000);
            previewWhole = pv->toPlainText();
            only->setChecked(true);
            waitFor([&] { return !pv->toPlainText().contains(QLatin1String("Most read")); }, 8000);
            dlg->findChild<QLineEdit *>(QStringLiteral("webTitle"))->setText(QStringLiteral("Skerry Point Keeper"));
            imported = true;
            dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        const int before = proj->research()->children.size();
        webAction->trigger();
        settle(300);
        check(statusAfterTypo.contains(QLatin1String("not the address")), QStringLiteral("web dialog: a typo in the address is reported"), statusAfterTypo);
        check(enterStaysOpen, QStringLiteral("web dialog: Enter in the address loads the preview (the window stays open)"));
        check(status.contains(QLatin1String("word")) && status.contains(QLatin1String("2 pictures")) && !status.contains(QLatin1String("(s)")) && status.contains(QLatin1String("Tom Hale"))
                  && status.contains(QLatin1String("Coastal Gazette")),
              QStringLiteral("web dialog: words, pictures, author and site of the page"), status);
        check(preview.contains(QLatin1String("one hundred and twelve steps")) && !preview.contains(QLatin1String("Most read"))
                  && !preview.contains(QLatin1String("We use cookies")) && !preview.contains(QLatin1String("What a lovely story"))
                  && !preview.contains(QLatin1String("All rights reserved")) && !preview.contains(QLatin1String("Share on")),
              QStringLiteral("web dialog: the preview shows the article without menus, cookie notice, comments, footer and share buttons"),
              preview.left(400));
        check(previewWhole.contains(QLatin1String("Most read")) && previewWhole.contains(QLatin1String("one hundred and twelve")),
              QStringLiteral("web dialog: “Only the article” off shows the whole page"));
        check(imported && proj->research()->children.size() == before + 1, QStringLiteral("web dialog: Import adds a document to the selected folder"));
        BinderItem *page = proj->research()->children.value(before);
        if (page) {
            const Look l = look(proj->docs()->text(page->id));
            check(page->title == QLatin1String("Skerry Point Keeper") && page->type == ItemType::Text,
                  QStringLiteral("web page: a text document with the title typed in the window"), page->title);
            check(styleOf(l, QStringLiteral("The Last Lighthouse Keeper")) == QLatin1String("Title")
                      && styleOf(l, QStringLiteral("A life measured")) == QLatin1String("Heading 1")
                      && styleOf(l, QStringLiteral("The automation")) == QLatin1String("Heading 2")
                      && styleOf(l, QStringLiteral("The sea does not care")) == QLatin1String("Block Quote")
                      && styleOf(l, QStringLiteral("if (dusk)")) == QLatin1String("Code Block")
                      && styleOf(l, QStringLiteral("The tower at Skerry Point, built")) == QLatin1String("Caption"),
                  QStringLiteral("web page: title, headings, quote, code and caption get the project's styles"));
            check(l.bullets == 4 && l.nested == 1 && l.numbers == 3, QStringLiteral("web page: bullet list (one item nested) and numbered list"),
                  QStringLiteral("bullets %1 nested %2 numbers %3").arg(l.bullets).arg(l.nested).arg(l.numbers));
            check(l.blocks.contains(QStringLiteral("1853\tTower built")) && l.blocks.contains(QStringLiteral("Year\tEvent")),
                  QStringLiteral("web page: a table becomes a line per row"));
            check(l.images == 2, QStringLiteral("web page: two pictures (the lazy one by its real address; no counting pixel)"),
                  QString::number(l.images));
            check(l.links.contains(site + QStringLiteral("/museum")), QStringLiteral("web page: links point to the full address"), l.links.join(QLatin1Char(' ')));
            check(l.boldWord == false && l.blocks.join(QLatin1Char(' ')).contains(QLatin1String("one hundred and twelve")),
                  QStringLiteral("web page: the text is there"));
            check(page->bookmarks.size() == 1 && page->bookmarks.first().target == site + QStringLiteral("/news/keeper.html")
                      && page->bookmarks.first().title == QLatin1String("Skerry Point Keeper"),
                  QStringLiteral("web page: the address (after the redirection) is a bookmark of the document"),
                  page->bookmarks.isEmpty() ? QString() : page->bookmarks.first().target);
            check(page->synopsis.startsWith(QLatin1String("For forty years Margaret Reid kept the light")),
                  QStringLiteral("web page: the page's description is the synopsis"), page->synopsis);
            const QString notes = proj->docs()->notesPlainText(page->id);
            check(notes.contains(QStringLiteral("Address: ") + site) && notes.contains(QLatin1String("Author: Tom Hale"))
                      && notes.contains(QLatin1String("Published: 2026-09-14")) && notes.contains(QLatin1String("Site: Coastal Gazette"))
                      && notes.contains(QRegularExpression(QStringLiteral("Retrieved: \\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d"))),
                  QStringLiteral("web page: address, site, author, date and time of retrieval in the notes"), notes);
            bool ua = false;
            for (const QString &r : server.requests)
                ua = ua || r.contains(QLatin1String("Mozilla/5.0"));
            check(ua && !server.requests.join(QLatin1Char(' ')).contains(QLatin1String("lens-small")),
                  QStringLiteral("web page: fetched like a browser; of a picture's sizes the larger one"), server.requests.join(QLatin1String(", ")));
            pane0->showItems({page});
            shot(&w, QStringLiteral("m12-web-imported"));
        }
        QSettings s;
        check(s.value(QStringLiteral("importers/web/articleOnly")).toBool() && s.value(QStringLiteral("importers/web/pictures")).toBool()
                  && !s.value(QStringLiteral("importers/web/asWebPage")).toBool(),
              QStringLiteral("web dialog: the choices are remembered"));
    }
    {
        // as a web page document
        bool done = false;
        whenWindow(200, isWebDialog, [&](QWidget *dlg) {
            dlg->findChild<QLineEdit *>(QStringLiteral("webAddress"))->setText(site + QStringLiteral("/news/keeper.html"));
            dlg->findChild<QRadioButton *>(QStringLiteral("webAsPage"))->setChecked(true);
            done = true;
            // Import before Load: it loads first
            dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        BinderItem *it = w.importWebPage(QString(), true, proj->research(), -1);
        check(done && it && it->type == ItemType::WebPage && it->fileExt == QLatin1String("html"),
              QStringLiteral("web page document: Import without Load first fetches the page, then adds it"));
        if (it) {
            const QString dir = proj->contentDir(it->id);
            const QString html = fileText(proj->mediaFilePath(it));
            const QStringList pics = QDir(dir + QStringLiteral("/page-files")).entryList(QDir::Files);
            check(html.contains(QLatin1String("steps of the tower every evening")) && html.contains(QLatin1String("page-files/")) && pics.size() == 2,
                  QStringLiteral("web page document: the page as HTML in the project, its two pictures beside it"), pics.join(QLatin1Char(',')));
            pane0->showItems({it});
            settle(300);
            QTextBrowser *tb = nullptr;
            for (QTextBrowser *b : w.findChildren<QTextBrowser *>())
                if (b->isVisible() && b->toPlainText().contains(QLatin1String("one hundred and twelve")))
                    tb = b;
            check(tb != nullptr, QStringLiteral("web page document: shown in the editor"));
            shot(&w, QStringLiteral("m12-web-page-document"));
        }
        QSettings s;
        s.setValue(QStringLiteral("importers/web/asWebPage"), false);
    }
    {
        // a web link dropped on the binder: no questions, the last choices
        auto *md = new QMimeData;
        md->setUrls({QUrl(site + QStringLiteral("/de/gruesse.html"))});
        md->setText(site + QStringLiteral("/de/gruesse.html"));
        BinderItem *folder = m->createItem(proj->research(), -1, ItemType::Folder, QStringLiteral("Clippings"));
        m->dropMimeData(md, Qt::CopyAction, -1, 0, m->indexFor(folder));
        delete md;
        check(waitFor([&] { return folder->children.size() == 1; }, 15000), QStringLiteral("a web link dropped on a folder of the binder is imported into it"));
        if (!folder->children.isEmpty()) {
            BinderItem *g = folder->children.first();
            const QString t = proj->docs()->plainText(g->id);
            check(g->title == QStringLiteral("Grüße aus Köln") && t.contains(QStringLiteral("Die Straße am Rhein war im Frühling"))
                      && t.contains(QStringLiteral("läuteten – wie")),
                  QStringLiteral("a page in ISO-8859-1 (Windows-1252 dashes): umlauts and dashes right"), g->title + QLatin1String(" / ") + t.left(120));
        }
        // more kinds of addresses (a message box tells about the script page)
        closeBoxes();
        g_boxes.clear();
        BinderItem *s1 = w.importWebPage(site + QStringLiteral("/app/story"), false, proj->research(), -1);
        check(s1 && proj->docs()->plainText(s1->id).contains(QLatin1String("only in the data of the page"))
                  && proj->docs()->plainText(s1->id).contains(QLatin1String("third paragraph"))
                  && g_boxes.join(QLatin1Char(' ')).contains(QLatin1String("script")),
              QStringLiteral("a page that builds its text with a script: the text of its JSON-LD data, and the writer is told"),
              g_boxes.join(QLatin1String(" | ")));
        BinderItem *s2 = w.importWebPage(site + QStringLiteral("/tides.txt"), false, proj->research(), -1);
        check(s2 && proj->docs()->plainText(s2->id).contains(QLatin1String("High water at 06:12")),
              QStringLiteral("a plain text address becomes a text document"));
        BinderItem *s3 = w.importWebPage(site + QStringLiteral("/photo.png"), false, proj->research(), -1);
        check(s3 && look(proj->docs()->text(s3->id)).images == 1, QStringLiteral("the address of a picture becomes a document with the picture"));
        // errors
        g_boxes.clear();
        check(!w.importWebPage(site + QStringLiteral("/missing.html"), false, proj->research(), -1), QStringLiteral("404: nothing imported"));
        settle(300);
        check(!w.importWebPage(site + QStringLiteral("/paper.pdf"), false, proj->research(), -1), QStringLiteral("a PDF address: nothing imported"));
        settle(300);
        check(!w.importWebPage(QStringLiteral("http://127.0.0.1:1/"), false, proj->research(), -1), QStringLiteral("no server: nothing imported"));
        settle(300);
        const QStringList messages = g_boxes;
        check(messages.size() == 3 && messages.value(0).contains(QLatin1String("404")) && messages.value(1).contains(QLatin1String("PDF"))
                  && !messages.value(2).isEmpty(),
              QStringLiteral("the writer is told why: 404, a PDF file, no connection"), messages.join(QLatin1String(" | ")));
        // pictures off
        QSettings st;
        st.setValue(QStringLiteral("importers/web/pictures"), false);
        BinderItem *s4 = w.importWebPage(site + QStringLiteral("/news/keeper.html"), false, proj->research(), -1);
        check(s4 && look(proj->docs()->text(s4->id)).images == 0, QStringLiteral("“Pictures” off: the text without pictures"));
        st.setValue(QStringLiteral("importers/web/pictures"), true);
        // HTTPS
        if (haveTls) {
            const QString secure = QStringLiteral("https://127.0.0.1:%1/news/keeper.html").arg(tlsServer.serverPort());
            BinderItem *s5 = w.importWebPage(secure, false, proj->research(), -1);
            check(QSslSocket::supportsSsl() && s5 && look(proj->docs()->text(s5->id)).images == 2 && s5->bookmarks.value(0).target == secure,
                  QStringLiteral("a secure (https) page with its pictures"), QSslSocket::sslLibraryVersionString());
        } else {
            check(false, QStringLiteral("a secure (https) page with its pictures"), QStringLiteral("no test certificate (openssl missing?)"));
        }
        proj->save();
    }

    // ================================================================ Export Files
    {
        BinderItem *chapter = m->createItem(proj->draft(), -1, ItemType::Text, QStringLiteral("Night Watch"));
        templates::setMarkdown(proj, chapter, QStringLiteral("The lamp turned. {>>Check the time<<} She waited.\n"));
        QTextDocument nd;
        nd.setPlainText(QStringLiteral("Remember the fog bell."));
        proj->docs()->setNotesContent(chapter->id, &nd);
        chapter->synopsis = QStringLiteral("The keeper waits for the fog.");
        if (!proj->labels.isEmpty())
            chapter->label = proj->labels.first().id;
        if (!proj->statuses.isEmpty())
            chapter->status = proj->statuses.first().id;
        Keyword kw;
        kw.id = QStringLiteral("kw-fog");
        kw.name = QStringLiteral("Fog");
        proj->keywords << kw;
        chapter->keywords << kw.id;
        CustomField cf;
        cf.id = QStringLiteral("cf-pov");
        cf.title = QStringLiteral("Point of View");
        proj->customFields << cf;
        chapter->custom.insert(cf.id, QStringLiteral("Margaret"));
        chapter->bookmarks << Bookmark{QStringLiteral("Tide tables"), QStringLiteral("https://example.org/tides")};
        // a comment in the text
        {
            QTextDocument *d = proj->docs()->text(chapter->id);
            QTextCursor c(d);
            c.setPosition(4);
            c.setPosition(8, QTextCursor::KeepAnchor);
            c.mergeCharFormat(text::noteFormat(NoteKind::Comment, QStringLiteral("c1"), QStringLiteral("Which lamp?")));
        }
        QString e;
        snapshots::take(proj, chapter->id, QStringLiteral("First draft"), &e);
        proj->notifyItemChanged(chapter);
        proj->save();
        BinderItem *webDoc = nullptr;
        for (BinderItem *c : proj->research()->children)
            if (c->type == ItemType::WebPage)
                webDoc = c;
        QTemporaryDir out;
        QString dialogTexts;
        whenWindow(200, [](QWidget *x) { return qobject_cast<ExportFilesDialog *>(x) != nullptr; }, [&](QWidget *x) {
            auto *dlg = static_cast<ExportFilesDialog *>(x);
            auto *fmt = dlg->findChild<QComboBox *>(QStringLiteral("exportFormat"));
            fmt->setCurrentIndex(fmt->findData(QStringLiteral("md")));
            for (const char *k : {"metadata", "synopsis", "notes", "snapshots"})
                dlg->findChild<QCheckBox *>(QStringLiteral("export_") + QLatin1String(k))->setChecked(true);
            dlg->findChild<QCheckBox *>(QStringLiteral("export_comments"))->setChecked(false);
            dlg->findChild<QLineEdit *>(QStringLiteral("exportLocation"))->setText(out.path());
            dlg->findChild<QLineEdit *>(QStringLiteral("exportFolderName"))->setText(QStringLiteral("For the Editor"));
            for (QCheckBox *c : dlg->findChildren<QCheckBox *>())
                dialogTexts += c->text() + QLatin1Char('|');
            shot(dlg, QStringLiteral("m12-export-dialog"));
            dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        QList<BinderItem *> sel{chapter};
        if (webDoc)
            sel << webDoc;
        binder->select(sel, true);
        settle(100);
        w.findChild<QAction *>(QStringLiteral("exportFiles"))->trigger();
        settle(300);
        const QString folder = out.path() + QStringLiteral("/For the Editor");
        check(dialogTexts.contains(QLatin1String("Export metadata")) && dialogTexts.contains(QLatin1String("Export notes"))
                  && dialogTexts.contains(QLatin1String("Export comments and inline annotations")) && dialogTexts.contains(QLatin1String("Export snapshots")),
              QStringLiteral("Export Files: Metadata and Options (as in the tutorial)"), dialogTexts);
        check(QFileInfo(folder).isDir(), QStringLiteral("Export Files: several documents go into a new folder of the given name"));
        const QString text = fileText(folder + QStringLiteral("/Night Watch.md"));
        const QString notes = fileText(folder + QStringLiteral("/Night Watch (notes).md"));
        const QString meta = fileText(folder + QStringLiteral("/Night Watch (metadata).txt"));
        const QStringList snaps = QDir(folder + QStringLiteral("/Night Watch (snapshots)")).entryList(QDir::Files);
        check(text.contains(QLatin1String("The lamp turned.")) && !text.contains(QLatin1String("Check the time")) && !text.contains(QLatin1String("Which lamp")),
              QStringLiteral("Export Files: comments and inline annotations left out on request"), text);
        check(notes.contains(QLatin1String("Remember the fog bell.")), QStringLiteral("Export Files: the notes in the chosen format (.md)"));
        check(meta.contains(QLatin1String("Title: Night Watch")) && meta.contains(QLatin1String("Keywords: Fog"))
                  && meta.contains(QLatin1String("Point of View: Margaret")) && meta.contains(QLatin1String("Include in Compile: Yes"))
                  && meta.contains(QLatin1String("Bookmark: Tide tables <https://example.org/tides>"))
                  && meta.contains(QLatin1String("Synopsis:\nThe keeper waits for the fog.")) && meta.contains(QRegularExpression(QStringLiteral("Created: \\d{4}-")))
                  && (proj->labels.isEmpty() || meta.contains(QLatin1String("Label: "))) && (proj->statuses.isEmpty() || meta.contains(QLatin1String("Status: "))),
              QStringLiteral("Export Files: the metadata file (label, status, keywords, custom metadata, bookmarks, dates, synopsis)"), meta);
        check(snaps.size() == 1 && snaps.first().endsWith(QLatin1String("First draft.md")),
              QStringLiteral("Export Files: the snapshots in a folder beside the document"), snaps.join(QLatin1Char(',')));
        if (webDoc) {
            const QStringList files = QDir(folder).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            check(files.contains(webDoc->title + QStringLiteral(".html")) && files.contains(QStringLiteral("page-files"))
                      && files.contains(webDoc->title + QStringLiteral(" (notes).md")),
                  QStringLiteral("Export Files: a web page document with its pictures and its notes"), files.join(QLatin1String(", ")));
        }
        // the choices are remembered; a single document needs no folder
        QTemporaryDir out2;
        bool remembered = false, noFolderField = false;
        whenWindow(200, [](QWidget *x) { return qobject_cast<ExportFilesDialog *>(x) != nullptr; }, [&](QWidget *x) {
            auto *dlg = static_cast<ExportFilesDialog *>(x);
            remembered = dlg->findChild<QComboBox *>(QStringLiteral("exportFormat"))->currentData().toString() == QLatin1String("md")
                         && dlg->findChild<QCheckBox *>(QStringLiteral("export_metadata"))->isChecked()
                         && !dlg->findChild<QCheckBox *>(QStringLiteral("export_comments"))->isChecked();
            noFolderField = dlg->findChild<QLineEdit *>(QStringLiteral("exportFolderName"))->isHidden();
            dlg->findChild<QCheckBox *>(QStringLiteral("export_comments"))->setChecked(true);
            auto *fmt = dlg->findChild<QComboBox *>(QStringLiteral("exportFormat"));
            fmt->setCurrentIndex(fmt->findData(QStringLiteral("rtf")));
            dlg->findChild<QLineEdit *>(QStringLiteral("exportLocation"))->setText(out2.path());
            dlg->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        binder->select({chapter}, true);
        settle(100);
        w.findChild<QAction *>(QStringLiteral("exportFiles"))->trigger();
        settle(300);
        const QString rtfText = fileText(out2.path() + QStringLiteral("/Night Watch.rtf"));
        check(remembered && noFolderField, QStringLiteral("Export Files: the choices of last time; no folder name for a single document"));
        check(rtfText.startsWith(QLatin1String("{\\rtf")) && QFileInfo::exists(out2.path() + QStringLiteral("/Night Watch (notes).rtf")),
              QStringLiteral("Export Files: RTF with the notes as RTF"));
        // a picture's notes
        BinderItem *img = importer::importMedia(proj, data + QStringLiteral("/tower.png"), proj->research(), -1);
        QTextDocument inotes;
        inotes.setPlainText(QStringLiteral("Taken from the jetty."));
        proj->docs()->setNotesContent(img->id, &inotes);
        QTemporaryDir out3;
        dnd::ExportOptions o;
        o.format = QStringLiteral("txt");
        o.notes = true;
        dnd::exportItem(proj, img, out3.path(), o);
        check(QFileInfo::exists(out3.path() + QStringLiteral("/tower.png")) && fileText(out3.path() + QStringLiteral("/tower (notes).txt")).contains(QLatin1String("jetty")),
              QStringLiteral("Export Files: a picture as it is, its notes as text (the tutorial's note on media files)"));
    }

    // ================================================================ reopen
    {
        const QString file = proj->indexPath();
        const QString folder = proj->folderPath();
        proj->save();
        w.closeProject();
        settle(200);
        const QString keep = qEnvironmentVariable("QB_TEST_KEEP");
        if (!keep.isEmpty()) {
            QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-r"), folder, keep});
            std::printf("      (project copied to %s)\n", qPrintable(keep));
        }
        QString e;
        auto again = Project::open(file, &e);
        int texts = 0, pages = 0;
        if (again)
            for (BinderItem *c : again->research()->children) {
                if (c->type == ItemType::WebPage)
                    ++pages;
                if (c->type == ItemType::Text && !c->bookmarks.isEmpty())
                    ++texts;
            }
        check(again && pages == 1 && texts >= 5, QStringLiteral("the project opens again with its imported pages and their bookmarks"), e);
    }
    std::printf("%d failure(s)\n", failures);
    return failures;
}
