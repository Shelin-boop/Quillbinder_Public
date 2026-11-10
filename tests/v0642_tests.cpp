// Scripted run for version 0.64.2, the fixes of the system test's full run of 0.64.1 (2026-11-10):
//  - a linked note over text of several formats (crashed: fragment iterator used after a format change);
//  - menus filled when they open (Go To ...): an entry of a document deleted since does nothing (crashed);
//  - word counts: combining accents, letters outside the first Unicode plane, 3,500 and 1.000.000;
//  - a picture's format on letters: text after a picture is text, pasted pictures keep their picture;
//  - subscript and superscript at the end of a word act on what is typed next;
//  - RTF: a face name that says italic gives italics also where fonts are not kept;
//  - German quotes: ' after a word closes an open ‚ (‘), an apostrophe inside a word stays ’;
//  - Markdown storage: a paragraph starting with a space, an empty paragraph aligned by hand;
//  - projects: the spare copy names its project, a project folder that is gone is not made anew;
//  - Scrivener format: unchanged label colors and top folders stay as they were, the Device stamp alone
//    writes nothing, a synopsis keeps its leading space, a cut-off save's file is not a document, pictures in
//    notes, a .scriv that is gone is not made anew, a text changed in the .scriv while open is read at the next
//    opening, Scrivener's lock in XML form;
//  - a second start with a project that is open: shown in its window (MainWindow::showIfOpen);
//  - the Documents menu names the label and status lists as the project does.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0642_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "compile/PluginManager.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/DirectFormat.h"
#include "text/AutoCorrect.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QMessageBox>
#include <QPointer>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
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
            std::printf("      %s\n", qPrintable(detail.left(3000)));
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

static void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

// closes every message box that comes up (notices after opening a project and the like)
static void closeBoxesLater(int ms)
{
    QTimer::singleShot(ms, qApp, [] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible())
                b->accept();
    });
}

static QByteArray readAll(const QString &f)
{
    QFile file(f);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static void writeAll(const QString &f, const QByteArray &data)
{
    QFile file(f);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

static QMap<QString, QByteArray> filesIn(const QString &dir)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        out.insert(QDir(dir).relativeFilePath(f), QCryptographicHash::hash(readAll(f), QCryptographicHash::Sha1));
    }
    return out;
}

static QStringList differences(const QMap<QString, QByteArray> &a, const QMap<QString, QByteArray> &b)
{
    QStringList out;
    QSet<QString> keys;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        keys.insert(it.key());
    for (auto it = b.cbegin(); it != b.cend(); ++it)
        keys.insert(it.key());
    for (const QString &k : keys)
        if (a.value(k) != b.value(k) && !k.endsWith(QLatin1String("quillbinder.lock")))
            out << k;
    out.sort();
    return out;
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

static EditorPane *firstPane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

static TextEditor *showText(MainWindow &w, BinderItem *it)
{
    EditorPane *pane = firstPane(w);
    if (!pane)
        return nullptr;
    pane->showItems({it}, false, int(ViewMode::Single));
    settle(300);
    TextEditor *e = pane->currentEditor();
    if (e) {
        w.activateWindow();
        e->setFocus();
    }
    return e;
}

static QAction *actionNamed(MainWindow &w, const QString &name)
{
    for (QAction *a : w.findChildren<QAction *>())
        if (a->objectName() == name)
            return a;
    return nullptr;
}

static int picturesIn(const QTextDocument *d)
{
    int n = 0;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat())
                n += int(it.fragment().text().count(QChar::ObjectReplacementCharacter));
    return n;
}

// a color as Scrivener writes it ("r g b", 0 to 1)
static QColor scrivColor(const QString &s)
{
    const QStringList p = s.simplified().split(QLatin1Char(' '));
    if (p.size() < 3)
        return QColor();
    return QColor::fromRgbF(p[0].toFloat(), p[1].toFloat(), p[2].toFloat());
}

