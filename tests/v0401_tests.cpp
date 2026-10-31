// Scripted run for version 0.40.1: fonts that keep several designs in one family (the
// optical sizes of Windows 11's Sitka, here the free Source Serif 4 of tests/fonts, which is
// built the same way) get a family per design, so that Bold and Italic stay within the
// design (text/FontFaces.*); and the Bold, Italic, Underline and Strikethrough buttons show
// the state of the whole selection, also of a selection made from right to left, in the
// editor and in the text fields of objects. Saves screenshots into the folder given as first
// argument. Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v0401_tests /tmp/shots
//   QB_KEEP_FONT_FAMILIES=1 ...  runs without the separation (the font checks then fail)
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "compile/PluginManager.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/FontFaces.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/FormatDialogs.h"
#include "ui/MainWindow.h"
#include "ui/ObjectsUi.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QListWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
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

static const QString kFamily = QStringLiteral("Source Serif 4 Variable");
static const QString kSample = QStringLiteral("The keeper climbed the stairs at dusk and lit the lamp.");

// the face Qt draws for a family with bold / italic, as the font file names it
static QString faceOf(const QString &family, bool bold, bool italic, double *width = nullptr)
{
    QFont f(family);
    f.setPointSizeF(12);
    f.setBold(bold);
    f.setItalic(italic);
    if (width)
        *width = QFontMetricsF(f).horizontalAdvance(kSample);
    return QFontInfo(f).styleName();
}

// the face Qt draws for a character format
static QString faceOf(const QTextCharFormat &cf)
{
    QFont f = cf.font();
    f.setPointSizeF(12);
    return QFontInfo(f).family() + QLatin1String(" / ") + QFontInfo(f).styleName();
}

static QTextCharFormat formatAt(QTextDocument *doc, const QString &word, int into = 1)
{
    const int at = int(doc->toPlainText().indexOf(word));
    QTextCursor c(doc);
    c.setPosition(qMax(0, at + into));
    return c.charFormat();
}

// select a word in an editor; backwards = the cursor ends at the start of the word
static void selectWord(QTextEdit *e, const QString &word, bool backwards, int length = -1)
{
    const int at = int(e->toPlainText().indexOf(word));
    const int end = at + (length >= 0 ? length : int(word.size()));
    QTextCursor c = e->textCursor();
    if (backwards) {
        c.setPosition(end);
        c.setPosition(at, QTextCursor::KeepAnchor);
    } else {
        c.setPosition(at);
        c.setPosition(end, QTextCursor::KeepAnchor);
    }
    e->setTextCursor(c);
    settle(120);
}

// ======================================================================= the font list

