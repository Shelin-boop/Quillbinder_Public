// Scripted run for version 0.18: the help of the mind maps (page with
// pictures, example board) and File > Compare and Transfer (relatedness with
// what Compile makes, matching chapters, differences in words and bold /
// italic / underline, taking them into the project or into a copy of a Word
// file with tracked changes or of a LibreOffice file, the window with chapter
// tick boxes and step by step).
// Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v018_tests /tmp/shots
#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/IconCatalog.h"
#include "core/ItemIcons.h"
#include "core/MindMap.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "core/ProjectTemplates.h"
#include "core/Zip.h"
#include "import/Compare.h"
#include "import/CompareFiles.h"
#include "import/FileImporter.h"
#include "import/MiniXml.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/ColorPickButton.h"
#include "ui/CompareDialog.h"
#include "ui/EditorPane.h"
#include "ui/FontInstall.h"
#include "ui/IconDialogs.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/MindMapController.h"
#include "ui/MindMapHelp.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapUi.h"
#include "ui/MindMapView.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QRegularExpression>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QProcess>
#include <QStandardPaths>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QUrl>
#include <cstdio>
#include <functional>

using namespace qb;
using namespace qb::mindmap;

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
    settle(250);
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

static void mouse(QWidget *vp, QEvent::Type t, const QPoint &pos, Qt::MouseButton b, Qt::MouseButtons bs,
                  Qt::KeyboardModifiers m = Qt::NoModifier)
{
    QMouseEvent ev(t, QPointF(pos), QPointF(vp->mapToGlobal(pos)), b, bs, m);
    QApplication::sendEvent(vp, &ev);
}

static void drag(QWidget *vp, const QPoint &from, const QPoint &to, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    mouse(vp, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, m);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i)
        mouse(vp, QEvent::MouseMove, from + (to - from) * i / steps, Qt::NoButton, Qt::LeftButton, m);
    mouse(vp, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, m);
    settle(60);
}

static void doubleClick(QWidget *vp, const QPoint &pos, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    mouse(vp, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, m);
    mouse(vp, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, m);
    mouse(vp, QEvent::MouseButtonDblClick, pos, Qt::LeftButton, Qt::LeftButton, m);
    mouse(vp, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, m);
    settle(60);
}

static QStringList menuTexts(QMenu *m)
{
    QStringList l;
    for (QAction *a : m->actions())
        if (!a->isSeparator() && !a->text().isEmpty())
            l << QString(a->text()).remove(QLatin1Char('&'));
    return l;
}

static QMenu *subMenu(QMenu *m, const QString &text)
{
    for (QAction *a : m->actions())
        if (a->menu() && QString(a->text()).remove(QLatin1Char('&')) == text)
            return a->menu();
    return nullptr;
}

static QMenu *menuBarMenu(QMenuBar *mb, const QString &text)
{
    for (QAction *a : mb->actions())
        if (QString(a->text()).remove(QLatin1Char('&')) == text)
            return a->menu();
    return nullptr;
}

static QColor colorAtWord(QTextDocument *doc, const QString &word)
{
    const QTextCursor f = doc->find(word);
    if (f.isNull())
        return QColor();
    QTextCursor c(doc);
    c.setPosition(f.selectionStart() + 1);
    return c.charFormat().foreground().color();
}

static bool highlightedAt(QTextDocument *doc, const QString &word)
{
    const QTextCursor f = doc->find(word);
    if (f.isNull())
        return false;
    QTextCursor c(doc);
    c.setPosition(f.selectionStart() + 1);
    return c.charFormat().property(props::Highlight).toBool();
}

static TextEditor *editorShowing(MainWindow &w, const QString &needle)
{
    for (TextEditor *e : w.findChildren<TextEditor *>())
        if (e->isVisible() && !e->isReadOnly() && e->item() && e->toPlainText().contains(needle))
            return e;
    return nullptr;
}

static void selectWord(TextEditor *e, const QString &word)
{
    const QTextCursor f = e->document()->find(word);
    e->setTextCursor(f);
}

// ------------------------------------------------------------------ files for Compare and Transfer

static QByteArray zipEntry(const QString &path, const QString &name) { return ZipReader(path).fileData(name); }

// a copy of a zip file with one entry changed (as another program would save it)
static bool rewriteEntry(const QString &from, const QString &to, const QString &name, const QByteArray &data)
{
    ZipReader zr(from);
    ZipWriter zw(to);
    for (const QString &n : zr.fileNames())
        zw.addFile(n, n == name ? data : zr.fileData(n), n != QLatin1String("mimetype"));
    return zw.close();
}

// the <w:p ...>...</w:p> that holds a text
static QPair<int, int> wordParagraph(const QString &x, const QString &text)
{
    const int at = x.indexOf(text);
    if (at < 0)
        return {-1, -1};
    int s = x.lastIndexOf(QLatin1String("<w:p>"), at);
    const int s2 = x.lastIndexOf(QLatin1String("<w:p "), at);
    s = qMax(s, s2);
    const int e = x.indexOf(QLatin1String("</w:p>"), at) + 6;
    return {s, e};
}

static QString wordPara(const QString &text, bool heading = false, const QString &style = QStringLiteral("Heading1"))
{
    return (heading ? QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"%1\"/></w:pPr>").arg(style) : QStringLiteral("<w:p>"))
           + QStringLiteral("<w:r><w:t xml:space=\"preserve\">") + text.toHtmlEscaped() + QStringLiteral("</w:t></w:r></w:p>");
}

// the text of a Word file as Word shows it with all changes accepted (or all rejected)
static QStringList wordView(const QString &path, bool rejected)
{
    xml::Document d;
    d.parse(zipEntry(path, QStringLiteral("word/document.xml")));
    QStringList out;
    xml::walk(d.documentElement(), [&](xml::Node *n) {
        if (n->name != QLatin1String("w:p"))
            return true;
        QString t;
        std::function<void(xml::Node *, bool, bool)> visit = [&](xml::Node *e, bool inIns, bool inDel) {
            for (auto &c : e->children) {
                if (c->type != xml::Node::Element)
                    continue;
                const bool ins = inIns || c->name == QLatin1String("w:ins");
                const bool del = inDel || c->name == QLatin1String("w:del");
                if (c->name == QLatin1String("w:pPr"))
                    continue;
                if (c->name == QLatin1String("w:t") && (rejected ? !ins : !del))
                    t += c->textContent();
                else if (c->name == QLatin1String("w:delText") && rejected)
                    t += c->textContent();
                else
                    visit(c.get(), ins, del);
            }
        };
        visit(n, false, false);
        t = t.simplified();
        if (!t.isEmpty())
            out << t;
        return false;
    });
    return out;
}

