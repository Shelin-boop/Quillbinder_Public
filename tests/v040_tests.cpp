// Scripted run for version 0.40: the welcome project (made at the first start and by
// Help > Welcome Project from resources/welcome, with its mind map, objects, links and
// comments), Help > Quick Start Guide, Help > Third-Party Licenses and the About window
// (0BSD, credits), the view "Combined Text" (the settings and themes of older versions
// still read), nothing left of the interactive tutorial of 0.15 to 0.30, the menu places
// the guide and the welcome project name, Help > List of All Placeholders, and two
// corrections carried over from the suite of the old tutorial (an index card's picture
// on the corkboard, the color of an inline annotation). Saves screenshots into the folder
// given as first argument. Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v040_tests /tmp/shots
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/OutlineProject.h"
#include "core/Project.h"
#include "text/MarkdownIO.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/MediaView.h"
#include "ui/MindMapController.h"
#include "ui/MindMapUi.h"
#include "ui/MindMapView.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
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

static QString menuText(QString t)
{
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.remove(QLatin1Char('&'));
    t.remove(QChar(0x2026));
    t.remove(QLatin1String("..."));
    return t.trimmed();
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

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

// a message box: the button with this text is clicked; its text is kept
static void answer(int delayMs, const QString &buttonText, QString *seen = nullptr)
{
    withModal<QMessageBox>(delayMs, [buttonText, seen](QMessageBox *b) {
        if (seen)
            *seen = b->text() + QLatin1Char('\n') + b->informativeText();
        shot(b, QStringLiteral("v040-question-") + QString(buttonText).replace(QLatin1Char(' '), QLatin1Char('-')).toLower());
        for (QAbstractButton *btn : b->buttons())
            if (menuText(btn->text()) == buttonText) {
                btn->click();
                return;
            }
        info(QStringLiteral("(no button \"%1\")").arg(buttonText));
        b->reject();
    });
}

static BinderItem *find(Project *p, const QString &title)
{
    if (!p)
        return nullptr;
    for (BinderItem *it : p->allItems())
        if (it->title == title)
            return it;
    return nullptr;
}

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *p : w.findChildren<EditorPane *>())
        if (p->isVisible())
            return p;
    return nullptr;
}

static QStringList anchors(QTextDocument *doc)
{
    QStringList out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto f = b.begin(); !f.atEnd(); ++f)
            if (f.fragment().charFormat().isAnchor())
                out << f.fragment().charFormat().anchorHref();
    return out;
}

static bool hasNoteKind(QTextDocument *doc, NoteKind kind)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto f = b.begin(); !f.atEnd(); ++f)
            if (NoteKind(f.fragment().charFormat().intProperty(props::NoteKind)) == kind)
                return true;
    return false;
}

// every text of every menu, submenus included (filled first, as when they open)
static void allMenuTexts(QMenu *m, const QString &path, QStringList *out, int depth = 0)
{
    if (depth > 6)
        return;
    emit m->aboutToShow();
    for (QAction *a : m->actions()) {
        if (a->isSeparator())
            continue;
        const QString p = path + QLatin1String(" > ") + menuText(a->text());
        out->append(p + QLatin1String(" | ") + a->toolTip() + QLatin1String(" | ") + a->iconText());
        if (a->menu())
            allMenuTexts(a->menu(), p, out, depth + 1);
    }
}