static void fontListChecks()
{
    // the words of style names
    {
        const fontfaces::StyleParts a = fontfaces::parseStyle(QStringLiteral("Banner Bold Italic"));
        const fontfaces::StyleParts b = fontfaces::parseStyle(QStringLiteral("Text Semi Bold"));
        const fontfaces::StyleParts c = fontfaces::parseStyle(QStringLiteral("Book"));
        const fontfaces::StyleParts d = fontfaces::parseStyle(QStringLiteral("Condensed ExtraBold Oblique"));
        const fontfaces::StyleParts e = fontfaces::parseStyle(QStringLiteral("SmText"));
        check(a.design == QLatin1String("Banner") && a.weight == QLatin1String("Bold") && a.italic
                  && a.styleInDesign() == QLatin1String("Bold Italic")
                  && b.design == QLatin1String("Text") && b.weight == QLatin1String("SemiBold") && !b.italic
                  && c.design.isEmpty() && c.weight.isEmpty() && c.styleInDesign() == QLatin1String("Regular")
                  && d.design == QLatin1String("Condensed") && d.weight == QLatin1String("ExtraBold") && d.italic
                  && e.design == QLatin1String("SmText") && e.styleInDesign() == QLatin1String("Regular"),
              QStringLiteral("style names are read as design, weight and slant (\"Banner Bold Italic\", \"Text Semi Bold\", "
                             "\"Book\", \"Condensed ExtraBold Oblique\", \"SmText\")"));
    }

    const QStringList before = QFontDatabase::styles(kFamily);
    const QStringList dejaVuBefore = QFontDatabase::styles(QStringLiteral("DejaVu Sans"));
    // Qt 6.4 lists 60 faces, or 56 when the font as a whole comes first and the five regular faces
    // become one (the order depends on fontconfig); the separation reads the faces from the file
    check(before.size() == 60 || before.size() == 56, QStringLiteral("(the test font is installed: one family with its faces of 5 designs)"),
          QString::number(before.size()));
    info(QStringLiteral("before: Qt lists %1 faces in the family").arg(before.size()));
    double w = 0;
    const QString boldBefore = faceOf(kFamily, true, false, &w);
    info(QStringLiteral("before: \"%1\" in bold draws the face \"%2\" (one of five Bold faces)").arg(kFamily, boldBefore));

    fontfaces::install(); // as main.cpp does
    const QHash<QString, QString> made = fontfaces::separatedFamilies();
    const QStringList designs = {QStringLiteral("Caption"), QStringLiteral("SmText"), QStringLiteral("Subhead"),
                                 QStringLiteral("Display")};
    bool all = true;
    QString detail;
    for (const QString &d : designs) {
        const QString fam = kFamily + QLatin1Char(' ') + d;
        const QStringList st = QFontDatabase::styles(fam);
        const bool ok = QFontDatabase::families().contains(fam) && made.value(fam) == kFamily && st.size() == 12
                        && st.contains(QLatin1String("Regular")) && st.contains(QLatin1String("Bold"))
                        && st.contains(QLatin1String("Italic")) && st.contains(QLatin1String("Bold Italic"));
        all = all && ok;
        detail += QStringLiteral("%1: %2 faces (%3); ").arg(fam).arg(st.size()).arg(st.join(QLatin1String(", ")));
    }
    check(all, QStringLiteral("a family per design: \"... Caption\", \"... SmText\", \"... Subhead\", \"... Display\", "
                              "each with 12 faces named Regular, Bold, Italic, Bold Italic ..."),
          detail);
    const QStringList after = QFontDatabase::styles(kFamily);
    bool own = after.size() == 12;
    for (const QString &s : after)
        own = own && fontfaces::parseStyle(s).design.isEmpty();
    check(own, QStringLiteral("the family itself keeps the 12 faces of its own design (texts saved with its name keep their look)"),
          after.join(QLatin1String(", ")));

    // Bold and Italic within the design: the face drawn is the one of the same design
    bool right = true;
    detail.clear();
    for (const QString &d : QStringList{QString()} + designs) {
        const QString fam = d.isEmpty() ? kFamily : kFamily + QLatin1Char(' ') + d;
        for (int b = 0; b < 2; ++b)
            for (int i = 0; i < 2; ++i) {
                QString want = d;
                if (b)
                    want += QLatin1String(" Bold");
                if (i)
                    want += QLatin1String(" Italic");
                want = want.trimmed();
                if (want.isEmpty())
                    want = QStringLiteral("Regular");
                const QString got = faceOf(fam, b, i);
                if (got != want) {
                    right = false;
                    detail += QStringLiteral("%1%2%3: \"%4\" instead of \"%5\"; ")
                                  .arg(fam, b ? QStringLiteral(" bold") : QString(), i ? QStringLiteral(" italic") : QString(), got, want);
                }
            }
    }
    check(right, QStringLiteral("bold, italic and bold italic of every design draw that design's own face (20 cases)"), detail);
    double wDefault = 0, wSmText = 0, wDisplay = 0, wDisplayBold = 0;
    faceOf(kFamily, false, false, &wDefault);
    faceOf(kFamily + QLatin1String(" SmText"), false, false, &wSmText);
    faceOf(kFamily + QLatin1String(" Display"), false, false, &wDisplay);
    faceOf(kFamily + QLatin1String(" Display"), true, false, &wDisplayBold);
    check(qAbs(wSmText - wDefault) > 5 && faceOf(kFamily + QLatin1String(" SmText"), false, false) == QLatin1String("SmText"),
          QStringLiteral("SmText Regular draws its own face, not the default one (Qt 6.4 files the variable font as a whole "
                         "under the first face of its weight; the face index is taken from the font file)"),
          QStringLiteral("width %1 px against %2 px").arg(wSmText).arg(wDefault));
    info(QStringLiteral("widths of the sample at 12 pt: default %1 px, SmText %2 px, Display %3 px, Display Bold %4 px")
             .arg(wDefault, 0, 'f', 1).arg(wSmText, 0, 'f', 1).arg(wDisplay, 0, 'f', 1).arg(wDisplayBold, 0, 'f', 1));
    check(fontfaces::separate() == 0, QStringLiteral("separating again changes nothing"));
    check(QFontDatabase::styles(QStringLiteral("DejaVu Sans")) == dejaVuBefore && !made.values().contains(QStringLiteral("DejaVu Sans")),
          QStringLiteral("a family with one design (DejaVu Sans: Book, Bold, Oblique ...) stays as it was"),
          QFontDatabase::styles(QStringLiteral("DejaVu Sans")).join(QLatin1String(", ")));

    // Fonts added and removed while the program runs (Format > Font > Add Fonts): Qt 6.4 adds a
    // font to its list, and reads all fonts anew when one is removed; the families are made again
    const QString extra = QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSerifCondensed.ttf");
    if (QFile::exists(extra)) {
        auto state = [] {
            return QStringLiteral("%1 faces in the family; Display Bold: %2; families made: %3")
                .arg(QFontDatabase::styles(kFamily).size())
                .arg(faceOf(kFamily + QLatin1String(" Display"), true, false))
                .arg(fontfaces::separatedFamilies().size());
        };
        auto fine = [] {
            return QFontDatabase::families().contains(kFamily + QLatin1String(" Display"))
                   && faceOf(kFamily + QLatin1String(" Display"), true, false) == QLatin1String("Display Bold")
                   && QFontDatabase::styles(kFamily).size() == 12
                   && fontfaces::separatedFamilies().value(kFamily + QLatin1String(" Display")) == kFamily;
        };
        const int id = QFontDatabase::addApplicationFont(extra);
        settle(300);
        const bool afterAdd = fine();
        const QString addState = state();
        if (id >= 0)
            QFontDatabase::removeApplicationFont(id);
        settle(300);
        check(afterAdd && fine(), QStringLiteral("a font added and removed while the program runs: the families are there "
                                                 "again after Qt read its fonts anew"),
              addState + QLatin1String(" | ") + state());
    } else {
        info(QStringLiteral("(no DejaVu Serif Condensed file: the check of a font added while running is left out)"));
    }
}