static QString uuidOf(const QString &scriv, const QString &title)
{
    const QByteArray x = readAll(scrivener::scrivxIn(scriv));
    const int t = x.indexOf("<Title>" + title.toUtf8() + "</Title>");
    const int u = x.lastIndexOf("UUID=\"", t);
    return t > 0 && u > 0 ? QString::fromUtf8(x.mid(u + 6, 36)) : QString();
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0642Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0642Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.64.2")) >= 0,
          QStringLiteral("version 0.64.2 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.smartQuotes = true;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    Formatting plainF;

    // ================================================================ word counts
    {
        const QString combining = QStringLiteral("Café au lait");
        const QString digits = QStringLiteral("3,500 people paid 1.000.000 coins");
        const QString wide = QStringLiteral("\U0001D400\U0001D401\U0001D402 word");
        check(text::count(combining).words == 3, QStringLiteral("a combining accent stays in its word (\"Café au lait\": 3 words)"),
              QString::number(text::count(combining).words));
        check(text::count(digits).words == 5, QStringLiteral("3,500 and 1.000.000 are one word each (5 words)"), QString::number(text::count(digits).words));
        check(text::count(wide).words == 2, QStringLiteral("letters outside the first Unicode plane make a word (2 words)"),
              QString::number(text::count(wide).words));
    }

    // ================================================================ a picture's format on letters
    {
        QTextDocument d;
        display::prepareDocument(&d, plainF, 1.0);
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(Qt::red);
        d.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("images/pic.png")), img);
        QTextCursor c(&d);
        c.insertText(QStringLiteral("Before "));
        QTextImageFormat imf;
        imf.setName(QStringLiteral("images/pic.png"));
        c.insertImage(imf);
        c.insertText(QStringLiteral("after"), imf); // what a paste left: letters with the picture's format
        const QList<QPair<QString, QTextCharFormat>> pieces = text::pictureSafePieces(QStringLiteral("￼ab￼"), imf);
        check(pieces.size() == 3 && pieces.at(0).second.isImageFormat() && !pieces.at(1).second.isImageFormat()
                  && pieces.at(1).first == QLatin1String("ab") && pieces.at(2).second.isImageFormat(),
              QStringLiteral("a fragment with the picture's format splits into pictures (one per U+FFFC) and text"));
        const QString md = md::write(&d, plainF, [](const QTextImageFormat &f) { return f.name(); });
        check(md.count(QLatin1String("![")) == 1 && md.contains(QLatin1String("after")),
              QStringLiteral("Markdown: letters with a picture's format are written as text (before: a picture per letter)"), md.left(300));
        const QByteArray rtfBytes = rtf::write(&d, plainF);
        check(rtfBytes.count("\\pict") == 1 && rtfBytes.contains("after"), QStringLiteral("RTF: the same"),
              QString::number(rtfBytes.count("\\pict")));
    }

    // ================================================================ RTF: italic from a face name
    {
        QTextDocument d;
        rtf::ReadOptions o;
        o.keepFormatting = false;
        rtf::read(QByteArray("{\\rtf1\\ansi{\\fonttbl{\\f0 SitkaTextItalic;}{\\f1 Georgia-BoldItalic;}}\\f0 Text in Sitka Text Italic. \\f1 Bold too.\\par}"),
                  &d, plainF, o);
        QTextCursor c(&d);
        c.setPosition(3);
        QTextCursor c2(&d);
        c2.setPosition(d.toPlainText().indexOf(QLatin1String("Bold")) + 2);
        check(c.charFormat().fontItalic() && c.charFormat().fontFamilies().toStringList().isEmpty()
                  && c2.charFormat().fontWeight() >= QFont::Bold && c2.charFormat().fontItalic(),
              QStringLiteral("RTF without kept fonts (a Markdown project): a face name that says italic or bold gives italics and bold, "
                             "not the font"),
              c.charFormat().fontFamilies().toStringList().join(QLatin1Char(',')));
    }

    // ================================================================ German quotes
    {
        const QString de = QStringLiteral("de");
        check(autocorrect::smartQuote(QLatin1Char('\''), QStringLiteral("Er sagte ‚einzeln"), de) == QChar(0x2018),
              QStringLiteral("German: ' after a word closes an open ‚ with ‘"));
        check(autocorrect::smartQuote(QLatin1Char('\''), QStringLiteral("Er sagte, das geht"), de) == QChar(0x2019),
              QStringLiteral("German: ' after a word with no open mark is the apostrophe ’"));
        check(autocorrect::smartQuote(QLatin1Char('\''), QStringLiteral("He said ‘single"), QStringLiteral("en")) == QChar(0x2019),
              QStringLiteral("English: ' after a word stays ’ (the closing mark and the apostrophe are one character)"));
        check(autocorrect::apostropheBeforeLetter(QStringLiteral("‚Wie geht‘"), QLatin1Char('s'), de) == 9
                  && autocorrect::apostropheBeforeLetter(QStringLiteral("‚Wie geht‘"), QLatin1Char(' '), de) == -1,
              QStringLiteral("German: a closing ‘ followed by a letter was an apostrophe (geht's)"));
    }

    // ================================================================ Markdown storage
    {
        QTextDocument d;
        display::prepareDocument(&d, plainF, 1.0);
        QTextCursor c(&d);
        c.insertText(QStringLiteral(" <$n> starts with a space"));
        c.insertBlock();
        c.insertText(QStringLiteral("\u00A0<$n>"));
        c.insertBlock();
        c.insertText(QStringLiteral("\tstarts with a tab"));
        c.insertBlock();
        c.insertBlock();
        c.insertText(QStringLiteral("last"));
        // the empty paragraph (third) aligned right by hand, through the editor's own way
        {
            QTextCursor e(d.findBlockByNumber(3));
            direct::ParaSpec ps;
            ps.bits = direct::Align;
            ps.align = Qt::AlignRight;
            e.setBlockFormat(direct::withParaSpec(e.blockFormat(), ps, plainF, true));
        }
        // a paragraph of the style Centered (S-W04)
        c.insertBlock(display::blockFormatFor(plainF, QStringLiteral("Centered")), display::charFormatFor(plainF, QStringLiteral("Centered")));
        c.insertText(QStringLiteral("Part One"));
        // a centered paragraph that starts with a no-break space (S-E04)
        c.insertBlock();
        c.insertText(QStringLiteral("\u00A0<$n>\u00A0"));
        {
            direct::ParaSpec ps;
            ps.bits = direct::Align;
            ps.align = Qt::AlignHCenter;
            c.setBlockFormat(direct::withParaSpec(c.blockFormat(), ps, plainF, false));
        }
        const QString written = md::write(&d, plainF);
        QTextDocument back;
        md::read(written, &back, plainF);
        {
            QString style;
            for (QTextBlock b = back.begin(); b.isValid(); b = b.next())
                if (b.text() == QLatin1String("Part One"))
                    style = b.blockFormat().stringProperty(props::Style);
            check(style == QLatin1String("Centered") && md::toStandardMarkdown(written).contains(QLatin1String("->Part One<-")),
                  QStringLiteral("Markdown: a paragraph of the style Centered keeps its style (it came back centered by hand); exported "
                                 "Markdown still centers it"),
                  style);
        }
        check(back.lastBlock().text() == QStringLiteral("\u00A0<$n>\u00A0") && (back.lastBlock().blockFormat().alignment() & Qt::AlignHCenter),
              QStringLiteral("Markdown: a centered paragraph keeps no-break spaces at its ends"), back.lastBlock().text());
        check(back.findBlockByNumber(0).text() == QLatin1String(" <$n> starts with a space"),
              QStringLiteral("Markdown: a paragraph that starts with a space keeps it"), back.findBlockByNumber(0).text());
        check(back.findBlockByNumber(1).text() == QStringLiteral("\u00A0<$n>"),
              QStringLiteral("Markdown: a paragraph that starts with a no-break space keeps it"), back.findBlockByNumber(1).text() + QStringLiteral(" | ") + written.left(200));
        check(back.findBlockByNumber(2).text() == QLatin1String("\tstarts with a tab"),
              QStringLiteral("Markdown: a paragraph that starts with a tab keeps it"), back.findBlockByNumber(2).text());
        check(back.blockCount() == d.blockCount() && (back.findBlockByNumber(3).blockFormat().alignment() & Qt::AlignRight),
              QStringLiteral("Markdown: an empty paragraph aligned by hand keeps its alignment"), written.left(300));
    }

    {
        // a bold run that ends with a space of a format of its own (S-W14's marathon)
        QTextDocument d;
        display::prepareDocument(&d, plainF, 1.0);
        QTextCursor c(&d);
        c.insertText(QStringLiteral("be"));
        QTextCharFormat b;
        b.setFontWeight(QFont::Bold);
        c.insertText(QStringLiteral("ca"), b);
        QTextCharFormat bs = b;
        bs.setProperty(QTextFormat::UserProperty + 77, 1); // a fragment of its own, nothing Markdown writes
        c.insertText(QStringLiteral(" "), bs);
        c.insertText(QStringLiteral("smelled"), QTextCharFormat());
        const QString written = md::write(&d, plainF);
        QTextDocument back;
        md::read(written, &back, plainF);
        QTextCursor at(&back);
        at.setPosition(3);
        check(back.toPlainText() == QLatin1String("beca smelled") && at.charFormat().fontWeight() >= QFont::Bold,
              QStringLiteral("Markdown: a bold run ending with a space of its own format comes back bold, without asterisks in the text"),
              written.trimmed() + QStringLiteral(" -> ") + back.toPlainText());
    }

    // ================================================================ projects: spare copy, folder gone
    {
        QTemporaryDir td;
        auto p = templates::createProject(blank, td.path(), QStringLiteral("Spare"), &err);
        if (p) {
            textDoc(p.get(), p->draft(), QStringLiteral("One"), QStringLiteral("Text of one."));
            p->save(&err);
            const QString spare = p->folderPath() + QStringLiteral("/settings/spare-copy") + QLatin1String(QB_PROJECT_FILE_SUFFIX);
            const QString resolved = Project::resolveProjectFile(spare);
            check(QFileInfo(resolved).absoluteFilePath() == QFileInfo(p->indexPath()).absoluteFilePath(),
                  QStringLiteral("the spare copy of the project file names the project itself (a backup made then had only the settings)"),
                  resolved);
            const QString folder = p->folderPath();
            QDir().rename(folder, folder + QStringLiteral("-moved"));
            QTextCursor c(p->docs()->text(p->draft()->children.first()->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Typed while it is gone."));
            const bool saved = p->save(&err);
            check(!saved && !QFileInfo::exists(folder) && !err.isEmpty(),
                  QStringLiteral("a project folder that is gone is not made anew by a save; the save fails with a reason"), err);
            QDir().rename(folder + QStringLiteral("-moved"), folder);
            check(p->save(&err) && QString::fromUtf8(readAll(p->docs()->textPath(p->draft()->children.first()->id))).contains(QLatin1String("while it is gone")),
                  QStringLiteral("... put back, the next save writes what was typed meanwhile"), err);
        } else {
            check(false, QStringLiteral("(a project)"), err);
        }
    }

    // ================================================================ Scrivener format
    {
        QTemporaryDir sw;
        auto sp = templates::createProject(blank, sw.path(), QStringLiteral("Format"), &err);
        BinderItem *alpha = textDoc(sp.get(), sp->draft(), QStringLiteral("Alpha"), QStringLiteral("Alpha text."));
        alpha->synopsis = QStringLiteral(" starts with a space");
        const QString alphaId = alpha->id;
        textDoc(sp.get(), sp->draft(), QStringLiteral("Beta"), QStringLiteral("Beta text."));
        sp->save(&err);
        const QString scriv = sw.path() + QStringLiteral("/Format.scriv");
        auto wc = scrivener::convertToScrivener(sp.get(), scriv, &err);
        check(wc != nullptr, QStringLiteral("(saved in Scrivener format)"), err);
        wc.reset();
        sp.reset();
        const QString scrivx = scrivener::scrivxIn(scriv);
        // as Scrivener writes them: no IncludeInCompile and no MetaData on the top folders, its own color digits,
        // another Device
        {
            QString x = QString::fromUtf8(readAll(scrivx));
            static const QRegularExpression color(QStringLiteral("(<Label ID=\"(\\d+)\" Color=\")([^\"]*)\""));
            int n = 0;
            x.replace(QRegularExpression(QStringLiteral(" Device=\"[^\"]*\"")), QString());
            x.replace(QStringLiteral("<ScrivenerProject "), QStringLiteral("<ScrivenerProject Device=\"Scrivener's Mac\" "));
            QRegularExpressionMatchIterator it = color.globalMatch(x);
            QList<QRegularExpressionMatch> ms;
            while (it.hasNext())
                ms << it.next();
            for (int i = ms.size() - 1; i >= 0; --i) {
                const QColor c = scrivColor(ms[i].captured(3));
                if (!c.isValid())
                    continue;
                // the same 8-bit color, written with other digits
                const double r = c.redF() > 0.5 ? c.redF() - 0.0011 : c.redF() + 0.0011;
                const QString other = QStringLiteral("%1 %2 %3").arg(r, 0, 'f', 6).arg(c.greenF(), 0, 'f', 6).arg(c.blueF(), 0, 'f', 6);
                if (QColor::fromRgbF(float(r), c.greenF(), c.blueF()).rgb() == c.rgb()) {
                    x.replace(ms[i].capturedStart(3), ms[i].capturedLength(3), other);
                    ++n;
                }
            }
            for (const QString &top : {QStringLiteral("Research"), QStringLiteral("Trash")}) {
                const int t = x.indexOf(QStringLiteral("<Title>%1</Title>").arg(top));
                if (t < 0)
                    continue;
                const int ms0 = x.indexOf(QLatin1String("<MetaData>"), t), me = x.indexOf(QLatin1String("</MetaData>"), t);
                const int next = x.indexOf(QLatin1String("<BinderItem"), t);
                if (ms0 > 0 && me > ms0 && (next < 0 || me < next))
                    x.remove(ms0, me + int(qstrlen("</MetaData>")) - ms0);
            }
            // laid out in another way (Scrivener's own layout differs from this program's): attributes in another
            // order, tabs for the indentation, double quotes in the XML declaration
            static const QRegularExpression item(QStringLiteral("<BinderItem ([^>]*?)UUID=\"([^\"]*)\"([^>]*)>"));
            x.replace(item, QStringLiteral("<BinderItem UUID=\"\\2\" \\1\\3>"));
            x.replace(QRegularExpression(QStringLiteral("\n( +)<")), QStringLiteral("\n\t<"));
            x.replace(QStringLiteral("<?xml version='1.0' encoding='UTF-8'?>"), QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
            writeAll(scrivx, x.toUtf8());
            std::printf("      (label colors written with other digits: %d)\n", n);
        }
        // a cut-off save of Scrivener's beside a text, the synopsis with its leading space
        const QString alphaUuid = uuidOf(scriv, QStringLiteral("Alpha"));
        writeAll(scriv + QStringLiteral("/Files/Data/") + alphaUuid + QStringLiteral("/content.rtf.Ab12Cd"), "{\\rtf1 half}");
        // the .scriv as "Scrivener" left it: opened here the first time (read afresh), saved and closed, it stays
        const QMap<QString, QByteArray> before = filesIn(scriv);
        QTest::qWait(1100);
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        settle(300);
        closeBoxesLater(1500);
        check(w.openScrivenerProject(scriv), QStringLiteral("(the .scriv opens)"));
        settle(600);
        Project *wp = w.project();
        BinderItem *a = wp ? wp->item(alphaId) : nullptr;
        if (!a && wp)
            for (BinderItem *x : wp->allItems())
                if (x->title == QLatin1String("Alpha"))
                    a = x;
        check(a && a->synopsis == QLatin1String(" starts with a space"), QStringLiteral("Scrivener format: a synopsis keeps its leading space"),
              a ? a->synopsis : QString());
        int alphas = 0;
        if (wp)
            for (BinderItem *x : wp->allItems())
                if (x->title.startsWith(QLatin1String("Alpha")) || x->title.contains(QLatin1String("Ab12Cd")))
                    ++alphas;
        check(alphas == 1, QStringLiteral("Scrivener format: the file of a cut-off save (content.rtf.Ab12Cd) is no document or media file"),
              QString::number(alphas));
        if (w.project())
            w.project()->save(&err);   // a full save, as File > Save makes it
        w.closeProject();
        settle(300);
        const QStringList changed = differences(before, filesIn(scriv));
        check(changed.isEmpty(),
              QStringLiteral("Scrivener format: opened and closed without a change, a .scriv as Scrivener writes it stays as it was "
                             "(its own layout of the .scrivx, label colors, top folders without IncludeInCompile and MetaData, "
                             "another Device, no search index made)"),
              changed.join(QLatin1String(", ")));
        // pictures in notes
        closeBoxesLater(1500);
        w.openScrivenerProject(scriv);
        settle(600);
        wp = w.project();
        a = nullptr;
        if (wp)
            for (BinderItem *x : wp->allItems())
                if (x->title == QLatin1String("Alpha"))
                    a = x;
        if (wp && a) {
            QImage img(40, 40, QImage::Format_RGB32);
            img.fill(Qt::blue);
            QTextDocument *nd = wp->docs()->notes(a->id);
            const QString rel = wp->docs()->importImage(a->id, img);
            nd->addResource(QTextDocument::ImageResource, QUrl(rel), img);
            QTextCursor c(nd);
            QTextImageFormat f;
            f.setName(rel);
            c.insertImage(f);
            c.insertText(QStringLiteral(" A note with a picture."));
            wp->save(&err);
            w.closeProject();
            settle(300);
            // the .scriv read afresh: a working copy of its own
            QTemporaryDir fresh;
            ScrivenerImportReport rep;
            auto np = ScrivenerImporter::importProject(scriv, fresh.path(), QStringLiteral("Fresh"), StorageFormat::Markdown, &rep, &err);
            BinderItem *fa = nullptr;
            if (np)
                for (BinderItem *x : np->allItems())
                    if (x->title == QLatin1String("Alpha"))
                        fa = x;
            int inNotes = -1;
            if (np && fa) {
                np->save(&err);
                const QString folder = np->folderPath();
                const QString faId = fa->id;
                np.reset();
                auto again = Project::open(folder, &err);
                if (again)
                    inNotes = picturesIn(again->docs()->notes(faId));
            }
            check(inNotes == 1, QStringLiteral("Scrivener format: a picture in the notes is kept when the .scriv is read (it was lost when saved)"),
                  QString::number(inNotes));
        } else {
            check(false, QStringLiteral("(the .scriv opened again)"));
        }
        // a text changed in the .scriv while the project is open, nothing changed here: read at the next opening
        closeBoxesLater(1500);
        w.openScrivenerProject(scriv);
        settle(600);
        wp = w.project();
        const QString betaUuid = uuidOf(scriv, QStringLiteral("Beta"));
        if (wp && !betaUuid.isEmpty()) {
            QTest::qWait(1100);
            writeAll(scriv + QStringLiteral("/Files/Data/") + betaUuid + QStringLiteral("/content.rtf"),
                     "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}\\f0 Beta as Scrivener wrote it while open.\\par}");
            BinderItem *alphaHere = nullptr;
            for (BinderItem *x : wp->allItems())
                if (x->title == QLatin1String("Alpha"))
                    alphaHere = x;
            if (alphaHere) {
                QTextCursor c(wp->docs()->text(alphaHere->id));
                c.movePosition(QTextCursor::End);
                c.insertText(QStringLiteral(" More alpha."));
            }
            wp->save(&err);
            w.closeProject();
            settle(300);
            closeBoxesLater(1500);
            w.openScrivenerProject(scriv);
            settle(600);
            QString shown;
            if (w.project())
                for (BinderItem *x : w.project()->allItems())
                    if (x->title == QLatin1String("Beta"))
                        shown = w.project()->docs()->text(x->id)->toPlainText();
            check(shown.contains(QLatin1String("while open")),
                  QStringLiteral("Scrivener format: a text changed in the .scriv while the project was open (and not here) is shown at the "
                                 "next opening (before: the old text, until the .scriv changed again)"),
                  shown.left(200));
            w.closeProject();
            settle(300);
        }
        // Scrivener's lock in XML form
        writeAll(scriv + QStringLiteral("/Files/user.lock"), "<?xml version=\"1.0\"?><Lock><UserName>anna</UserName><ComputerName>Annas-Mac</ComputerName></Lock>");
        QString who;
        check(scrivener::lockedByScrivener(scriv, &who) && who.contains(QLatin1String("anna")) && who.contains(QLatin1String("Annas-Mac")),
              QStringLiteral("Scrivener's lock: the user and computer are read from an XML lock as well"), who);
        QFile::remove(scriv + QStringLiteral("/Files/user.lock"));
        // a .scriv that is gone is not made anew
        closeBoxesLater(1500);
        w.openScrivenerProject(scriv);
        settle(600);
        wp = w.project();
        if (wp) {
            QDir().rename(scriv, scriv + QStringLiteral("-moved"));
            for (BinderItem *x : wp->allItems())
                if (x->title == QLatin1String("Alpha")) {
                    QTextCursor c(wp->docs()->text(x->id));
                    c.movePosition(QTextCursor::End);
                    c.insertText(QStringLiteral(" Typed while it is gone."));
                }
            QString e2;
            const bool ok = wp->save(&e2);
            check(!ok && !QFileInfo::exists(scriv),
                  QStringLiteral("Scrivener format: a .scriv that is gone is not made anew by a save (it held only the changed texts)"), e2);
            QDir().rename(scriv + QStringLiteral("-moved"), scriv);
            check(wp->save(&e2) && QString::fromUtf8(readAll(scriv + QStringLiteral("/Files/Data/") + alphaUuid + QStringLiteral("/content.rtf"))).contains(QLatin1String("while it is gone")),
                  QStringLiteral("... put back, the next save writes what was typed meanwhile"), e2);
            // a write into the .scriv that fails in the autosave: File > Save tries again and says so (S-D06)
            const QString dataDir = scriv + QStringLiteral("/Files/Data/") + alphaUuid;
            for (BinderItem *x : wp->allItems())
                if (x->title == QLatin1String("Alpha")) {
                    QTextCursor c(wp->docs()->text(x->id));
                    c.movePosition(QTextCursor::End);
                    c.insertText(QStringLiteral(" Typed on a full disk."));
                }
            QDir().rename(dataDir, dataDir + QStringLiteral("-aside"));
            writeAll(dataDir, "not a folder");
            QString e3, e4;
            const bool quickOk = wp->autosave(&e3);
            const bool fullOk = wp->save(&e4);
            std::printf("      (autosave %d: %s; save %d: %s)\n", quickOk, qPrintable(e3.left(120)), fullOk, qPrintable(e4.left(120)));
            check(!quickOk && !fullOk && !e4.isEmpty(),
                  QStringLiteral("Scrivener format: a write into the .scriv that failed in the autosave fails File > Save again, with its reason"),
                  e4);
            QFile::remove(dataDir);
            QDir().rename(dataDir + QStringLiteral("-aside"), dataDir);
            check(wp->save(&e2) && QString::fromUtf8(readAll(dataDir + QStringLiteral("/content.rtf"))).contains(QLatin1String("on a full disk")),
                  QStringLiteral("... and once it can be written, the next save writes it"), e2);
            w.closeProject();
            settle(300);
        }
    }

    // ================================================================ in the window
    {
        QTemporaryDir td;
        auto p = templates::createProject(blank, td.path(), QStringLiteral("Window"), &err);
        BinderItem *one = textDoc(p.get(), p->draft(), QStringLiteral("One"), QStringLiteral("Plain bold italic plain."));
        BinderItem *two = textDoc(p.get(), p->draft(), QStringLiteral("Two"), QStringLiteral("Second text."));
        p->labelTitle = QStringLiteral("Point of View");
        p->statusTitle = QStringLiteral("Draft State");
        p->save(&err);
        const QString path = p->indexPath();
        const QString oneId = one->id, twoId = two->id;
        p.reset();
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        settle(300);
        check(w.openProject(path), QStringLiteral("(the project opens)"));
        settle(500);
        Project *wp = w.project();
        // a linked note over text of several formats
        TextEditor *e = wp ? showText(w, wp->item(oneId)) : nullptr;
        if (e) {
            QTextCursor c(e->document());
            c.setPosition(6);
            c.setPosition(10, QTextCursor::KeepAnchor);
            QTextCharFormat b;
            b.setFontWeight(QFont::Bold);
            c.mergeCharFormat(b);
            c.setPosition(11);
            c.setPosition(17, QTextCursor::KeepAnchor);
            QTextCharFormat i;
            i.setFontItalic(true);
            c.mergeCharFormat(i);
            QTextCursor all(e->document());
            all.select(QTextCursor::Document);
            e->setTextCursor(all);
            const QString id = e->addLinkedNote(NoteKind::Comment, QStringLiteral("A comment over three formats."));
            int covered = 0;
            for (QTextBlock bl = e->document()->begin(); bl.isValid(); bl = bl.next())
                for (auto it = bl.begin(); !it.atEnd(); ++it)
                    if (it.fragment().charFormat().stringProperty(props::NoteId) == id)
                        covered += it.fragment().length();
            QTextCursor at(e->document());
            at.setPosition(8);
            check(!id.isEmpty() && covered >= 23 && at.charFormat().fontWeight() >= QFont::Bold,
                  QStringLiteral("a comment over text of several formats covers all of it and keeps the formats (it crashed)"),
                  QString::number(covered));
            // subscript at the end of a word: what is typed next
            QTextCursor endc(e->document());
            endc.movePosition(QTextCursor::End);
            e->setTextCursor(endc);
            QTest::keyClicks(e, QStringLiteral(" H"));
            e->toggleSubscript();
            QTest::keyClicks(e, QStringLiteral("2"));
            e->toggleSubscript();
            QTest::keyClicks(e, QStringLiteral("O"));
            const QString t = e->document()->toPlainText();
            QTextCursor h(e->document()), two2(e->document()), o(e->document());
            h.setPosition(t.lastIndexOf(QLatin1Char('H')) + 1);
            two2.setPosition(t.lastIndexOf(QLatin1Char('2')) + 1);
            o.setPosition(t.lastIndexOf(QLatin1Char('O')) + 1);
            check(h.charFormat().verticalAlignment() == QTextCharFormat::AlignNormal
                      && two2.charFormat().verticalAlignment() == QTextCharFormat::AlignSubScript
                      && o.charFormat().verticalAlignment() == QTextCharFormat::AlignNormal,
                  QStringLiteral("subscript at the end of a word acts on what is typed next, not on the word (H2O)"));
            // Backspace at the start of a list item: the paragraph leaves the list with its style's spacing again
            {
                const Formatting f = wp->formatting;
                const QTextBlockFormat normal = display::normalBlockFormat(f);
                QTextCursor lc = e->textCursor();
                lc.movePosition(QTextCursor::End);
                lc.insertBlock(normal);
                lc.insertText(QStringLiteral("A list item"));
                e->setTextCursor(lc);
                e->toggleList(false);
                QTextCursor st = e->textCursor();
                st.movePosition(QTextCursor::StartOfBlock);
                e->setTextCursor(st);
                QTest::keyClick(e, Qt::Key_Backspace);
                const QTextBlock lb = e->textCursor().block();
                check(!lb.textList() && qAbs(lb.blockFormat().bottomMargin() - normal.bottomMargin()) < 0.01
                          && qAbs(lb.blockFormat().textIndent() - normal.textIndent()) < 0.01 && lb.text() == QLatin1String("A list item"),
                      QStringLiteral("Backspace at the start of a list item: the paragraph leaves the list with its style's spacing and indent "
                                     "(it kept the list's until the project was opened again)"),
                      QStringLiteral("after %1, indent %2").arg(lb.blockFormat().bottomMargin()).arg(lb.blockFormat().textIndent()));
                QTextCursor endc2 = e->textCursor();
                endc2.movePosition(QTextCursor::End);
                e->setTextCursor(endc2);
            }
            // Enter at the end of a list item: the item left behind keeps its spacing (Qt's own Enter set it to 0)
            {
                const QTextBlockFormat normal = display::normalBlockFormat(wp->formatting);
                QTextCursor pc = e->textCursor();
                pc.movePosition(QTextCursor::End);
                pc.insertBlock(normal);
                pc.insertText(QStringLiteral("An item of a numbered list"));
                e->setTextCursor(pc);
                e->toggleList(true);
                QTextCursor eol = e->textCursor();
                eol.movePosition(QTextCursor::EndOfBlock);
                e->setTextCursor(eol);
                const int item = eol.blockNumber();
                QTest::keyClick(e, Qt::Key_Return);
                const QTextBlock left = e->document()->findBlockByNumber(item);
                const QTextBlock next = e->document()->findBlockByNumber(item + 1);
                check(qAbs(left.blockFormat().bottomMargin() - normal.bottomMargin()) < 0.01 && next.textList() && next.textList() == left.textList(),
                      QStringLiteral("Enter at the end of a list item: a new item of the same list, and the item left behind keeps the "
                                     "spacing after it (it became 0 until the project was opened again)"),
                      QStringLiteral("after %1").arg(left.blockFormat().bottomMargin()));
                QTest::keyClick(e, Qt::Key_Return);   // an empty item: the list ends
            }
            // German quotes while typing
            as.quoteStyle = QStringLiteral("de");
            QTest::keyClicks(e, QStringLiteral(" 'Wie geht's?' "));
            const QString typed = e->document()->toPlainText();
            check(typed.contains(QStringLiteral("‚Wie geht’s?‘")),
                  QStringLiteral("German quotes while typing: ‚Wie geht’s?‘"), typed.right(30));
            as.quoteStyle = QStringLiteral("en");
            // text typed after a picture is text
            QImage img(20, 20, QImage::Format_RGB32);
            img.fill(Qt::green);
            const QString rel = wp->docs()->importImage(oneId, img);
            QTextCursor pc = e->textCursor();
            pc.movePosition(QTextCursor::End);
            QTextImageFormat imf;
            imf.setName(rel);
            pc.insertImage(imf);
            e->setTextCursor(pc);
            settle(50);
            QTest::keyClicks(e, QStringLiteral("abc"));
            QTextCursor ac(e->document());
            ac.setPosition(e->document()->characterCount() - 2);
            check(!ac.charFormat().isImageFormat() && picturesIn(e->document()) == 1,
                  QStringLiteral("letters typed right after a picture are text, not pictures"));
            // a picture cut and pasted keeps its picture (RTF on the clipboard names it rtfimage://)
            QTextCursor sel(e->document());
            sel.setPosition(e->document()->characterCount() - 5);
            sel.setPosition(e->document()->characterCount() - 4, QTextCursor::KeepAnchor);
            e->setTextCursor(sel);
            e->cut();
            QTextCursor start(e->document());
            e->setTextCursor(start);
            e->paste();
            settle(50);
            wp->save(&err);
            const QString md = QString::fromUtf8(readAll(wp->docs()->textPath(oneId)));
            static const QRegularExpression pic(QStringLiteral("!\\[[^\\]]*\\]\\((images/[^)]+)\\)"));
            int good = 0;
            QStringList names;
            for (auto it = pic.globalMatch(md); it.hasNext();) {
                const QString n = it.next().captured(1);
                names << n;
                if (QFileInfo::exists(QFileInfo(wp->docs()->textPath(oneId)).absolutePath() + QLatin1Char('/') + n))
                    ++good;
            }
            check(good == 1 && names.size() == 1, QStringLiteral("a picture cut and pasted is saved with its picture (it was lost)"),
                  md.left(400));
            shot(&w, QStringLiteral("editor"));
        }
        // another computer takes the project over while the writer types here: the notice does not take the keys
        if (wp && e) {
            const QString lockFile = wp->folderPath() + QStringLiteral("/settings/lock");
            const QDateTime alive = QDateTime::currentDateTimeUtc();
            writeAll(lockFile, QStringLiteral("laptop 4242\ndevice=laptop\nuser=\ninstance=x\nkey=0123456789abcdef\nprogram=Quillbinder 0.64.2\nopened=%1\nalive=%1\n")
                                   .arg(alive.toString(Qt::ISODate)).toUtf8());
            w.activateWindow();
            e->setFocus();
            settle(100);
            wp->save(&err);   // every save looks at the lock
            settle(400);
            QMessageBox *taken = nullptr;
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible() && b->objectName() == QLatin1String("lockTakenNotice"))
                    taken = b;
            QAbstractButton *def = taken ? taken->defaultButton() : nullptr;
            check(taken && !taken->isModal() && QApplication::activeWindow() == &w && def && def->text().contains(QLatin1String("Keep")),
                  QStringLiteral("the notice that another computer opened the project does not take the keys of the writer who is "
                                 "typing (not modal, the window stays active; Keep Working is the default): a Space meant for the text "
                                 "closed the project before"),
                  taken ? QStringLiteral("modal %1, active window %2").arg(taken->isModal()).arg(QApplication::activeWindow() ? QApplication::activeWindow()->objectName() : QStringLiteral("none")) : QStringLiteral("(no notice)"));
            if (taken)
                taken->reject();
            QFile::remove(lockFile);
        }
        // the Go To menu: an entry of a document deleted since does nothing
        QPointer<QAction> stale;
        for (QMenu *m : w.menuBar()->findChildren<QMenu *>()) {
            emit m->aboutToShow();
            for (QAction *a : m->actions())
                if (a->text().remove(QLatin1Char('&')) == QLatin1String("Two") && !a->menu())
                    stale = a;
        }
        if (wp && wp->item(twoId)) {
            QList<BinderItem *> del = {wp->item(twoId)};
            wp->model()->moveToTrash(del);
            wp->model()->emptyTrash();
            settle(200);
        }
        if (stale)
            stale->trigger();
        settle(200);
        check(w.project() != nullptr, QStringLiteral("a menu entry of a document deleted since (Go To, bookmarks ...) does nothing (it crashed)"),
              stale ? QString() : QStringLiteral("(no entry found)"));
        // the Documents menu names the lists as the project does
        QStringList titles;
        for (QMenu *m : w.menuBar()->findChildren<QMenu *>())
            if (m->title().remove(QLatin1Char('&')) == QLatin1String("Documents")) {
                emit m->aboutToShow();
                for (QAction *a : m->actions())
                    if (a->menu())
                        titles << a->text().remove(QLatin1Char('&'));
            }
        check(titles.contains(QLatin1String("Point of View")) && titles.contains(QLatin1String("Draft State")),
              QStringLiteral("the Documents menu names the label and status lists as the project calls them"), titles.join(QLatin1String(", ")));
        // a second start with this project: it is shown, no second window
        check(w.showIfOpen(path) && !w.showIfOpen(td.path() + QStringLiteral("/Other.quillbinder/Other.qbx")),
              QStringLiteral("a second start with a project that is open shows its window (MainWindow::showIfOpen); another project does not"));
        w.closeProject();
        settle(300);
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