// a LibreOffice text written by hand: headings, paragraphs, a bold span
static bool writeOdt(const QString &path, const QStringList &paras)
{
    QString body;
    for (const QString &p : paras) {
        if (p.startsWith(QLatin1String("# ")))
            body += QStringLiteral("<text:h text:style-name=\"Heading_20_1\" text:outline-level=\"1\">%1</text:h>").arg(p.mid(2).toHtmlEscaped());
        else
            body += QStringLiteral("<text:p text:style-name=\"Standard\">%1</text:p>").arg(p);
    }
    const QString content = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" office:version=\"1.3\">"
        "<office:automatic-styles><style:style style:name=\"T1\" style:family=\"text\"><style:text-properties fo:font-weight=\"bold\"/></style:style>"
        "<style:style style:name=\"T2\" style:family=\"text\"><style:text-properties fo:font-style=\"italic\"/></style:style></office:automatic-styles>"
        "<office:body><office:text>%1</office:text></office:body></office:document-content>").arg(body);
    const QByteArray manifest =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
        "manifest:version=\"1.3\"><manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/></manifest:manifest>";
    ZipWriter zw(path);
    zw.addFile(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text", false);
    zw.addFile(QStringLiteral("META-INF/manifest.xml"), manifest);
    zw.addFile(QStringLiteral("content.xml"), content.toUtf8());
    return zw.close();
}

struct Story {
    std::unique_ptr<Project> proj;
    BinderItem *ch1 = nullptr, *ch2 = nullptr, *d1 = nullptr, *d2 = nullptr, *d3 = nullptr, *d4 = nullptr, *d5 = nullptr,
               *cut = nullptr, *research = nullptr;
};

static void addPara(QTextCursor &c, const QString &text, bool first = false)
{
    if (!first)
        c.insertBlock();
    c.insertText(text, QTextCharFormat());
}

// a project with five chapters' worth of scenes (and some text Compile leaves out)
static Story makeStory(const QString &dir, const QString &name)
{
    Story s;
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    s.proj = templates::createProject(novel, dir, name, &err);
    Project *p = s.proj.get();
    s.ch1 = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter One"));
    s.d1 = p->model()->createItem(s.ch1, -1, ItemType::Text, QStringLiteral("The Storm"));
    s.d2 = p->model()->createItem(s.ch1, -1, ItemType::Text, QStringLiteral("The Calm"));
    s.d3 = p->model()->createItem(s.ch1, -1, ItemType::Text, QStringLiteral("The Ships"));
    s.ch2 = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter Two"));
    s.d4 = p->model()->createItem(s.ch2, -1, ItemType::Text, QStringLiteral("The Keeper"));
    s.d5 = p->model()->createItem(s.ch2, -1, ItemType::Text, QStringLiteral("The Bell"));
    s.cut = p->model()->createItem(s.ch2, -1, ItemType::Text, QStringLiteral("Cut Scene"));
    s.cut->includeInCompile = false;
    s.research = p->model()->createItem(p->research(), -1, ItemType::Text, QStringLiteral("Notes on Lighthouses"));
    QTextCharFormat italic, bold;
    italic.setFontItalic(true);
    bold.setFontWeight(QFont::Bold);
    {
        QTextCursor c(p->docs()->text(s.d1->id));
        c.insertText(QStringLiteral("The wind rose in the night and the "));
        c.insertText(QStringLiteral("sea"), text::noteFormat(NoteKind::Comment, QStringLiteral("n1"), QStringLiteral("Too dramatic?")));
        c.insertText(QStringLiteral(" came up over the rocks below the tower."), QTextCharFormat());
        c.insertBlock();
        c.insertText(QStringLiteral("Nobody "), QTextCharFormat());
        c.insertText(QStringLiteral("slept"), italic);
        c.insertText(QStringLiteral(" in the keeper's house that night, and the lamp burned until morning."), QTextCharFormat());
        addPara(c, QStringLiteral("When the light came the shore was covered with wood from a ship nobody had seen."));
        addPara(c, QStringLiteral("The keeper walked along the high-water line with a basket, picking up what the sea had left: "
                                  "a cask without a mark, a child's shoe, a length of rope as thick as his wrist, and a board "
                                  "with half a name painted on it in letters he did not recognise."));
    }
    {
        QTextCursor c(p->docs()->text(s.d2->id));
        c.insertText(QStringLiteral("By morning the boats were gone and the keeper counted the "));
        c.insertText(QStringLiteral("gulls"), bold);
        c.insertText(QStringLiteral(" on the rail."), QTextCharFormat());
        addPara(c, QStringLiteral("He never spoke about the night again, not even to his daughter or the priest."));
        addPara(c, QStringLiteral("The calm lasted three days, and on the fourth the fishermen went out again."));
        addPara(c, QStringLiteral("They came back with full nets and empty faces, and in the inn that evening nobody mentioned "
                                  "the ship, the wood on the shore or the bell, as if talking about it could bring another night "
                                  "like that one."));
    }
    {
        QTextCursor c(p->docs()->text(s.d3->id));
        c.insertText(QStringLiteral("Three ships passed the reef at noon, one of them flying a flag nobody knew."));
        addPara(c, QStringLiteral("The keeper watched them through the glass until they were as small as birds."));
        c.insertText(QStringLiteral(" Check the old charts."), text::noteFormat(NoteKind::InlineFootnote, QStringLiteral("n2")));
    }
    {
        QTextCursor c(p->docs()->text(s.d4->id));
        c.insertText(QStringLiteral("The keeper had come to the island as a young man, with one trunk and a dog that died in the "
                                    "first winter. He learned the lamp from the old keeper, who drank, and the weather from the "
                                    "fishermen, who did not trust him for eleven years. When the old keeper died he stayed."));
        addPara(c, QStringLiteral("He married late, a widow from the mainland who knew the names of every bird on the coast and "
                                  "who laughed at his silence until it stopped bothering either of them. Their daughter was born "
                                  "in the year of the great storm, and he wrote her name in the log under the wind readings."));
    }
    {
        QTextCursor c(p->docs()->text(s.d5->id));
        c.insertText(QStringLiteral("The bell rang twice before dawn, and the village woke to the sound of it."));
        addPara(c, QStringLiteral("Nobody could say who had climbed the tower in the dark to ring it."));
        addPara(c, QStringLiteral("The priest said it was the wind, the schoolmaster said it was a prank, and the keeper's "
                                  "daughter said nothing at all but went up the stairs the next evening with a lantern and "
                                  "came down an hour later with dust on her skirt and a look nobody could read."));
    }
    QTextCursor(p->docs()->text(s.cut->id)).insertText(QStringLiteral(
        "This scene was cut: the keeper's brother arrives by boat and stays a week, and they quarrel about the inheritance "
        "until the brother leaves again without saying goodbye to anyone in the village or on the island."));
    QTextCursor(p->docs()->text(s.research->id)).insertText(QStringLiteral(
        "Lighthouses of the northern coast were lit with oil lamps until the end of the century, and the keepers were paid "
        "in kind as often as in money. The towers stood on rocks the sea covered at high tide."));
    p->save(&err);
    return s;
}

static QString docText(Project *p, BinderItem *it) { return p->docs()->text(it->id)->toPlainText(); }

static const compare::Pair *pairOf(const compare::Result &r, const QString &title)
{
    for (const compare::Pair &pr : r.pairs)
        if (pr.title == title)
            return &pr;
    return nullptr;
}