// ======================================================================= the window

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    // the test font through fontconfig, as fonts installed on the system are found
    {
        const QString conf = home.path() + QLatin1String("/fonts.conf");
        QFile f(conf);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QStringLiteral("<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n<fontconfig>\n"
                                   "  <include ignore_missing=\"yes\">/etc/fonts/fonts.conf</include>\n"
                                   "  <dir>%1/fonts/source-serif-4</dir>\n  <cachedir>%2/fc-cache</cachedir>\n</fontconfig>\n")
                        .arg(QStringLiteral(QB_TEST_SOURCE_DIR), home.path())
                        .toUtf8());
            f.close();
            qputenv("FONTCONFIG_FILE", conf.toUtf8());
        }
    }
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0401Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0401Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    // (0.40.2: the version check of the newest run is in tests/v0402_tests.cpp; here: 0.40.1 or newer)
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.40.1")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" alpha")),
          QStringLiteral("version 0.40.1 or newer, shown with \"alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));
    fontListChecks();

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
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto proj = templates::createProject(novel, work.path(), QStringLiteral("Harbor Light"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter One"));
    BinderItem *doc = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Dusk"));
    {
        QTextCursor c(p->docs()->text(doc->id));
        c.setBlockFormat(display::blockFormatFor(p->formatting, QStringLiteral("Heading 1"), QTextBlockFormat()));
        c.insertText(QStringLiteral("At the Lamp"), display::charFormatFor(p->formatting, QStringLiteral("Heading 1")));
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(kSample);
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("alpha beta gamma delta epsilon"));
    }
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(400);
    auto *binder = w.findChild<BinderPanel *>();
    auto *formatBar = w.findChild<QToolBar *>(QStringLiteral("formatBar"));
    auto *fontCombo = formatBar ? formatBar->findChild<QFontComboBox *>() : nullptr;
    QAction *bold = byKey(&w, QStringLiteral("bold"));
    QAction *italic = byKey(&w, QStringLiteral("italic"));
    QAction *underline = byKey(&w, QStringLiteral("underline"));
    QAction *strike = byKey(&w, QStringLiteral("strike"));
    check(binder && fontCombo && bold && italic && underline && strike, QStringLiteral("(window parts found)"));
    if (!binder || !fontCombo || !bold || !italic || !underline || !strike)
        return 1;
    binder->select({doc}, true);
    settle(400);
    TextEditor *e = nullptr;
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible() && !e)
            e = ep->currentEditor();
    check(e != nullptr, QStringLiteral("(editor open)"));
    if (!e)
        return 1;
    e->setFocus();
    settle(150);
    QTextDocument *text = e->document();

    // ============================================================ a design's own Bold in the text
    {
        check(fontCombo->findText(kFamily + QLatin1String(" Display")) >= 0 && fontCombo->findText(kFamily) >= 0,
              QStringLiteral("the font list of the format bar offers \"... Display\" and the others next to the family itself"));
        selectWord(e, QStringLiteral("keeper climbed"), false);
        fontCombo->setCurrentFont(QFont(kFamily + QLatin1String(" Display")));
        settle(200);
        e->setFocus();
        selectWord(e, QStringLiteral("keeper"), false);
        bold->trigger();
        settle(200);
        const QTextCharFormat k = formatAt(text, QStringLiteral("keeper"));
        const QTextCharFormat c = formatAt(text, QStringLiteral("climbed"));
        check(direct::charSpec(k).family == kFamily + QLatin1String(" Display") && k.fontWeight() == QFont::Bold
                  && faceOf(k) == kFamily + QLatin1String(" Display / Display Bold")
                  && faceOf(c) == kFamily + QLatin1String(" Display / Display"),
              QStringLiteral("text in \"... Display\" made bold with the Bold command shows Display Bold, the plain words "
                             "next to it Display"),
              faceOf(k) + QLatin1String(" | ") + faceOf(c));
        selectWord(e, QStringLiteral("climbed"), false);
        italic->trigger();
        settle(150);
        check(faceOf(formatAt(text, QStringLiteral("climbed"))) == kFamily + QLatin1String(" Display / Display Italic"),
              QStringLiteral("... and Italic shows Display Italic"), faceOf(formatAt(text, QStringLiteral("climbed"))));
        QTextCursor none = e->textCursor();
        none.clearSelection();
        e->setTextCursor(none);
        shot(&w, QStringLiteral("01-display-bold-and-italic"));

        // Format > Font > Show Fonts: the design is a font of its own with its typefaces
        selectWord(e, QStringLiteral("keeper"), false);
        bool listed = false, faces = false;
        QString facesDetail;
        QTimer::singleShot(500, qApp, [&] {
            auto *dlg = qobject_cast<FontDialog *>(QApplication::activeModalWidget());
            if (!dlg)
                return;
            auto *fams = dlg->findChild<QListWidget *>(QStringLiteral("fontFamilies"));
            auto *tf = dlg->findChild<QListWidget *>(QStringLiteral("fontTypefaces"));
            if (fams && tf) {
                listed = fams->currentItem() && fams->currentItem()->text() == kFamily + QLatin1String(" Display");
                QStringList names;
                for (int i = 0; i < tf->count(); ++i)
                    names << tf->item(i)->text();
                facesDetail = names.join(QLatin1String(", "));
                faces = names.size() == 12 && names.contains(QLatin1String("Bold")) && tf->currentItem()
                        && tf->currentItem()->text() == QLatin1String("Bold");
            }
            shot(dlg, QStringLiteral("02-show-fonts-display"));
            dlg->reject();
        });
        byKey(&w, QStringLiteral("showFonts"))->trigger();
        settle(300);
        check(listed && faces,
              QStringLiteral("Format > Font > Show Fonts: the bold Display word shows \"... Display\" with its 12 typefaces, "
                             "Bold chosen"),
              facesDetail);

        // saved and read again: the family name of the design is in the file, Bold stays Display Bold
        QString saveErr;
        w.project()->save(&saveErr);
        const QString path = w.project()->docs()->textPath(doc->id);
        QByteArray raw;
        {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly))
                raw = f.readAll();
        }
        const QString stored = QString::fromUtf8(raw);
        QTextDocument again;
        if (path.endsWith(QLatin1String(".md")))
            md::read(stored, &again, w.project()->formatting);
        else
            rtf::read(raw, &again, w.project()->formatting);
        check(stored.contains(kFamily + QLatin1String(" Display"))
                  && faceOf(formatAt(&again, QStringLiteral("keeper"))) == kFamily + QLatin1String(" Display / Display Bold"),
              QStringLiteral("saved: the text file names \"... Display\"; read again, \"keeper\" is Display Bold"),
              stored.left(400));
    }

    // ============================================================ the buttons show the selection
    {
        // the keyboard back in the text (the font window had it): the format bar follows the
        // editor that has the keyboard
        w.activateWindow();
        e->setFocus();
        settle(200);
        check(e->hasFocus(), QStringLiteral("(the editor has the keyboard)"));
        selectWord(e, QStringLiteral("beta"), false);
        bold->trigger();
        settle(150);
        selectWord(e, QStringLiteral("beta"), true);
        const bool backwards = bold->isChecked();
        shot(&w, QStringLiteral("03-bold-word-selected-backwards"));
        selectWord(e, QStringLiteral("beta"), false);
        const bool forwards = bold->isChecked();
        check(backwards && forwards,
              QStringLiteral("a bold word selected from right to left shows Bold on (the reported problem), as from left to right"),
              QStringLiteral("right to left: %1, left to right: %2").arg(backwards).arg(forwards));

        selectWord(e, QStringLiteral("beta gamma"), true);
        const bool mixedOff = !bold->isChecked();
        bold->trigger();
        settle(150);
        const bool nowAll = formatAt(text, QStringLiteral("gamma")).fontWeight() == QFont::Bold
                            && formatAt(text, QStringLiteral("beta")).fontWeight() == QFont::Bold && bold->isChecked();
        bold->trigger();
        settle(150);
        const bool offAgain = formatAt(text, QStringLiteral("gamma")).fontWeight() < QFont::DemiBold
                              && formatAt(text, QStringLiteral("beta")).fontWeight() < QFont::DemiBold && !bold->isChecked();
        check(mixedOff && nowAll && offAgain,
              QStringLiteral("a selection that is only partly bold shows Bold off; Bold makes all of it bold (button on), "
                             "Bold again all of it plain (button off)"),
              QStringLiteral("%1 %2 %3").arg(mixedOff).arg(nowAll).arg(offAgain));

        // italic, underline, strikethrough the same way
        selectWord(e, QStringLiteral("delta"), false);
        italic->trigger();
        underline->trigger();
        strike->trigger();
        settle(150);
        selectWord(e, QStringLiteral("delta"), true);
        const bool three = italic->isChecked() && underline->isChecked() && strike->isChecked() && !bold->isChecked();
        selectWord(e, QStringLiteral("delta epsilon"), true);
        const bool threeOff = !italic->isChecked() && !underline->isChecked() && !strike->isChecked();
        check(three && threeOff,
              QStringLiteral("Italic, Underline and Strikethrough: on for a word that has them (selected from right to left), "
                             "off when the selection reaches plain text"),
              QStringLiteral("%1 %2").arg(three).arg(threeOff));

        // no selection: the format at the cursor, as before
        QTextCursor c = e->textCursor();
        c.setPosition(int(text->toPlainText().indexOf(QLatin1String("delta"))) + 2);
        e->setTextCursor(c);
        settle(120);
        const bool inside = italic->isChecked() && !bold->isChecked();
        // a heading: bold comes from its style; since 0.40.3 the button shows what is seen
        selectWord(e, QStringLiteral("At the Lamp"), true);
        const bool heading = bold->isChecked();
        check(inside && heading,
              QStringLiteral("without a selection the buttons show the format at the cursor; a heading's bold from its style "
                             "lights the button (0.40.3; before, only bold set by hand did)"),
              QStringLiteral("%1 %2").arg(inside).arg(heading));
    }

    // ============================================================ the Fonts window on a partly bold selection
    {
        e->setFocus();
        selectWord(e, QStringLiteral("beta"), false);
        bold->trigger();
        settle(120);
        selectWord(e, QStringLiteral("beta gamma"), true);
        QString shown;
        QTimer::singleShot(500, qApp, [&] {
            auto *dlg = qobject_cast<FontDialog *>(QApplication::activeModalWidget());
            if (!dlg)
                return;
            if (auto *tf = dlg->findChild<QListWidget *>(QStringLiteral("fontTypefaces"))) {
                shown = tf->currentItem() ? tf->currentItem()->text() : QString();
                const auto hits = tf->findItems(QStringLiteral("Bold"), Qt::MatchExactly);
                if (!hits.isEmpty())
                    tf->setCurrentItem(hits.first());
            }
            dlg->accept();
        });
        byKey(&w, QStringLiteral("showFonts"))->trigger();
        settle(300);
        const bool both = formatAt(text, QStringLiteral("beta")).fontWeight() == QFont::Bold
                          && formatAt(text, QStringLiteral("gamma")).fontWeight() == QFont::Bold;
        check(shown == QLatin1String("Regular") && both,
              QStringLiteral("Format > Font > Show Fonts on a partly bold selection: shows Regular (not all of it is bold); "
                             "choosing Bold makes all of it bold"),
              QStringLiteral("shown: %1; beta %2, gamma %3")
                  .arg(shown)
                  .arg(formatAt(text, QStringLiteral("beta")).fontWeight())
                  .arg(formatAt(text, QStringLiteral("gamma")).fontWeight()));
    }

    // ============================================================ text fields of objects
    {
        ObjectTextEdit field;
        field.setMarkdownText(QStringLiteral("north **lantern** south"));
        selectWord(&field, QStringLiteral("lantern"), true);
        const bool back = field.isOn(ObjectTextEdit::Toggle::Bold);
        selectWord(&field, QStringLiteral("lantern south"), true);
        const bool mixed = !field.isOn(ObjectTextEdit::Toggle::Bold);
        field.toggle(ObjectTextEdit::Toggle::Bold);
        const bool made = field.markdownText().contains(QLatin1String("**lantern south**"));
        check(back && mixed && made,
              QStringLiteral("a text field of an object: a bold word selected from right to left counts as bold, a mixed "
                             "selection not; Bold then makes all of it bold"),
              QStringLiteral("%1 %2 %3: %4").arg(back).arg(mixed).arg(made).arg(field.markdownText()));
    }

    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