// The menu places "**A > B > C**" of a text: each must be in the menus (a place may end in a
// window's page after a command, as "File > Options > Appearance").
static QStringList missingMenuPlaces(MainWindow *w, const QString &text, int *count)
{
    static const QRegularExpression pathRe(QStringLiteral("\\*\\*([A-Z][^*\\n]*? > [^*\\n]+)\\*\\*"));
    QStringList tops;
    for (QAction *a : w->menuBar()->actions())
        tops << menuText(a->text());
    QStringList missing;
    QSet<QString> seen;
    auto it = pathRe.globalMatch(text);
    while (it.hasNext()) {
        const QString raw = it.next().captured(1);
        QStringList parts = raw.split(QStringLiteral(" > "));
        for (QString &x : parts)
            x = menuText(x);
        if (!tops.contains(parts.first()) || seen.contains(parts.join(QLatin1Char('>'))))
            continue;
        seen.insert(parts.join(QLatin1Char('>')));
        QList<QAction *> level = w->menuBar()->actions();
        for (int i = 0; i < parts.size(); ++i) {
            QAction *found = nullptr;
            for (QAction *a : level)
                if (menuText(a->text()) == parts[i]) {
                    found = a;
                    break;
                }
            if (!found) {
                missing << raw;
                break;
            }
            if (!found->menu())
                break; // a command; what follows is a page of its window
            emit found->menu()->aboutToShow();
            level = found->menu()->actions();
        }
    }
    *count = int(seen.size());
    return missing;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_DOCUMENTS_DIR", (home.path() + QLatin1String("/Documents")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/Documents"));
    // (no data folder yet: the program must make it before it uses the desktop's trash)
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV040Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV040Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("DejaVu Sans"), 10));
    const QString appName = QStringLiteral(QB_APP_NAME);
    const QString src = QStringLiteral(QB_TEST_SOURCE_DIR "/..");

    // ============================================================ settings of older versions
    {
        QSettings s;
        s.setValue(QStringLiteral("appearance/scriveningsTitles"), true);
        s.setValue(QStringLiteral("appearance/fonts/scriveningsTitle"), QStringLiteral("DejaVu Serif|15"));
        s.setValue(QStringLiteral("app/tutorialFolder"), home.path());
        s.sync();
    }
    AppSettings &as = AppSettings::instance();
    as.load();
    check(as.showTitlesInCombined, QStringLiteral("settings of 0.30: \"Show titles\" of the combined text is read from the old name"));
    check(as.ownFonts.contains(QStringLiteral("combinedTitle")) && !as.ownFonts.contains(QStringLiteral("scriveningsTitle")),
          QStringLiteral("... and the font of its titles comes under the new name"), QStringList(as.ownFonts.keys()).join(QLatin1String(", ")));
    theme::FontRole fr;
    check(theme::fontRoleFromKey(QStringLiteral("scriveningsTitle"), &fr) && fr == theme::FontRole::CombinedTitle
              && QLatin1String(theme::key(theme::FontRole::CombinedTitle)) == QLatin1String("combinedTitle"),
          QStringLiteral("theme files of older versions: the old font name still means the titles of the combined text"));
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.save();
    {
        QSettings s;
        s.sync();
        const QStringList keys = s.allKeys();
        check(!keys.contains(QStringLiteral("appearance/scriveningsTitles")) && !keys.contains(QStringLiteral("app/tutorialFolder"))
                  && !keys.contains(QStringLiteral("appearance/fonts/scriveningsTitle"))
                  && s.value(QStringLiteral("appearance/combinedTitles")).toBool()
                  && s.contains(QStringLiteral("appearance/fonts/combinedTitle")),
              QStringLiteral("... saved again under the new names only; the tutorial's folder is gone"),
              keys.filter(QStringLiteral("criv")).join(QLatin1String(", ")) + QLatin1Char(' ')
                  + keys.filter(QStringLiteral("utorial")).join(QLatin1String(", ")));
    }
    check(!as.welcomeShown, QStringLiteral("the welcome project has not been shown in these settings yet"));
    theme::apply();

    // ============================================================ version, license, nothing left of the tutorial
    check(QLatin1String(QB_APP_VERSION) == QLatin1String("0.40.0") && QLatin1String(QB_APP_VERSION_SHOWN) == QLatin1String("0.40.0 alpha"),
          QStringLiteral("version 0.40.0, shown as \"0.40.0 alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));
    {
        const QString lic = QString::fromUtf8(readAll(src + QLatin1String("/LICENSE")));
        check(lic.startsWith(QLatin1String("Copyright (C) 2026 by the Quillbinder authors\n"))
                  && lic.contains(QLatin1String("Permission to use, copy, modify, and/or distribute this software for any\npurpose with or "
                                                "without fee is hereby granted."))
                  && !lic.contains(QLatin1String("MIT")),
              QStringLiteral("LICENSE is the Zero-Clause BSD license, with no person named"), lic.left(200));
        const QString third = QString::fromUtf8(readAll(src + QLatin1String("/THIRD-PARTY.md")));
        check(third.contains(QLatin1String("0BSD")) && third.contains(QLatin1String("Help > Third-Party Licenses")),
              QStringLiteral("THIRD-PARTY.md names the 0BSD license and Help > Third-Party Licenses"));
        // The pen name of earlier versions is in no file: it is looked for by the SHA-256 of three
        // words in a row, so that this test does not name it itself. The old tutorial: its files
        // are gone, and its command is named only in the development log.
        const QByteArray penName = QByteArray::fromHex("7fdbda2a60c9b936f620bb2eb0524ea26a80de567309a56231848816ebc59a7f");
        QStringList named, tutorial;
        QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString rel = QDir(src).relativeFilePath(f);
            if (rel.startsWith(QLatin1String(".git/")) || rel.startsWith(QLatin1String("build")) || rel.startsWith(QLatin1String("dist"))
                || rel.startsWith(QLatin1String("packaging/tools/")) || f.endsWith(QLatin1String(".png")) || f.endsWith(QLatin1String(".svg"))
                || f.endsWith(QLatin1String(".ogg")) || f.endsWith(QLatin1String(".qbpkg")) || f.endsWith(QLatin1String(".zip"))
                || rel.startsWith(QLatin1String("tests/media/")) || rel.startsWith(QLatin1String("tests/import/")))
                continue;
            const QByteArray b = readAll(f);
            // words without the punctuation around them ("(c)", quotes, commas); an initial keeps its dot
            QList<QByteArray> words;
            for (QByteArray w : b.simplified().split(' ')) {
                while (!w.isEmpty() && !QChar::isLetter(uchar(w.front())))
                    w.remove(0, 1);
                while (w.size() > 2 && !QChar::isLetter(uchar(w.back())))
                    w.chop(1);
                if (w.size() == 2 && !QChar::isLetter(uchar(w.back())) && w.back() != '.')
                    w.chop(1);
                words << w;
            }
            for (int i = 0; i + 2 < words.size(); ++i)
                if (words.at(i + 1).size() == 2 && words.at(i + 1).endsWith('.')
                    && QCryptographicHash::hash(words.at(i) + ' ' + words.at(i + 1) + ' ' + words.at(i + 2), QCryptographicHash::Sha256)
                           == penName) {
                    named << rel;
                    break;
                }
            if (b.contains("Interactive Tutorial") && rel != QLatin1String("docs/status.md") && rel != QLatin1String("tests/v040_tests.cpp"))
                tutorial << rel;
        }
        check(named.isEmpty(), QStringLiteral("the pen name of earlier versions is in no file of the source"), named.join(QLatin1String(", ")));
        check(tutorial.isEmpty() && !QFileInfo::exists(src + QLatin1String("/docs/tutorial.txt"))
                  && !QFileInfo::exists(src + QLatin1String("/addons/tutorial")),
              QStringLiteral("nothing of the old tutorial is left in the source"), tutorial.join(QLatin1String(", ")));
    }

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);

    // ============================================================ menus: Help, no tutorial, no "Scrivenings"
    {
        QMenu *help = nullptr;
        for (QAction *a : w.menuBar()->actions())
            if (a->menu() && menuText(a->text()) == QLatin1String("Help"))
                help = a->menu();
        QStringList entries;
        if (help)
            for (QAction *a : help->actions())
                if (!a->isSeparator())
                    entries << menuText(a->text());
        check(entries.value(0) == QLatin1String("User Manual") && entries.value(1) == QLatin1String("Quick Start Guide")
                  && entries.value(2) == QLatin1String("Welcome Project") && entries.contains(QLatin1String("Third-Party Licenses"))
                  && entries.last() == QStringLiteral("About %1").arg(appName),
              QStringLiteral("Help menu: User Manual, Quick Start Guide, Welcome Project…, ..., Third-Party Licenses…, About"),
              entries.join(QLatin1String(" | ")));
        check(byKey(&w, QStringLiteral("quickStart"))->isEnabled() && byKey(&w, QStringLiteral("welcome"))->isEnabled()
                  && byKey(&w, QStringLiteral("licenses"))->isEnabled() && !byKey(&w, QStringLiteral("tutorial")),
              QStringLiteral("... the three work without a project; the command of the old tutorial is gone"));
        QStringList all;
        for (QAction *a : w.menuBar()->actions())
            if (a->menu())
                allMenuTexts(a->menu(), menuText(a->text()), &all);
        const QStringList bad = all.filter(QRegularExpression(QStringLiteral("[Ss]crivening|[Tt]utorial")));
        check(all.size() > 300 && bad.isEmpty(), QStringLiteral("no menu entry, tool tip or button text says \"Scrivenings\" or \"Tutorial\" (%1 entries)").arg(all.size()),
              bad.join(QLatin1String("; ")));
        check(all.filter(QStringLiteral("View > Text Editing > Show Titles in Combined Text")).size() == 1,
              QStringLiteral("View > Text Editing > \"Show Titles in Combined Text\""), all.filter(QStringLiteral("Combined")).join(QLatin1String("; ")));
        QStringList fontLabels;
        for (const theme::FontInfo &fi : theme::fontRoles())
            fontLabels << fi.label;
        check(fontLabels.contains(QStringLiteral("Titles in Combined Text")) && fontLabels.filter(QStringLiteral("criven")).isEmpty(),
              QStringLiteral("Options > Appearance: the font \"Titles in Combined Text\""), fontLabels.join(QLatin1String(", ")));
    }

    // ============================================================ Help > Quick Start Guide
    {
        byKey(&w, QStringLiteral("quickStart"))->trigger();
        settle(500);
        auto *tb = qApp->findChild<QTextBrowser *>(QStringLiteral("quickStart"));
        if (!tb)
            for (QWidget *top : QApplication::topLevelWidgets())
                if (auto *t = top->findChild<QTextBrowser *>(QStringLiteral("quickStart")))
                    tb = t;
        const QString text = tb ? tb->toPlainText() : QString();
        check(tb && tb->window()->isVisible() && text.contains(QStringLiteral("%1 Quick Start Guide").arg(appName))
                  && text.contains(QLatin1String("Dark mode")) && text.contains(QLatin1String("Window > Themes"))
                  && text.contains(QLatin1String("Import Scrivener Project")) && text.contains(QLatin1String("Work in Scrivener Format"))
                  && text.contains(QLatin1String("Markdown")) && !text.contains(QLatin1String("{{")),
              QStringLiteral("Help > Quick Start Guide opens the guide: settings (dark mode ...) and capabilities (Scrivener, own format ...)"),
              text.left(400));
        if (tb) {
            tb->window()->resize(820, 760);
            shot(tb->window(), QStringLiteral("v040-01-quick-start-guide"));
            tb->window()->close();
        }
        int n = 0;
        const QStringList missing = missingMenuPlaces(
            &w, QString::fromUtf8(readAll(QStringLiteral(":/docs/quick-start.md"))).replace(QLatin1String("{{APP}}"), appName), &n);
        check(n >= 40 && missing.isEmpty(), QStringLiteral("every menu place the Quick Start Guide names is in the menus (%1)").arg(n),
              missing.join(QLatin1String("; ")));
    }

    // ============================================================ About and Help > Third-Party Licenses
    {
        const QString about = MainWindow::aboutText();
        check(about.contains(QLatin1String("0BSD")) && about.contains(QLatin1String("Main Developer: Claude.AI w. Opus 5.5 Extra"))
                  && !about.contains(QLatin1String("Project Manager"))
                  && !about.contains(QLatin1String("MIT")) && about.contains(QLatin1String("not made by or connected with Literature"))
                  && about.contains(QLatin1String("0.40.0 alpha")),
              QStringLiteral("About: version, the 0BSD license, Claude as the only credit, independent of Literature & Latte"), about);
        bool button = false;
        withModal<QMessageBox>(500, [&](QMessageBox *b) {
            shot(b, QStringLiteral("v040-02-about"));
            for (QAbstractButton *btn : b->buttons())
                if (btn->objectName() == QLatin1String("aboutLicenses")) {
                    button = true;
                    btn->click();
                    return;
                }
            b->reject();
        });
        byKey(&w, QStringLiteral("about"))->trigger();
        settle(600);
        QWidget *lw = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (top->objectName() == QLatin1String("licensesWindow") && top->isVisible())
                lw = top;
        check(button && lw, QStringLiteral("... its button \"Third-Party Licenses…\" opens the licenses window"));
        if (!lw) {
            byKey(&w, QStringLiteral("licenses"))->trigger();
            settle(500);
            for (QWidget *top : QApplication::topLevelWidgets())
                if (top->objectName() == QLatin1String("licensesWindow") && top->isVisible())
                    lw = top;
        }
        auto *list = lw ? lw->findChild<QListWidget *>(QStringLiteral("licensesList")) : nullptr;
        auto *text = lw ? lw->findChild<QPlainTextEdit *>(QStringLiteral("licensesText")) : nullptr;
        QString first, qt, lgpl;
        QStringList names;
        if (list && text) {
            first = text->toPlainText();
            for (int i = 0; i < list->count(); ++i) {
                names << list->item(i)->text();
                if (list->item(i)->text().startsWith(QLatin1String("qt6-base"))) {
                    list->setCurrentRow(i);
                    settle(100);
                    qt = text->toPlainText();
                }
                if (list->item(i)->text().trimmed() == QLatin1String("LGPL-3")) {
                    list->setCurrentRow(i);
                    settle(100);
                    lgpl = text->toPlainText();
                }
            }
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text().startsWith(QLatin1String("qt6-base")))
                    list->setCurrentRow(i);
            shot(lw, QStringLiteral("v040-03-third-party-licenses"));
        }
        check(names.size() >= 40 && first.startsWith(appName) && first.contains(QLatin1String("0BSD"))
                  && first.contains(QLatin1String("Permission to use, copy, modify")),
              QStringLiteral("Third-Party Licenses: %1 entries, the first is %2 itself with its 0BSD text").arg(names.size()).arg(appName),
              names.join(QLatin1String(", ")));
        check(qt.contains(QLatin1String("LGPL-3")) && qt.contains(QLatin1String("Source: Ubuntu 24.04 source package qt6-base"))
                  && qt.contains(QLatin1String("libQt6Core.so.6")),
              QStringLiteral("... Qt: its libraries, its license, where its source code is, and Ubuntu's copyright file"), qt.left(600));
        check(lgpl.contains(QLatin1String("GNU LESSER GENERAL PUBLIC LICENSE")) && lgpl.contains(QLatin1String("Version 3")),
              QStringLiteral("... the full text of the LGPL 3 is there"), lgpl.left(300));
        // every library the AppImage carries has its entry, and every text can be read
        const QJsonObject index = QJsonDocument::fromJson(readAll(QStringLiteral(":/licenses/index.json"))).object();
        QStringList where, unreadable;
        QJsonArray entries = index.value(QStringLiteral("components")).toArray();
        for (const QJsonValue &v : index.value(QStringLiteral("commonLicenses")).toArray())
            entries.append(v);
        for (const QJsonValue &v : std::as_const(entries)) {
            where << v.toObject().value(QStringLiteral("where")).toString();
            if (readAll(QStringLiteral(":/licenses/texts/") + v.toObject().value(QStringLiteral("text")).toString()).size() < 200)
                unreadable << v.toObject().value(QStringLiteral("text")).toString();
        }
        const QString all = where.join(QLatin1String(", "));
        QStringList absent;
        for (const char *lib : {"libQt6Core.so.6", "libQt6Widgets.so.6", "libhunspell-1.7.so.0", "libicuuc.so.74", "libpng16.so.16",
                                "libqxcb.so", "libqwayland-generic.so", "libxkbcommon.so.0", "libgomp.so.1"})
            if (!all.contains(QLatin1String(lib)))
                absent << QLatin1String(lib);
        check(absent.isEmpty() && unreadable.isEmpty() && all.contains(QLatin1String("AppImage runtime")),
              QStringLiteral("... the libraries and plugins of the AppImage and its runtime are all listed, every text can be read"),
              absent.join(QLatin1String(", ")) + QLatin1String(" / ") + unreadable.join(QLatin1String(", ")));
        if (lw)
            lw->close();
    }

    // ============================================================ the first start: the welcome project
    const QString docs = home.path() + QLatin1String("/Documents");
    as.welcomeFolder = docs;
    as.save();
    Project *p = nullptr;
    {
        const bool opened = w.openWelcomeOnFirstStart();
        settle(1200);
        p = w.project();
        check(opened && p && p->title() == QStringLiteral("Welcome to %1").arg(appName)
                  && QFileInfo(p->folderPath()).absolutePath() == QFileInfo(docs).absoluteFilePath() && as.welcomeShown,
              QStringLiteral("first start: the welcome project is made in Documents and opened"),
              p ? p->folderPath() : QStringLiteral("(no project)"));
        BinderItem *sel = w.findChild<BinderPanel *>()->selectedItems().value(0);
        check(sel && sel->title == QLatin1String("Welcome") && p->root()->children.value(0) == sel && p->root()->children.value(1) == p->draft(),
              QStringLiteral("... it begins with \"Welcome\", above the folder of the tour"), sel ? sel->title : QString());
        EditorPane *pane = visiblePane(w);
        TextEditor *ed = pane ? pane->currentEditor() : nullptr;
        check(ed && ed->toPlainText().startsWith(QLatin1String("This small project shows what")),
              QStringLiteral("... and shows its text"), ed ? ed->toPlainText().left(80) : QString());
        shot(&w, QStringLiteral("v040-04-welcome"));
        check(!w.openWelcomeOnFirstStart(), QStringLiteral("... only once: the next start does not open it again"));
    }

    // ============================================================ what the welcome project holds
    if (p) {
        const QStringList tour = {QStringLiteral("The Icon Bar"), QStringLiteral("Zoom Where You Point"), QStringLiteral("Mind Maps"),
                                  QStringLiteral("Object Sheets"), QStringLiteral("Comments of a Whole Folder"),
                                  QStringLiteral("A Chapter with Comments"), QStringLiteral("Compare and Transfer"),
                                  QStringLiteral("Scrivener Projects"), QStringLiteral("Themes, Colors, and Fonts"),
                                  QStringLiteral("Updates and Add-ons"), QStringLiteral("Safety Nets"), QStringLiteral("Where Next")};
        QStringList titles;
        for (BinderItem *it : p->draft()->children)
            titles << it->title;
        check(p->draft()->title == QLatin1String("Feature Tour") && titles == tour,
              QStringLiteral("the Feature Tour: %1 stops").arg(tour.size()), titles.join(QLatin1String(" | ")));
        QStringList emptyOrRaw, longOnes;
        int words = 0;
        for (BinderItem *it : p->allItems()) {
            if (!it->hasText() || it->type == ItemType::Folder || it == p->draft() || it->title == QLatin1String("Trash")
                || it->title == QLatin1String("Research"))
                continue;
            const QString t = p->docs()->text(it->id)->toPlainText();
            const int n = int(t.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size());
            words += n;
            if (n < 30 || t.contains(QLatin1String("{{")) || t.contains(QLatin1String("qb-title:")) || t.contains(QLatin1String("qbobj-name:"))
                || t.contains(QLatin1String("Scrivenings")))
                emptyOrRaw << it->title;
            if (n > 260)
                longOnes << QStringLiteral("%1 (%2)").arg(it->title).arg(n);
        }
        check(emptyOrRaw.isEmpty(), QStringLiteral("every text is filled in (program name, links), none says \"Scrivenings\""),
              emptyOrRaw.join(QLatin1String(", ")));
        check(longOnes.isEmpty() && words < 2200, QStringLiteral("... and short: %1 words in all, no text over 260 words").arg(words),
              longOnes.join(QLatin1String(", ")));
        // the Welcome document links to every stop of the tour
        BinderItem *welcome = find(p, QStringLiteral("Welcome"));
        QStringList linked;
        for (const QString &h : anchors(p->docs()->text(welcome->id)))
            if (BinderItem *t = p->item(h.mid(5)))
                linked << t->title;
        QStringList notLinked;
        for (const QString &t : tour)
            if (t != QLatin1String("A Chapter with Comments") && !linked.contains(t))
                notLinked << t;
        check(notLinked.isEmpty() && linked.size() == 11, QStringLiteral("\"Welcome\" links to every stop of the tour (%1 links)").arg(linked.size()),
              notLinked.join(QLatin1String(", ")));
        check(p->projectBookmarks.size() == 1 && p->projectBookmarks.first().internalId() == welcome->id,
              QStringLiteral("... and is the project bookmark"));
        BinderItem *iconBar = find(p, QStringLiteral("The Icon Bar"));
        BinderItem *mm = find(p, QStringLiteral("Mind Maps"));
        check(iconBar && iconBar->icon == QLatin1String("qb-flag-red") && mm && mm->icon == QLatin1String("qb-mindmap")
                  && find(p, QStringLiteral("Object Sheets"))->icon == QLatin1String("qb-character-sheet"),
              QStringLiteral("the documents have icons of their own"));

        // the mind map: an overview of what boards can do
        mindmap::Board board;
        QString err;
        const bool loaded = mm && board.load(mindmap::pathFor(p->contentDir(mm->id)), &err);
        int arrows = 0;
        for (const mindmap::Note &n : board.notes)
            arrows += int(n.pointsTo.size());
        const mindmap::Note *center = board.note(1);
        const mindmap::Note *icons = board.note(6);
        check(loaded && board.notes.size() == 9 && board.shapes.size() == 1 && center && center->connected.size() == 8 && arrows == 7,
              QStringLiteral("\"Mind Maps\" has a board: 9 notes around one in the middle, a background shape, 8 lines, 7 arrows"),
              QStringLiteral("%1 notes, %2 shapes, %3 arrows %4").arg(board.notes.size()).arg(board.shapes.size()).arg(arrows).arg(err));
        check(center && center->look.borderStyle == mindmap::BorderStyle::Cloud && icons && icons->icons.size() == 3
                  && board.lineLabel(1, 3) == QLatin1String("drag") && board.lineLook(1, 6).icon == QLatin1String("qb-pencil")
                  && board.lineLook(1, 4).color.isValid() && board.note(5)->look.borderStyle == mindmap::BorderStyle::Jagged,
              QStringLiteral("... with shapes, colors, icons on a note, a word on a line, a colored line and an icon on a line"));

        // the objects
        objects::Store *st = p->objects();
        QHash<QString, const objects::Object *> byName;
        for (const objects::Object *o : st->objects())
            byName.insert(o->name, o);
        const objects::Object *mara = byName.value(QStringLiteral("Mara Fenn"));
        const objects::Object *ash = byName.value(QStringLiteral("Ashvale"));
        BinderItem *scene = find(p, QStringLiteral("A Scene in Ashvale"));
        check(st->count() == 5 && mara && ash && mara->category == QLatin1String("characters")
                  && mara->ids(QStringLiteral("livesIn")) == QStringList{ash->id}
                  && ash->ids(QStringLiteral("environment")) == QStringList{byName.value(QStringLiteral("Fenwood"))->id}
                  && mara->ids(QStringLiteral("scenes")) == QStringList{scene->id}
                  && mara->text(QStringLiteral("background")).contains(objects::linkTo(byName.value(QStringLiteral("Tobias Quill"))->id)),
              QStringLiteral("five objects, linked: Mara lives in Ashvale, Ashvale lies in Fenwood, Mara appears in the scene, a link in her background"));
        QStringList sceneLinks;
        for (const QString &h : anchors(p->docs()->text(scene->id)))
            if (const objects::Object *o = st->object(objects::idFromLink(h)))
                sceneLinks << o->name;
        sceneLinks.removeDuplicates();
        check(sceneLinks.size() == 5, QStringLiteral("\"A Scene in Ashvale\" links to all five objects in its text"), sceneLinks.join(QLatin1String(", ")));
        BinderItem *ferry = find(p, QStringLiteral("The Ferry Book"));
        BinderItem *evening = find(p, QStringLiteral("Evening"));
        check(ferry && hasNoteKind(p->docs()->text(ferry->id), NoteKind::Comment) && evening
                  && hasNoteKind(p->docs()->text(evening->id), NoteKind::LinkedFootnote)
                  && hasNoteKind(p->docs()->text(evening->id), NoteKind::InlineAnnotation),
              QStringLiteral("the chapter with comments: a comment, a footnote and an inline annotation"));

        // walking through: the mind map tab, the objects tab, the comments of the folder
        auto *binder = w.findChild<BinderPanel *>();
        auto *insp = w.findChild<Inspector *>();
        if (!insp->isVisible())
            byKey(&w, QStringLiteral("showInspector"))->setChecked(true);
        binder->select({mm}, true);
        settle(400);
        insp->showTab(Inspector::MindMap);
        settle(800);
        int boardItems = 0;
        for (MindMapView *v : insp->findChildren<MindMapView *>())
            if (v->isVisible() && v->scene())
                boardItems = qMax(boardItems, int(v->scene()->items().size()));
        shot(&w, QStringLiteral("v040-05-mind-map-tab"));
        check(boardItems >= 18, QStringLiteral("\"Mind Maps\": the Mind Map tab of the inspector shows the board (%1 items drawn)").arg(boardItems));
        QToolButton *big = nullptr;
        for (QToolButton *b : insp->findChildren<QToolButton *>(QStringLiteral("mmOpenWindow")))
            if (b->isVisible())
                big = b;
        check(big != nullptr, QStringLiteral("... with its button Open in Window"));
        if (big) {
            big->click();
            settle(900);
            for (QWidget *top : QApplication::topLevelWidgets())
                if (top->isVisible() && top != &w && top->findChild<MindMapView *>()) {
                    top->resize(1100, 760);
                    shot(top, QStringLiteral("v040-06-mind-map-window"));
                    top->close();
                }
        }
        binder->select({scene}, true);
        settle(400);
        insp->showTab(Inspector::Objects);
        settle(700);
        QWidget *row = insp->findChild<QWidget *>(QStringLiteral("objectsInDocument"));
        shot(&w, QStringLiteral("v040-07-objects-in-this-document"));
        check(row && row->isVisible(), QStringLiteral("\"A Scene in Ashvale\": the Objects tab lists the objects in this document"));
        BinderItem *chapter = find(p, QStringLiteral("A Chapter with Comments"));
        binder->select({chapter}, true);
        settle(500);
        check(menuText(byKey(&w, QStringLiteral("viewText"))->text()) == QLatin1String("Combined Text")
                  && byKey(&w, QStringLiteral("viewText"))->iconText() == QLatin1String("Combined Text"),
              QStringLiteral("with a folder, the first view button and View menu entry are called \"Combined Text\""),
              byKey(&w, QStringLiteral("viewText"))->text());
        insp->showTab(Inspector::Comments);
        settle(700);
        QStringList heads;
        for (QLabel *l : insp->findChildren<QLabel *>(QStringLiteral("scopeHeader")))
            if (l->isVisible())
                heads << l->text();
        EditorPane *pane = visiblePane(w);
        shot(&w, QStringLiteral("v040-08-comments-of-a-folder"));
        check(pane && pane->mode() == ViewMode::Corkboard && heads.size() == 2 && heads.join(QString()).contains(QLatin1String("The Ferry Book"))
                  && heads.join(QString()).contains(QLatin1String("Evening")),
              QStringLiteral("\"A Chapter with Comments\": a corkboard, and the inspector lists the notes of both documents"),
              heads.join(QLatin1String(" | ")));

        // the menu places the welcome texts name
        QString allText;
        for (BinderItem *it : p->allItems())
            if (it->hasText())
                allText += md::write(p->docs()->text(it->id), p->formatting) + QLatin1Char('\n');
        int n = 0;
        const QStringList missing = missingMenuPlaces(&w, allText, &n);
        check(n >= 25 && missing.isEmpty(), QStringLiteral("every menu place the welcome project names is in the menus (%1)").arg(n),
              missing.join(QLatin1String("; ")));

        // ---- the picture of an index card shows on the corkboard (from the old tutorial's suite)
        {
            QImage img(240, 160, QImage::Format_RGB32);
            img.fill(QColor(200, 30, 140));
            QDir().mkpath(p->contentDir(evening->id));
            img.save(p->contentDir(evening->id) + QLatin1String("/card.png"));
            evening->synopsisImage = QStringLiteral("card.png");
            p->notifyItemChanged(evening, false);
            binder->select({chapter}, true);
            settle(300);
            pane = visiblePane(w);
            pane->setViewMode(ViewMode::Corkboard);
            settle(500);
            const QImage grab = pane->corkboard()->viewport()->grab().toImage();
            int magenta = 0;
            for (int y = 0; y < grab.height(); y += 2)
                for (int x = 0; x < grab.width(); x += 2) {
                    const QColor c = grab.pixelColor(x, y);
                    if (c.red() > 170 && c.green() < 70 && c.blue() > 110 && c.blue() < 170)
                        ++magenta;
                }
            check(magenta > 300, QStringLiteral("the picture of an index card shows on the corkboard as well"), QString::number(magenta));
        }

        // ---- the color of an inline annotation is its own and is kept (from the old tutorial's suite)
        {
            auto annotation = [](QTextDocument *d) -> QTextCharFormat {
                for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                    for (auto f = b.begin(); !f.atEnd(); ++f)
                        if (NoteKind(f.fragment().charFormat().intProperty(props::NoteKind)) == NoteKind::InlineAnnotation)
                            return f.fragment().charFormat();
                return QTextCharFormat();
            };
            auto annotationStart = [](QTextDocument *d) {
                for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                    for (auto f = b.begin(); !f.atEnd(); ++f)
                        if (NoteKind(f.fragment().charFormat().intProperty(props::NoteKind)) == NoteKind::InlineAnnotation)
                            return f.fragment().position();
                return -1;
            };
            binder->select({evening}, true);
            settle(400);
            pane = visiblePane(w);
            TextEditor *ed = pane ? pane->currentEditor() : nullptr;
            const QColor green(20, 140, 60);
            QColor shown, kept;
            if (ed && annotationStart(ed->document()) >= 0) {
                QTextCursor c(ed->document());
                c.setPosition(annotationStart(ed->document()) + 3);
                ed->setTextCursor(c);
                byKey(&w, QStringLiteral("selectAnnotation"))->trigger();
                settle(100);
                ed->setTextColor(green);
                settle(200);
                shown = annotation(ed->document()).foreground().color();
                QTextDocument back;
                md::read(md::write(ed->document(), p->formatting), &back, p->formatting);
                kept = annotation(&back).property(props::NoteColor).value<QColor>();
                ed->document()->undo();
            }
            check(shown == green && kept == green, QStringLiteral("a text color given to an inline annotation is its color, also after saving"),
                  shown.name() + QLatin1Char(' ') + kept.name());
        }
    }

    // ============================================================ Help > List of All Placeholders
    {
        int rows = 0;
        bool pagesRow = false;
        QString copied;
        withModal<QDialog>(500, [&](QDialog *d) {
            auto *table = d->findChild<QTableWidget *>(QStringLiteral("placeholdersTable"));
            if (table) {
                rows = table->rowCount();
                for (int r = 0; r < rows; ++r)
                    pagesRow = pagesRow || table->item(r, 0)->text() == QLatin1String("<$pages>");
                table->setFocus();
                table->selectRow(0);
                QApplication::clipboard()->clear();
                QTest::keySequence(table, QKeySequence::Copy);
                settle(100);
                copied = QApplication::clipboard()->text();
            }
            d->reject();
        });
        byKey(&w, QStringLiteral("placeholders"))->trigger();
        settle(300);
        check(rows >= 20 && pagesRow, QStringLiteral("Help > List of All Placeholders lists them (%1 rows, <$pages> among them)").arg(rows));
        check(copied.startsWith(QLatin1String("<$n>\t")), QStringLiteral("... Ctrl+C copies the selected rows"), copied);
    }

    // ============================================================ the spell checker knows the program's words
    {
        SpellChecker &sc = SpellChecker::instance();
        if (sc.setLanguage(QStringLiteral("en_US"))) {
            QStringList wrong;
            for (const QString &word : {appName, QStringLiteral("outliner"), QStringLiteral("Outliner"), QStringLiteral("subdocuments"),
                                        QStringLiteral("copyholder"), appName.toUpper()})
                if (!sc.isCorrect(word))
                    wrong << word;
            check(wrong.isEmpty() && !sc.isCorrect(QStringLiteral("Quilbinder")) && !SpellChecker::programWords().contains(QStringLiteral("Scrivenings")),
                  QStringLiteral("the spell checker takes the program's name and its terms as correct (a misspelling stays one)"),
                  wrong.join(QLatin1String(", ")));
        } else {
            info(QStringLiteral("(no en_US dictionary: the spelling check of the program's words is skipped)"));
        }
    }

    // ============================================================ Help > Welcome Project again: open, fresh copy
    if (p) {
        const QString folder = p->folderPath();
        BinderItem *start = find(p, QStringLiteral("Welcome"));
        start->title = QStringLiteral("Changed by the reader");
        p->notifyItemChanged(start);
        p->save();
        w.closeProject();
        settle(300);
        QString question;
        answer(500, QStringLiteral("Open the Welcome Project"), &question);
        byKey(&w, QStringLiteral("welcome"))->trigger();
        settle(1200);
        p = w.project();
        check(p && find(p, QStringLiteral("Changed by the reader")) && question.contains(QDir::toNativeSeparators(folder)),
              QStringLiteral("Help > Welcome Project again: it says where the project is and opens it as it was left"), question);
        answer(500, QStringLiteral("Make a Fresh Copy"));
        byKey(&w, QStringLiteral("welcome"))->trigger();
        settle(1500);
        p = w.project();
        const bool fresh = p && find(p, QStringLiteral("Welcome")) && !find(p, QStringLiteral("Changed by the reader"));
        bool trashed = false;
        QDirIterator t(home.path() + QLatin1String("/data/Trash/files"), QDir::Dirs | QDir::NoDotAndDotDot);
        while (t.hasNext())
            trashed = trashed || t.next().contains(QStringLiteral("Welcome to"));
        check(fresh && trashed && QFileInfo(p->folderPath()).absoluteFilePath() == QFileInfo(folder).absoluteFilePath()
                  && !QFileInfo::exists(QDir::currentPath() + QLatin1String("/files")),
              QStringLiteral("... \"Make a Fresh Copy\" puts the old one into the trash and starts again in the same place"),
              QStringLiteral("fresh %1, trashed %2").arg(fresh).arg(trashed));
    }

    // ============================================================ a picture shown, the project closed and opened again
    // A picture view kept the document of the project closed before (0.21.1 to 0.30): the next save of the
    // window's places wrote through the freed document ("corrupted double-linked list" on closing a project,
    // now and then; found with AddressSanitizer in qb_v0211_tests).
    {
        QTemporaryDir tmp;
        const QJsonObject o = QJsonDocument::fromJson(R"json({"draft": [{"title": "Text", "text": "Hello."}],
            "research": [{"title": "Tower", "type": "image", "file": "tower.png"}]})json").object();
        outline::Result r = outline::build(o, QStringLiteral(QB_TEST_SOURCE_DIR "/import"), tmp.path(), QStringLiteral("Pictures"));
        const QString file = r.project ? r.project->indexPath() : QString();
        QStringList stale;
        Project *pp = nullptr;
        if (r.project) {
            w.adoptProject(std::move(r.project));
            settle(400);
            pp = w.project();
            auto *binder = w.findChild<BinderPanel *>();
            binder->select({find(pp, QStringLiteral("Tower"))}, true);
            settle(500);
            binder->select({find(pp, QStringLiteral("Text"))}, true);
            settle(300);
            w.closeProject();
            settle(300);
            w.openProject(file);
            settle(800);
            pp = w.project();
            for (MediaView *mv : w.findChildren<MediaView *>())
                if (mv->item() && !(pp && pp->allItems().contains(mv->item())))
                    stale << QStringLiteral("a picture view of %1").arg(mv->parentWidget() ? QString::fromLatin1(mv->parentWidget()->metaObject()->className()) : QString());
        }
        check(pp && stale.isEmpty(), QStringLiteral("a picture shown, the project closed and opened again: no picture view keeps a document of the closed project"),
              stale.join(QLatin1String(", ")));
        w.closeProject();
        settle(300);
    }

    // ============================================================ the outline builder refuses broken outlines
    {
        QTemporaryDir tmp;
        QJsonObject o = QJsonDocument::fromJson(R"json({"draft": [{"title": "A", "text": "[x](qb-title:Nowhere)"}],
            "objects": [{"category": "characters", "name": "B", "values": {"livesIn": ["Nobody"]}}]})json").object();
        outline::Result r = outline::build(o, tmp.path(), tmp.path(), QStringLiteral("Broken"));
        check(!r.project && r.errors.size() == 2 && !QFileInfo::exists(tmp.path() + QStringLiteral("/Broken") + QLatin1String(QB_PROJECT_SUFFIX)),
              QStringLiteral("an outline that names a missing document or object makes no project, and says what is missing"),
              r.errors.join(QLatin1String("; ")));
    }

    w.closeProject();
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