static QString describe(const compare::Result &r)
{
    QStringList l;
    for (const compare::Pair &pr : r.pairs) {
        QStringList ch;
        for (const compare::Change &c : pr.changes)
            ch << QString::number(int(c.kind));
        l << QStringLiteral("%1[%2%3 %4]").arg(pr.title).arg(int(pr.kind)).arg(pr.moved ? QStringLiteral(" moved") : QString(), ch.join(QLatin1Char(',')));
    }
    return QStringLiteral("related %1 %, covered %2 %: ").arg(r.related).arg(r.covered) + l.join(QStringLiteral("; "));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV018Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV018Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    QTemporaryDir work;
    QString err;

    // ============================================================ 1. the comparison
    Story s = makeStory(work.path() + QStringLiteral("/a"), QStringLiteral("Lighthouse"));
    Project *p = s.proj.get();
    const QString compiledDocx = work.path() + QStringLiteral("/compiled.docx");
    {
        compile::Compiler cc(p, compile::formatByName(p->compile.formatName), p->compile, {});
        compile::DocxExporter dx;
        check(dx.exportManuscript(cc.build(), compiledDocx, &err), QStringLiteral("(the project compiled into a Word file)"), err);
    }
    // the editor's version: words, a paragraph less and one more, a chapter less, a new chapter, bold changed
    const QString edited = work.path() + QStringLiteral("/Lighthouse edited.docx");
    QString x = QString::fromUtf8(zipEntry(compiledDocx, QStringLiteral("word/document.xml")));
    int done = 0;
    auto edit = [&](const QString &from, const QString &to) {
        if (x.contains(from)) {
            x.replace(from, to);
            ++done;
        } else {
            info(QStringLiteral("(not in the compiled file: %1)").arg(from.left(80)));
        }
    };
    edit(QStringLiteral("came up over the rocks"), QStringLiteral("came up over the black rocks"));
    {
        const auto r = wordParagraph(x, QStringLiteral("He never spoke about the night"));
        if (r.first >= 0) {
            x.remove(r.first, r.second - r.first);
            ++done;
        }
    }
    {
        const auto r = wordParagraph(x, QStringLiteral("Three ships passed the reef"));
        if (r.first >= 0) {
            x.insert(r.second, wordPara(QStringLiteral("A fourth ship came late in the evening, with torn sails and no flag at all.")));
            ++done;
        }
    }
    for (const QString &t : {QStringLiteral("The keeper had come to the island"), QStringLiteral("He married late")}) {
        const auto r = wordParagraph(x, t);
        if (r.first >= 0) {
            x.remove(r.first, r.second - r.first);
            ++done;
        }
    }
    edit(QStringLiteral(">The bell rang twice"),
         QStringLiteral(">The </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>bell</w:t></w:r><w:r><w:t xml:space=\"preserve\"> rang twice"));
    edit(QStringLiteral("<w:rPr><w:b/><w:bCs/></w:rPr><w:t xml:space=\"preserve\">gulls"), QStringLiteral("<w:t xml:space=\"preserve\">gulls"));
    const QString epilogue1 = QStringLiteral("Years later the light was automated, and the last keeper left the island on a grey morning "
                                             "with his daughter, his books and the log of forty winters in a wooden box.");
    const QString epilogue2 = QStringLiteral("The bell still hangs in the tower. On some nights in autumn, the fishermen say, it rings "
                                             "twice before dawn, although nobody has climbed the stairs for many years now. The new "
                                             "lamp turns without a keeper, the island belongs to the birds again, and the box with "
                                             "the logbook stands in the museum on the mainland, where children press their noses "
                                             "against the glass to read the wind readings and the names.");
    x.insert(x.indexOf(QLatin1String("<w:sectPr")),
             wordPara(QStringLiteral("Epilogue"), true) + wordPara(epilogue1) + wordPara(epilogue2));
    check(done == 7 && rewriteEntry(compiledDocx, edited, QStringLiteral("word/document.xml"), x.toUtf8()),
          QStringLiteral("(the edited Word file made: 7 edits)"), QString::number(done));

    compare::Result res;
    check(compare::compareWithFile(p, edited, &res, &err), QStringLiteral("Compare: the edited Word file is read and compared"), err);
    info(describe(res));
    check(res.related >= 65 && res.related <= 100 && res.canWriteFile,
          QStringLiteral("Compare: related, %1 % of the file's text is in what Compile makes (65 % needed)").arg(res.related));
    const compare::Pair *pd1 = pairOf(res, QStringLiteral("The Storm")), *pd2 = pairOf(res, QStringLiteral("The Calm")),
                        *pd3 = pairOf(res, QStringLiteral("The Ships")), *pd4 = pairOf(res, QStringLiteral("The Keeper")),
                        *pd5 = pairOf(res, QStringLiteral("The Bell")), *pep = pairOf(res, QStringLiteral("Epilogue"));
    check(pd1 && pd1->kind == compare::Pair::Matched && pd1->changes.size() == 1 && pd1->changes[0].kind == compare::Change::Changed
              && pd1->changes[0].ops.size() == 1 && pd1->changes[0].ops[0].type == compare::WordOp::Insert,
          QStringLiteral("Compare: a word added in the file is one change (the comment on another word does not count)"));
    check(pd2 && pd2->changes.size() == 2 && pd2->changes[0].kind == compare::Change::Formatting
              && pd2->changes[1].kind == compare::Change::OnlyInProject,
          QStringLiteral("Compare: bold taken away from a word, a paragraph missing in the file"));
    check(pd3 && pd3->changes.size() == 1 && pd3->changes[0].kind == compare::Change::OnlyInFile,
          QStringLiteral("Compare: a new paragraph in the file (the inline footnote of the project does not count)"));
    check(pd4 && pd4->kind == compare::Pair::OnlyInProject && pd5 && pd5->changes.size() == 1
              && pd5->changes[0].kind == compare::Change::Formatting,
          QStringLiteral("Compare: a scene missing from the file; a word made bold in the file"));
    check(pep && pep->kind == compare::Pair::OnlyInFile && pep->file.size() == 2,
          QStringLiteral("Compare: a new chapter of the file, with its heading as the title"));
    bool noCut = true;
    for (const compare::Pair &pr : res.pairs)
        if (pr.title == QLatin1String("Cut Scene") || pr.title == QLatin1String("Notes on Lighthouses"))
            noCut = false;
    check(noCut, QStringLiteral("Compare: only what Compile takes (not a scene left out of Compile, not the research)"));
    check(pep && compare::defaultIncluded(*pep, compare::Direction::IntoProject) && !compare::defaultIncluded(*pep, compare::Direction::IntoFile)
              && pd4 && !compare::defaultIncluded(*pd4, compare::Direction::IntoProject) && compare::defaultIncluded(*pd4, compare::Direction::IntoFile),
          QStringLiteral("Compare: whole chapters are added by default, never deleted unless ticked"));

    // related to what Compile makes, not to the whole project
    {
        auto relatedFile = [&](const QString &name, const QStringList &paras) {
            const QString path = work.path() + QLatin1Char('/') + name;
            writeOdt(path, paras);
            compare::Result r2;
            compare::compareWithFile(p, path, &r2, &err);
            return r2.related;
        };
        const int fromResearch = relatedFile(QStringLiteral("research.odt"), {docText(p, s.research)});
        const int fromCut = relatedFile(QStringLiteral("cut.odt"), {docText(p, s.cut)});
        const int fromDraft = relatedFile(QStringLiteral("draft.odt"), {docText(p, s.d4)});
        check(fromResearch < 65 && fromCut < 65 && fromDraft >= 65,
              QStringLiteral("Compare: text of the research or of a scene left out of Compile is not related, a scene of the Draft is"),
              QStringLiteral("research %1 %, cut scene %2 %, Draft %3 %").arg(fromResearch).arg(fromCut).arg(fromDraft));
        // a compile group (compile window): only its documents count
        p->compile.compileGroupId = s.ch2->id;
        const int outsideGroup = relatedFile(QStringLiteral("group1.odt"), {docText(p, s.d1)});
        const int insideGroup = relatedFile(QStringLiteral("group2.odt"), {docText(p, s.d5)});
        p->compile.compileGroupId.clear();
        check(outsideGroup < 65 && insideGroup >= 65,
              QStringLiteral("Compare: with a compile group chosen, only the documents of that group count"),
              QStringLiteral("outside %1 %, inside %2 %").arg(outsideGroup).arg(insideGroup));
    }

    // ------------------------------------------------ 2. into the project
    {
        compare::Result r = res;
        compare::Report rep;
        check(compare::apply(p, r, compare::Direction::IntoProject, QString(), &rep, &err),
              QStringLiteral("Transfer into the project"), err);
        info(QStringLiteral("changed %1, added %2, trashed %3, places %4, snapshots %5")
                 .arg(rep.documentsChanged).arg(rep.documentsAdded).arg(rep.documentsTrashed).arg(rep.places).arg(rep.snapshots));
        QTextDocument *t1 = p->docs()->text(s.d1->id);
        const QTextCursor sea = t1->find(QStringLiteral("sea"));
        QTextCursor at(t1);
        at.setPosition(sea.selectionStart() + 1);
        check(docText(p, s.d1).contains(QStringLiteral("came up over the black rocks")) && NoteKind(at.charFormat().intProperty(props::NoteKind)) == NoteKind::Comment,
              QStringLiteral("... the word is in the document, the comment on another word is still there"));
        QTextDocument *t2 = p->docs()->text(s.d2->id);
        const QTextCursor g = t2->find(QStringLiteral("gulls"));
        QTextCursor gc(t2);
        gc.setPosition(g.selectionStart() + 1);
        check(!docText(p, s.d2).contains(QStringLiteral("He never spoke")) && gc.charFormat().fontWeight() < QFont::DemiBold,
              QStringLiteral("... the paragraph the file does not have is gone, \"gulls\" is no longer bold"));
        check(docText(p, s.d3).contains(QStringLiteral("A fourth ship came late")) && docText(p, s.d3).contains(QStringLiteral("Check the old charts")),
              QStringLiteral("... the new paragraph is there, the inline footnote too"));
        QTextDocument *t5 = p->docs()->text(s.d5->id);
        const QTextCursor b = t5->find(QStringLiteral("bell"));
        QTextCursor bc(t5);
        bc.setPosition(b.selectionStart() + 1);
        check(bc.charFormat().fontWeight() >= QFont::Bold, QStringLiteral("... \"bell\" is bold"));
        BinderItem *ep = nullptr;
        for (BinderItem *c : s.ch2->children)
            if (c->title == QLatin1String("Epilogue"))
                ep = c;
        check(ep && ep->row() == s.d5->row() + 1 && docText(p, ep).contains(QStringLiteral("Years later the light was automated")),
              QStringLiteral("... the new chapter is a new document after the scene before it"));
        check(p->item(s.d4->id) && s.d4->parent == s.ch2, QStringLiteral("... the scene missing from the file stays (not ticked)"));
        check(rep.snapshots == 4 && !snapshots::list(p, s.d1->id).isEmpty()
                  && snapshots::list(p, s.d1->id).first().title.startsWith(QStringLiteral("Before Compare and Transfer")),
              QStringLiteral("... every changed document has a snapshot from before"), QString::number(rep.snapshots));
        compare::Result again;
        compare::compareWithFile(p, edited, &again, &err);
        int left = 0;
        for (const compare::Pair &pr : again.pairs)
            if (pr.kind == compare::Pair::Matched)
                left += int(pr.changes.size());
        check(left == 0, QStringLiteral("... compared again: no differences left in the documents"), describe(again));
    }

    // ------------------------------------------------ 3. into a copy of the Word file
    Story s2 = makeStory(work.path() + QStringLiteral("/b"), QStringLiteral("Lighthouse"));
    Project *p2 = s2.proj.get();
    {
        compare::Result r;
        compare::compareWithFile(p2, edited, &r, &err);
        for (compare::Pair &pr : r.pairs)
            pr.included = compare::defaultIncluded(pr, compare::Direction::IntoFile);
        const QString out = compare::suggestedCopyPath(edited);
        compare::Report rep;
        check(out.endsWith(QStringLiteral("Lighthouse edited (Quillbinder).docx"))
                  && compare::apply(p2, r, compare::Direction::IntoFile, out, &rep, &err) && QFile::exists(out),
              QStringLiteral("Transfer into the file: a copy beside it, \"... (Quillbinder).docx\""), err);
        const QString xo = QString::fromUtf8(zipEntry(out, QStringLiteral("word/document.xml")));
        check(xo.contains(QStringLiteral("<w:ins ")) && xo.contains(QStringLiteral("<w:del ")) && xo.contains(QStringLiteral("<w:rPrChange "))
                  && xo.contains(QStringLiteral("w:author=\"Quillbinder\"")) && xo.contains(QStringLiteral("<w:delText")),
              QStringLiteral("... the changes are tracked changes by \"Quillbinder\" (insertions, deletions, formatting)"));
        check(wordView(out, true) == wordView(edited, false),
              QStringLiteral("... all changes rejected: the file as it was"), wordView(out, true).join(QStringLiteral(" | ")).left(1500));
        compare::Result after;
        compare::compareWithFile(p2, out, &after, &err);
        int left = 0;
        bool keeperIn = false;
        for (const compare::Pair &pr : after.pairs) {
            if (pr.kind == compare::Pair::Matched)
                left += int(pr.changes.size());
            if (pr.title == QLatin1String("The Keeper") && pr.kind == compare::Pair::Matched)
                keeperIn = true;
        }
        check(left == 0 && keeperIn, QStringLiteral("... all changes accepted: the file has the project's text (also the missing scene)"),
              describe(after));
        {
            // the first scene of a chapter goes after the chapter's heading, not at the end of the chapter before
            const QStringList v = wordView(out, false);
            int heading = -1, keeper = -1, bell = -1, ships = -1;
            for (int i = 0; i < v.size(); ++i) {
                if (v[i] == QLatin1String("Chapter Three"))
                    heading = i;
                if (v[i].startsWith(QLatin1String("The keeper had come")))
                    keeper = i;
                if (v[i].startsWith(QLatin1String("The bell rang")))
                    bell = i;
                if (v[i].startsWith(QLatin1String("The keeper watched them")))
                    ships = i;
            }
            check(ships >= 0 && ships < heading && heading < keeper && keeper < bell,
                  QStringLiteral("... the missing first scene of a chapter is put after the chapter's heading"),
                  QStringLiteral("ships %1, heading %2, keeper %3, bell %4").arg(ships).arg(heading).arg(keeper).arg(bell));
        }
        bool sameRest = true;
        ZipReader a(edited), b2(out);
        for (const QString &n : a.fileNames())
            if (n != QLatin1String("word/document.xml") && a.fileData(n) != b2.fileData(n))
                sameRest = false;
        check(sameRest && a.fileNames() == b2.fileNames() && xo.contains(QStringLiteral("rIdHeader")),
              QStringLiteral("... everything else in the file is as it was (styles, header, page)"));
        // LibreOffice (when installed) sees the tracked changes
        const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
        if (!soffice.isEmpty()) {
            QTemporaryDir lo;
            QProcess pr;
            pr.start(soffice, {QStringLiteral("-env:UserInstallation=file://") + lo.path() + QStringLiteral("/profile"), QStringLiteral("--headless"),
                               QStringLiteral("--convert-to"), QStringLiteral("odt"), QStringLiteral("--outdir"), lo.path(), out});
            pr.waitForFinished(180000);
            const QString odt = lo.path() + QStringLiteral("/Lighthouse edited (Quillbinder).odt");
            const QByteArray c = zipEntry(odt, QStringLiteral("content.xml"));
            check(c.contains("<text:tracked-changes") && c.contains("<text:insertion") && c.contains("<text:deletion"),
                  QStringLiteral("... LibreOffice reads them as tracked changes (insertions and deletions)"), QString::fromUtf8(c.left(300)));
        } else {
            info(QStringLiteral("(LibreOffice not installed: its reading of the tracked changes not checked here)"));
        }
    }

    // ------------------------------------------------ 4. LibreOffice files (changes made directly)
    {
        Story s3 = makeStory(work.path() + QStringLiteral("/c"), QStringLiteral("Lighthouse"));
        Project *p3 = s3.proj.get();
        const QString odt = work.path() + QStringLiteral("/Lighthouse.odt");
        QStringList paras;
        paras << QStringLiteral("# The Storm")
              << QStringLiteral("The wind rose in the night and the sea came up over the black rocks below the tower.")
              << QStringLiteral("Nobody <text:span text:style-name=\"T2\">slept</text:span> in the keeper's house that night, and the lamp burned until morning.")
              << QStringLiteral("When the light came the shore was covered with wood from a ship nobody had seen.")
              << QStringLiteral("# The Calm")
              << QStringLiteral("By morning the boats were gone and the keeper counted the <text:span text:style-name=\"T1\">gulls</text:span> on the rail.")
              << QStringLiteral("He never spoke about the night again,<text:s/> not even to his daughter or the priest.")
              << QStringLiteral("The calm lasted three days, and on the fourth the fishermen went out again.")
              << QStringLiteral("# The Bell")
              << QStringLiteral("The <text:span text:style-name=\"T1\">bell</text:span> rang twice before dawn, and the village woke to the sound of it.")
              << QStringLiteral("Nobody could say who had climbed the tower in the dark to ring it.")
              << QStringLiteral("# The Ships")
              << QStringLiteral("Three ships passed the reef at noon, one of them flying a flag nobody knew.")
              << QStringLiteral("The keeper watched them through the glass until they were as small as birds.");
        writeOdt(odt, paras);
        compare::Result r;
        const bool read = compare::compareWithFile(p3, odt, &r, &err);
        check(read && r.canWriteFile && r.related >= 65, QStringLiteral("LibreOffice file: read and related (%1 %)").arg(r.related), err);
        info(describe(r));
        const compare::Pair *bell = pairOf(r, QStringLiteral("The Bell")), *ships = pairOf(r, QStringLiteral("The Ships"));
        check(bell && ships && (bell->moved || ships->moved), QStringLiteral("... a scene in another order than in Compile is marked as moved"));
        for (compare::Pair &pr : r.pairs)
            pr.included = compare::defaultIncluded(pr, compare::Direction::IntoFile);
        const QString out = compare::suggestedCopyPath(odt);
        compare::Report rep;
        check(compare::apply(p3, r, compare::Direction::IntoFile, out, &rep, &err), QStringLiteral("... the project's text into a copy"), err);
        compare::Result after;
        compare::compareWithFile(p3, out, &after, &err);
        int left = 0;
        for (const compare::Pair &pr : after.pairs)
            if (pr.kind == compare::Pair::Matched)
                left += int(pr.changes.size());
        check(left == 0, QStringLiteral("... read again: the words and the bold of the project (\"black\" gone, \"bell\" plain, \"gulls\" bold)"),
              describe(after));
        ZipReader z(out);
        check(z.fileNames().value(0) == QLatin1String("mimetype"), QStringLiteral("... still a LibreOffice file (mimetype first)"));
        // and the other way: the file's words into the project
        compare::Result back;
        compare::compareWithFile(p3, odt, &back, &err);
        check(compare::apply(p3, back, compare::Direction::IntoProject, QString(), &rep, &err)
                  && docText(p3, s3.d1).contains(QStringLiteral("black rocks")),
              QStringLiteral("... and from the LibreOffice file into the project"));
    }
    // a file that can only be read
    {
        const QString txt = work.path() + QStringLiteral("/plain.txt");
        QFile f(txt);
        f.open(QIODevice::WriteOnly);
        f.write("The wind rose in the night and the sea came up over the rocks below the tower.\n");
        f.close();
        compare::Result r;
        compare::compareWithFile(p2, txt, &r, &err);
        compare::Report rep;
        check(!r.canWriteFile && !compare::apply(p2, r, compare::Direction::IntoFile, txt + QStringLiteral(".out"), &rep, &err),
              QStringLiteral("A plain text file: compared, but only the project side can be changed"));
    }
    // a long book: about 100,000 words in 60 scenes, a few places changed in the Word file
    {
        Story big = makeStory(work.path() + QStringLiteral("/big"), QStringLiteral("Long Book"));
        Project *pb = big.proj.get();
        QStringList vocab;
        const QStringList syl = {QStringLiteral("ka"), QStringLiteral("lo"), QStringLiteral("mi"), QStringLiteral("ren"), QStringLiteral("tu"),
                                 QStringLiteral("sa"), QStringLiteral("vel"), QStringLiteral("or"), QStringLiteral("ne"), QStringLiteral("pith"),
                                 QStringLiteral("da"), QStringLiteral("gor"), QStringLiteral("wi"), QStringLiteral("han"), QStringLiteral("bu")};
        for (const QString &a : syl)
            for (const QString &b : syl)
                for (const QString &c : {QString(), QStringLiteral("s"), QStringLiteral("en"), QStringLiteral("ly")})
                    vocab << a + b + c;
        quint32 seed = 12345;
        auto rnd = [&](int n) {
            seed = seed * 1664525u + 1013904223u;
            return int((seed >> 8) % quint32(n));
        };
        BinderItem *part = pb->model()->createItem(pb->draft(), -1, ItemType::Folder, QStringLiteral("Part Two"));
        QList<BinderItem *> scenes;
        QHash<QString, QStringList> parasOf;
        for (int d = 0; d < 60; ++d) {
            BinderItem *it = pb->model()->createItem(part, -1, ItemType::Text, QStringLiteral("Scene %1").arg(d + 1));
            scenes << it;
            QTextCursor c(pb->docs()->text(it->id));
            for (int k = 0; k < 30; ++k) {
                QStringList w;
                for (int j = 0; j < 55; ++j)
                    w << vocab.at(rnd(int(vocab.size())));
                w[0][0] = w[0][0].toUpper();
                const QString para = w.join(QLatin1Char(' ')) + QLatin1Char('.');
                parasOf[it->id] << para;
                if (k)
                    c.insertBlock();
                c.insertText(para, QTextCharFormat());
            }
        }
        const QString bigDocx = work.path() + QStringLiteral("/long book.docx");
        QElapsedTimer t;
        t.start();
        {
            compile::Compiler cc(pb, compile::formatByName(pb->compile.formatName), pb->compile, {});
            compile::DocxExporter dx;
            dx.exportManuscript(cc.build(), bigDocx, &err);
        }
        const qint64 tCompile = t.restart();
        // the editor's changes: a word added in three scenes, a paragraph taken out of a fourth
        QString bx = QString::fromUtf8(zipEntry(bigDocx, QStringLiteral("word/document.xml")));
        int made = 0;
        for (int d : {4, 21, 47}) {
            const QStringList w = parasOf.value(scenes[d]->id).at(3).split(QLatin1Char(' '));
            const QString from = w.mid(0, 6).join(QLatin1Char(' '));
            if (bx.contains(from)) {
                bx.replace(from, w.mid(0, 3).join(QLatin1Char(' ')) + QStringLiteral(" suddenly ") + w.mid(3, 3).join(QLatin1Char(' ')));
                ++made;
            }
        }
        {
            const QStringList w = parasOf.value(scenes[33]->id).at(10).split(QLatin1Char(' '));
            const auto r = wordParagraph(bx, w.mid(0, 6).join(QLatin1Char(' ')));
            if (r.first >= 0) {
                bx.remove(r.first, r.second - r.first);
                ++made;
            }
        }
        const QString bigEdited = work.path() + QStringLiteral("/long book edited.docx");
        rewriteEntry(bigDocx, bigEdited, QStringLiteral("word/document.xml"), bx.toUtf8());
        t.restart();
        compare::Result r;
        const bool ok = compare::compareWithFile(pb, bigEdited, &r, &err);
        const qint64 tCompare = t.restart();
        int changed = 0, pairsWithChanges = 0;
        for (const compare::Pair &pr : r.pairs)
            if (pr.kind == compare::Pair::Matched && !pr.changes.isEmpty()) {
                changed += int(pr.changes.size());
                ++pairsWithChanges;
            }
        check(ok && made == 4 && r.related >= 95 && changed == 4 && pairsWithChanges == 4,
              QStringLiteral("A long book (%1 words): the four changed places are found").arg(r.compiledWords),
              QStringLiteral("made %1, related %2, changes %3 in %4 documents").arg(made).arg(r.related).arg(changed).arg(pairsWithChanges));
        for (compare::Pair &pr : r.pairs)
            pr.included = compare::defaultIncluded(pr, compare::Direction::IntoFile);
        compare::Report rep;
        const bool wrote = compare::apply(pb, r, compare::Direction::IntoFile, compare::suggestedCopyPath(bigEdited), &rep, &err);
        const qint64 tWrite = t.restart();
        compare::Result r2;
        compare::compareWithFile(pb, bigEdited, &r2, &err);
        const bool intoProject = compare::apply(pb, r2, compare::Direction::IntoProject, QString(), &rep, &err);
        const qint64 tProject = t.restart();
        info(QStringLiteral("(times: compile %1 ms, compare %2 ms, the Word copy %3 ms, compare and transfer into the project %4 ms)")
                 .arg(tCompile).arg(tCompare).arg(tWrite).arg(tProject));
        check(wrote && intoProject && tCompare < 20000 && tWrite < 20000 && tProject < 40000,
              QStringLiteral("... compared and transferred both ways in seconds (compare %1 s)").arg(double(tCompare) / 1000.0, 0, 'f', 1), err);
    }


    // ============================================================ 5. the window: menu, question, chapters, steps
    Story s4 = makeStory(work.path() + QStringLiteral("/d"), QStringLiteral("Lighthouse"));
    Project *p4 = s4.proj.get();
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1500, 980);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(s4.proj)), QStringLiteral("(project open)"));
    settle(400);
    {
        QMenu *file = menuBarMenu(w.menuBar(), QStringLiteral("File"));
        QMenu *cmp = file ? subMenu(file, QStringLiteral("Compare and Transfer")) : nullptr;
        check(cmp && menuTexts(cmp) == QStringList({QStringLiteral("Compare with File…"), QStringLiteral("By Chapter"), QStringLiteral("Step by Step")})
                  && byKey(&w, QStringLiteral("compareByChapter"))->isCheckable() && byKey(&w, QStringLiteral("compareByChapter"))->isChecked()
                  && !byKey(&w, QStringLiteral("compareStepByStep"))->isChecked() && byKey(&w, QStringLiteral("compareFile"))->isEnabled(),
              QStringLiteral("File > Compare and Transfer: Compare with File, and the tick boxes By Chapter (on) and Step by Step"),
              cmp ? menuTexts(cmp).join(QStringLiteral(" | ")) : QString());
    }
    // a file of another book: the question
    {
        const QString other = work.path() + QStringLiteral("/other.odt");
        writeOdt(other, {QStringLiteral("# A Different Book"),
                         QStringLiteral("The desert road ran straight for two hundred kilometres, and the bus stopped only once, at a petrol station with a dog asleep under the pump."),
                         QStringLiteral("Maria counted the poles of the power line to stay awake and lost count somewhere after four hundred.")});
        bool asked = false;
        QString question;
        withModal<QMessageBox>(400, [&](QMessageBox *m) {
            asked = m->objectName() == QLatin1String("compareNotRelated");
            question = m->text();
            m->grab().save(outDir + QStringLiteral("/v018-01-not-related.png"));
            m->reject();
        });
        const bool went = w.compareWithFile(other);
        check(asked && !went && question.contains(QStringLiteral("65 %")),
              QStringLiteral("Compare with a file of another book: \"only 0 % ... 65 % are needed\", Cancel stops"), question);
    }
    // by chapter: tick boxes, a change skipped, the transfer
    {
        bool opened = false, transferred = false;
        int rows = 0, checkable = 0;
        QString calmHtml;
        withModal<CompareDialog>(500, [&](CompareDialog *d) {
            opened = true;
            QTreeWidget *list = d->chapterList();
            rows = list->topLevelItemCount();
            for (int r = 0; r < rows; ++r)
                if (list->topLevelItem(r)->flags() & Qt::ItemIsUserCheckable)
                    ++checkable;
            for (int r = 0; r < rows; ++r)
                if (list->topLevelItem(r)->text(0) == QLatin1String("The Calm"))
                    list->setCurrentItem(list->topLevelItem(r));
            settle(150);
            calmHtml = d->view()->toHtml();
            d->grab().save(outDir + QStringLiteral("/v018-02-compare-by-chapter.png"));
            // the comment on "sea" ... the first change of The Storm skipped by its link
            for (int r = 0; r < rows; ++r)
                if (list->topLevelItem(r)->text(0) == QLatin1String("The Storm")) {
                    list->setCurrentItem(list->topLevelItem(r));
                    const int pi = list->topLevelItem(r)->data(0, Qt::UserRole).toInt();
                    emit d->view()->anchorClicked(QUrl(QStringLiteral("toggle:%1:0").arg(pi)));
                }
            // the new chapter unticked
            for (int r = 0; r < rows; ++r)
                if (list->topLevelItem(r)->text(0) == QLatin1String("Epilogue"))
                    list->topLevelItem(r)->setCheckState(0, Qt::Unchecked);
            settle(100);
            withModal<QMessageBox>(400, [&](QMessageBox *m) {
                transferred = m->objectName() == QLatin1String("compareReport") && m->text().contains(QStringLiteral("snapshot"));
                m->grab().save(outDir + QStringLiteral("/v018-03-report-project.png"));
                m->accept();
            });
            d->findChild<QPushButton *>(QStringLiteral("compareTransfer"))->click();
        });
        w.compareWithFile(edited);
        settle(300);
        check(opened && rows >= 7 && checkable >= 6,
              QStringLiteral("Compare and Transfer window: the chapters with tick boxes (%1 rows, %2 to tick)").arg(rows).arg(checkable));
        check(calmHtml.contains(QStringLiteral("line-through")) && calmHtml.contains(QStringLiteral("He never spoke")),
              QStringLiteral("... a chapter's changes: what goes is struck through in red, what comes is green"));
        BinderItem *ep = nullptr;
        for (BinderItem *c : s4.ch2->children)
            if (c->title == QLatin1String("Epilogue"))
                ep = c;
        check(transferred && !docText(p4, s4.d1).contains(QStringLiteral("black rocks")) && !docText(p4, s4.d2).contains(QStringLiteral("He never spoke"))
                  && docText(p4, s4.d3).contains(QStringLiteral("A fourth ship")) && !ep,
              QStringLiteral("... Transfer: the skipped change and the unticked chapter are left out, the rest is in the project"));
    }
    // step by step: take, skip, ... into a copy of the file
    {
        Story s5 = makeStory(work.path() + QStringLiteral("/e"), QStringLiteral("Lighthouse"));
        auto *w2 = new MainWindow;
        w2->resize(1400, 900);
        w2->show();
        w2->adoptProject(std::move(s5.proj));
        settle(300);
        byKey(w2, QStringLiteral("compareStepByStep"))->setChecked(true);
        QStringList labels;
        int steps = 0;
        bool reportOk = false;
        QString out;
        withModal<CompareDialog>(500, [&](CompareDialog *d) {
            d->setDirection(compare::Direction::IntoFile);
            settle(100);
            d->setStepByStep(false);
            d->setStepByStep(true);
            settle(100);
            auto *label = d->findChild<QLabel *>(QStringLiteral("compareStepLabel"));
            labels << label->text();
            d->grab().save(outDir + QStringLiteral("/v018-04-compare-step-by-step.png"));
            // chapters only in the file (title page, epilogue) and the first place of "The Calm" (bold of "gulls")
            // skipped, everything else taken
            bool calmSeen = false;
            for (int i = 0; i < 40 && d->findChild<QPushButton *>(QStringLiteral("compareTake"))->isEnabled(); ++i) {
                const QPair<int, int> st = d->currentStep();
                const compare::Pair &pr = d->result().pairs[st.first];
                bool skip = pr.kind == compare::Pair::OnlyInFile;
                if (pr.title == QLatin1String("The Calm") && !calmSeen) {
                    calmSeen = true;
                    skip = true;
                }
                d->findChild<QPushButton *>(skip ? QStringLiteral("compareSkip") : QStringLiteral("compareTake"))->click();
                if (i == 0)
                    labels << label->text();
                ++steps;
            }
            labels << label->text();
            out = d->outputPath();
            d->grab().save(outDir + QStringLiteral("/v018-05-compare-steps-done.png"));
            withModal<QMessageBox>(400, [&](QMessageBox *m) {
                reportOk = m->text().contains(QStringLiteral("Word or LibreOffice")) && m->findChildren<QPushButton *>().size() >= 2;
                m->grab().save(outDir + QStringLiteral("/v018-06-report-file.png"));
                m->accept();
            });
            d->findChild<QPushButton *>(QStringLiteral("compareTransfer"))->click();
        });
        w2->compareWithFile(edited);
        settle(300);
        check(labels.value(0).startsWith(QStringLiteral("Change 1 of")) && labels.value(1).startsWith(QStringLiteral("Change 2 of"))
                  && labels.value(2).startsWith(QStringLiteral("All changes seen")),
              QStringLiteral("Step by Step: one change at a time (Take, Skip), then \"All changes seen\""), labels.join(QStringLiteral(" | ")));
        // the second change (bold of "gulls") was skipped: the copy keeps the file's plain "gulls"
        const QString xo = QString::fromUtf8(zipEntry(out, QStringLiteral("word/document.xml")));
        const QStringList accepted = wordView(out, false);
        check(reportOk && QFile::exists(out) && xo.contains(QStringLiteral("<w:ins ")) && accepted.join(QLatin1Char(' ')).contains(QStringLiteral("came up over the rocks"))
                  && !accepted.join(QLatin1Char(' ')).contains(QStringLiteral("black rocks"))
                  && accepted.join(QLatin1Char(' ')).contains(QStringLiteral("Years later the light was automated")),
              QStringLiteral("... into a copy of the Word file with tracked changes (the skipped new chapter stays); the report offers to open it"));
        int rprChanges = int(xo.count(QStringLiteral("<w:rPrChange ")));
        check(rprChanges == 1, QStringLiteral("... the skipped change (bold of one word) is not in the copy, the taken one is"),
              QString::number(rprChanges));
        w2->close();
        delete w2;
        byKey(&w, QStringLiteral("compareStepByStep"))->setChecked(false);
        AppSettings::instance().compareStepByStep = false;
    }
    // the copy as LibreOffice shows it (when installed): a picture of its first page
    {
        const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
        const QString copy = compare::suggestedCopyPath(edited);
        if (!soffice.isEmpty() && QFile::exists(copy)) {
            QTemporaryDir lo;
            QProcess pr;
            pr.start(soffice, {QStringLiteral("-env:UserInstallation=file://") + lo.path() + QStringLiteral("/profile"), QStringLiteral("--headless"),
                               QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), lo.path(), copy});
            pr.waitForFinished(180000);
            const QString pdf = lo.path() + QLatin1Char('/') + QFileInfo(copy).completeBaseName() + QStringLiteral(".pdf");
            QProcess pp;
            pp.start(QStringLiteral("pdftoppm"), {QStringLiteral("-r"), QStringLiteral("70"), QStringLiteral("-png"), QStringLiteral("-f"), QStringLiteral("3"),
                                                 QStringLiteral("-l"), QStringLiteral("4"), pdf, outDir + QStringLiteral("/v018-07-copy-in-libreoffice")});
            pp.waitForFinished(60000);
            info(QStringLiteral("(LibreOffice's PDF of the copy: %1)").arg(QFileInfo::exists(pdf) ? QStringLiteral("made") : QStringLiteral("not made")));
        }
    }
    // the whole file at once (neither tick box)
    {
        byKey(&w, QStringLiteral("compareByChapter"))->setChecked(false);
        bool listHidden = false;
        withModal<CompareDialog>(500, [&](CompareDialog *d) {
            listHidden = !d->chapterList()->isVisible();
            d->grab().save(outDir + QStringLiteral("/v018-08-compare-whole-file.png"));
            d->reject();
        });
        w.compareWithFile(edited);
        byKey(&w, QStringLiteral("compareByChapter"))->setChecked(true);
        check(listHidden, QStringLiteral("... without By Chapter: all changes in one list, no chapter tick boxes"));
    }

    // ============================================================ 6. the mind map help
    {
        QMenu *help = menuBarMenu(w.menuBar(), QStringLiteral("Help"));
        check(help && menuTexts(help).contains(QStringLiteral("Mind Map Help")), QStringLiteral("Help > Mind Map Help"));
        byKey(&w, QStringLiteral("mindMapHelp"))->trigger();
        settle(300);
        QDialog *hw = nullptr;
        for (QWidget *t : QApplication::topLevelWidgets())
            if (t->objectName() == QLatin1String("mindMapHelp") && t->isVisible())
                hw = qobject_cast<QDialog *>(t);
        auto *tb = hw ? hw->findChild<QTextBrowser *>(QStringLiteral("mindMapHelpText")) : nullptr;
        const QString text = tb ? tb->toPlainText() : QString();
        const QImage arrow = tb ? tb->document()->resource(QTextDocument::ImageResource, QUrl(QStringLiteral("qbmm:arrow"))).value<QImage>() : QImage();
        int images = 0;
        if (tb)
            for (QTextBlock b = tb->document()->begin(); b.isValid(); b = b.next())
                for (auto it = b.begin(); !it.atEnd(); ++it)
                    if (it.fragment().charFormat().isImageFormat()
                        && it.fragment().charFormat().toImageFormat().name().startsWith(QLatin1String("qbmm:")))
                        ++images;
        check(hw && text.contains(QStringLiteral("drag it onto another note")) && text.contains(QStringLiteral("Ctrl+Alt+Shift"))
                  && arrow.width() > 200 && images == 8,
              QStringLiteral("Mind Map Help: how lines and arrows are drawn, with 8 pictures drawn by the board itself"),
              QStringLiteral("%1 pictures in the page").arg(images));
        if (hw) {
            hw->resize(760, 900);
            hw->grab().save(outDir + QStringLiteral("/v018-09-mind-map-help.png"));
            tb->scrollToAnchor(QString());
            tb->verticalScrollBar()->setValue(tb->verticalScrollBar()->maximum() * 2 / 5);
            settle(100);
            hw->grab().save(outDir + QStringLiteral("/v018-10-mind-map-help-arrows.png"));
        }
        // every picture is there
        bool all = true;
        for (const char *n : {"notes", "drag", "line", "arrow", "arrowback", "both", "label", "shape"})
            if (mindmaphelp::picture(QLatin1String(n)).isNull())
                all = false;
        check(all, QStringLiteral("... eight pictures: notes, dragging, line, arrow, arrow back, both ends, label, background shape"));
        // the menu places the help names
        QFile mf(QStringLiteral(":/docs/mindmap-help.md"));
        mf.open(QIODevice::ReadOnly);
        const QString md = QString::fromUtf8(mf.readAll());
        static const QRegularExpression re(QStringLiteral("\\*\\*Menu > ([^*\\n]+)\\*\\*"));
        MindMapController ctlProbe(new MindMapDoc(nullptr, QString(), &w), new MindMapView(&w), &w, false);
        QStringList missing;
        auto it = re.globalMatch(md);
        int places = 0;
        while (it.hasNext()) {
            const QStringList parts = it.next().captured(1).split(QStringLiteral(" > "));
            ++places;
            QMenu m;
            const QString top = parts.value(0);
            if (top == QLatin1String("Notes"))
                ctlProbe.fillNotesMenu(&m);
            else if (top == QLatin1String("Edit"))
                ctlProbe.fillEditMenu(&m);
            else if (top == QLatin1String("File"))
                ctlProbe.fillFileMenu(&m);
            QString want = parts.value(1);
            bool found = false;
            for (QAction *a : m.actions())
                if (QString(a->text()).remove(QLatin1Char('&')) == want)
                    found = true;
            if (!found)
                missing << parts.join(QStringLiteral(" > "));
        }
        check(places >= 3 && missing.isEmpty(), QStringLiteral("... the menu places it names are in the board's menus"), missing.join(QStringLiteral("; ")));
        // the example board
        auto *ex = hw ? hw->findChild<QPushButton *>(QStringLiteral("openExampleBoard")) : nullptr;
        if (ex)
            ex->click();
        settle(400);
        MindMapWindow *mw = nullptr;
        for (QWidget *t : QApplication::topLevelWidgets())
            if (auto *m = qobject_cast<MindMapWindow *>(t))
                if (m->isVisible() && m->windowTitle() == QLatin1String("Mind Map: Example Board"))
                    mw = m;
        check(mw && mw->controller()->doc()->board.notes.size() >= 15 && !mw->controller()->doc()->board.shapes.isEmpty()
                  && !mw->controller()->doc()->project(),
              QStringLiteral("Open Example Board: a practice board in a window of its own, not part of the project"));
        if (mw) {
            mw->resize(1200, 800);
            mw->view()->zoomToFit();
            settle(200);
            mw->grab().save(outDir + QStringLiteral("/v018-11-example-board.png"));
            Board &b = mw->controller()->doc()->board;
            int dragMe = 0, dropHere = 0, alt = 0, dropHere2 = 0;
            for (const Note &n : b.notes) {
                if (n.text == QLatin1String("Drag me"))
                    dragMe = n.id;
                if (n.text == QLatin1String("Drag me with Alt"))
                    alt = n.id;
                if (n.text == QLatin1String("Drop here"))
                    (dropHere ? dropHere2 : dropHere) = n.id;
            }
            MindMapView *v = mw->view();
            mw->activateWindow();
            v->setFocus();
            settle(100);
            drag(v->viewport(), v->mapFromScene(b.rectOf(dragMe).center()), v->mapFromScene(b.rectOf(dropHere).center()));
            settle(100);
            drag(v->viewport(), v->mapFromScene(b.rectOf(alt).center()), v->mapFromScene(b.rectOf(dropHere2).center()), Qt::AltModifier);
            settle(200);
            check(b.isConnected(dragMe, dropHere) && !b.pointsTo(dragMe, dropHere) && b.pointsTo(alt, dropHere2),
                  QStringLiteral("... on it: a note dropped on another draws a line, with Alt an arrow"));
            mw->grab().save(outDir + QStringLiteral("/v018-12-example-board-practised.png"));
            mw->close();
            settle(200);
        }
        check(!QFile::exists(mindmap::pathFor(p4->contentDir(p4->draft()->id))), QStringLiteral("... and nothing of it is saved in the project"));
        if (hw)
            hw->close();
        // the board's own "?" and Help menu
        auto *inspector = w.findChild<Inspector *>();
        inspector->showTab(Inspector::MindMap);
        settle(300);
        MindMapPanel *panel = inspector->mindMapPanel();
        auto *q = panel ? panel->findChild<QToolButton *>(QStringLiteral("mmHelpButton")) : nullptr;
        check(q && q->isVisible(), QStringLiteral("... the mind map in the inspector has a \"?\" button for the help"));
    }
    std::printf("%d failure(s)\n", failures);
    return failures;
}
