// Scripted run for version 0.70.0 beta, the fixes of the two external audits of 2026-11-10 (report A, 31 faults;
// report B, 14 items) and the writer's decisions of that evening:
//  - text read and written exactly: bold inside italics in Markdown (A F-01), the RTF reader's lists (B F-1),
//    damaged RTF and text in another encoding shown read-only (A F-05, F-04), control characters told (B F-4);
//  - failed saves cannot be missed (A S-01, B U-2, S-1), mind maps report and keep their changes (A U-01, U-02);
//  - backups per project, restored by the program, made beside the window (A F-02, U-05, U-06, U-09, B F-3);
//  - pictures carried along by Merge and Split (A U-03);
//  - questions with a safe default (A F-03, B S-3), project settings only safer (A U-04), 30 s latest save
//    (A S-09), Options for all projects (A S-05), the project file chosen in a folder with several (B F-2);
//  - damaged and hostile files: Word 97 counts, binder depth, duplicate ids, foreign XML, counts, boards
//    (A U-11, F-08, F-07, F-06, B L-1), reading speed (A F-10);
//  - build and release: test switches off by default (A S-04), signing apart from building and the test gate in
//    the release workflow (A S-03, S-02), the update's signature checked before every start (A U-07);
//  - the German interface, the dark page, the font self-test (A S-06, S-07, S-08).
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0700_tests /tmp/shots
#include "core/Backup.h"
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Launcher.h"
#include "core/MindMap.h"
#include "core/Project.h"
#include "core/SettingsLayer.h"
#include "core/Addons.h"
#include "core/Signing.h"
#include "core/Zip.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/ScriptIO.h"
#include "text/TextProps.h"
#include "ui/Ask.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/TextEditor.h"
#include "ui/Language.h"
#include "ui/MainWindow.h"
#include "ui/MindMapDoc.h"
#include "ui/OptionsDialog.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTimer>
#include <QTranslator>

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

static bool waitFor(const std::function<bool()> &f, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (f())
            return true;
        settle(50);
    }
    return f();
}

static void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QByteArray readAll(const QString &f)
{
    QFile file(f);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static void writeAll(const QString &f, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile file(f);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

static QString run(const QString &program, const QStringList &args, int *rc = nullptr, int timeoutMs = 120000,
                   const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment())
{
    QProcess p;
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished();
        if (rc)
            *rc = -2;
        return QString::fromLocal8Bit(p.readAll());
    }
    if (rc)
        *rc = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return QString::fromLocal8Bit(p.readAll());
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

// a folder replaced by a plain file of the same name (the session's root ignores permissions)
struct Block {
    QString path, away;
    explicit Block(const QString &dir) : path(dir), away(dir + QStringLiteral(".away"))
    {
        if (QFileInfo(path).isDir())
            QDir().rename(path, away);
        else
            away.clear();
        writeAll(path, "blocked");
    }
    ~Block() { undo(); }
    void undo()
    {
        QFile::remove(path);
        if (!away.isEmpty())
            QDir().rename(away, path);
        away.clear();
    }
};

static EditorPane *firstPane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

static void answerLater(int ms, std::function<void(QMessageBox *)> fn)
{
    QTimer::singleShot(ms, qApp, [fn] {
        if (auto *b = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            fn(b);
        else if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

// ------------------------------------------------------------------ text read and written exactly

// what a document is, character by character: the letter and its bold / italic / strike / underline
static QString signature(const QTextDocument *d)
{
    QString out;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const QTextCharFormat cf = f.charFormat();
            const QString flags = QString(cf.fontWeight() >= QFont::Bold ? QLatin1Char('B') : QLatin1Char('-'))
                                  + (cf.fontItalic() ? QLatin1Char('I') : QLatin1Char('-'))
                                  + (cf.fontStrikeOut() ? QLatin1Char('S') : QLatin1Char('-'))
                                  + (cf.fontUnderline() ? QLatin1Char('U') : QLatin1Char('-'));
            for (const QChar c : f.text())
                out += c.isSpace() ? QStringLiteral(" ") : QString(c) + QLatin1Char('[') + flags + QLatin1Char(']');
        }
        out += QLatin1Char('\n');
    }
    return out;
}

static void markdownRoundTrips()
{
    std::printf("== Markdown: touching formats (audit A F-01)\n");
    const Formatting f;
    // the three paragraphs of the report, as 0.65.0 wrote them: read back with their formats
    {
        QTextDocument d;
        md::read(QStringLiteral("***Note****: the rest of this sentence is in italics.*"), &d, f);
        const QString sig = signature(&d);
        check(d.toPlainText() == QLatin1String("Note: the rest of this sentence is in italics.") && sig.startsWith(QLatin1String("N[BI--]"))
                  && sig.contains(QLatin1String(":[-I--]")),
              QStringLiteral("a file of 0.65.0: \"***Note****: ...*\" reads as bold italic Note and italics, without asterisks"),
              d.toPlainText() + QLatin1String(" / ") + sig.left(60));
        QTextDocument e;
        md::read(QStringLiteral("*She read the letter to the very end**!***"), &e, f);
        check(e.toPlainText() == QLatin1String("She read the letter to the very end!") && signature(&e).contains(QLatin1String("![BI--]")),
              QStringLiteral("a file of 0.65.0: \"*... end**!***\" reads as italics with a bold \"!\""), e.toPlainText());
        QTextDocument g;
        md::read(QStringLiteral("Not ***un****believable* at all."), &g, f);
        const QString gs = signature(&g);
        check(g.toPlainText() == QLatin1String("Not unbelievable at all.") && gs.contains(QLatin1String("u[BI--]"))
                  && gs.contains(QLatin1String("b[-I--]")) && gs.contains(QLatin1String("a[----]")),
              QStringLiteral("a file of 0.65.0: \"Not ***un****believable* at all.\" reads right"), g.toPlainText());
    }
    // a property: random sentences with bold, italic, strike and underline on 1 to 3 ranges anywhere, written and
    // read back: the same letters with the same formats (report A: 12 % changed)
    {
        QRandomGenerator rng(70);
        const QStringList words = {QStringLiteral("She"), QStringLiteral("read"), QStringLiteral("the"), QStringLiteral("letter"),
                                   QStringLiteral("to"), QStringLiteral("very"), QStringLiteral("end"), QStringLiteral("and"),
                                   QStringLiteral("then"), QStringLiteral("laughed"), QStringLiteral("quietly"), QStringLiteral("again")};
        int changed = 0;
        QString firstBad;
        const int rounds = 4000;
        for (int r = 0; r < rounds; ++r) {
            QString sentence;
            const int n = 4 + int(rng.bounded(8));
            for (int i = 0; i < n; ++i)
                sentence += (i ? QStringLiteral(" ") : QString()) + words.at(int(rng.bounded(words.size())));
            if (rng.bounded(3) == 0)
                sentence += QStringLiteral("!");
            QTextDocument d;
            QTextCursor c(&d);
            c.insertText(sentence);
            const int ranges = 1 + int(rng.bounded(3));
            for (int k = 0; k < ranges; ++k) {
                int a = int(rng.bounded(sentence.size())), b = int(rng.bounded(sentence.size()));
                if (a > b)
                    std::swap(a, b);
                if (a == b)
                    ++b;
                QTextCursor s(&d);
                s.setPosition(a);
                s.setPosition(qMin(b, int(sentence.size())), QTextCursor::KeepAnchor);
                QTextCharFormat cf;
                switch (rng.bounded(4)) {
                case 0: cf.setFontWeight(QFont::Bold); break;
                case 1: cf.setFontItalic(true); break;
                case 2: cf.setFontStrikeOut(true); break;
                default: cf.setFontUnderline(true); break;
                }
                s.mergeCharFormat(cf);
            }
            const QString written = md::write(&d, f);
            QTextDocument back;
            md::read(written, &back, f);
            if (signature(&back) != signature(&d)) {
                ++changed;
                if (firstBad.isEmpty())
                    firstBad = written;
                if (changed <= 4 && qEnvironmentVariableIsSet("QB_V0700_ONLY"))
                    info(QStringLiteral("written: %1\n      wanted: %2\n      read:   %3").arg(written, signature(&d).simplified(), signature(&back).simplified()));
            }
        }
        check(changed == 0, QStringLiteral("Markdown: %1 random sentences with touching and overlapping formats come back unchanged").arg(rounds),
              QStringLiteral("%1 changed; first: %2").arg(changed).arg(firstBad));
    }
}

static void rtfReader()
{
    std::printf("== RTF reader: lists and tables of a damaged file (audit B F-1)\n");
    const Formatting f;
    {
        QTextDocument d;
        rtf::read(QByteArray("{\\rtf{\\qblist 0 1}{\\par\\pard{\\cell}}"), &d, f);
        check(true, QStringLiteral("the 36-byte file of report B is read without a crash"));
    }
    {
        QTextDocument d;
        rtf::read(QByteArray("{\\rtf1{\\*\\qblist 2147483647 1}A\\par{\\*\\qblist 2147483647 9}B\\par\\ilvl2147483647 C\\par}"), &d, f);
        check(d.toPlainText().contains(QLatin1String("A")) && d.toPlainText().contains(QLatin1String("C")),
              QStringLiteral("list levels and types near INT_MAX are bounded"), d.toPlainText());
    }
    // a small storm of broken list and table markup
    {
        const QList<QByteArray> parts = {"{\\*\\qblist 0 1}", "{\\*\\qblist 1 2}", "\\par", "\\pard", "\\cell", "\\row", "\\trowd",
                                         "\\intbl", "{", "}", "text ", "\\ls1", "\\ilvl1", "{\\*\\qbtable {}}", "\\li720"};
        QRandomGenerator rng(1);
        QElapsedTimer t;
        t.start();
        int done = 0;
        for (int r = 0; r < 3000; ++r) {
            QByteArray data("{\\rtf1 ");
            const int n = 4 + int(rng.bounded(30));
            for (int i = 0; i < n; ++i)
                data += parts.at(int(rng.bounded(parts.size())));
            data += "}";
            QTextDocument d;
            rtf::read(data, &d, f);
            ++done;
        }
        check(done == 3000, QStringLiteral("3,000 files of broken list and table markup are read without a crash (%1 ms)").arg(t.elapsed()));
    }
    // the structure check and the mending (audit A F-05)
    {
        const QByteArray good = "{\\rtf1\\ansi{\\fonttbl{\\f0 Serif;}}\\f0 The whole text is here.\\par}";
        QString why;
        check(rtf::wellFormed(good, &why), QStringLiteral("a well-formed RTF file counts as such"), why);
        QByteArray zeroed = good;
        for (int i = 0; i < 16; ++i)
            zeroed[i] = '\0';
        QByteArray noBrace = good.mid(1);
        QByteArray stray = good;
        stray.insert(stray.indexOf("The whole"), "}}}");
        bool allDamaged = true, allMended = true;
        for (const QByteArray &bad : {zeroed, noBrace, stray, good.left(good.size() - 1)}) {
            allDamaged = allDamaged && !rtf::wellFormed(bad);
            QTextDocument d;
            rtf::read(rtf::mended(bad), &d, Formatting());
            allMended = allMended && d.toPlainText().contains(QLatin1String("whole text is here"));
        }
        check(allDamaged, QStringLiteral("a zeroed start, a missing first brace, stray closing braces and a cut-off end count as damaged"));
        check(allMended, QStringLiteral("the mended copy still shows the text of each"));
    }
}

static void damagedFiles(const QString &W)
{
    std::printf("== Damaged files and other encodings are shown read-only, never written (audit A F-05, F-04)\n");
    QString err;
    // RTF storage: three kinds of damage
    {
        auto p = Project::create(W, QStringLiteral("Damaged RTF"), &err);
        p->storage = StorageFormat::Rtf;
        BinderItem *a0 = textDoc(p.get(), p->draft(), QStringLiteral("A"), QStringLiteral("The whole text of A is still here."));
        p->save(&err);
        const QString aId = a0->id;
        const QString file = p->contentDir(aId) + QStringLiteral("/text.rtf");
        const QString folder = p->folderPath();
        p.reset();
        const QByteArray good = readAll(file);
        QByteArray zeroed = good;
        for (int i = 0; i < 16 && i < zeroed.size(); ++i)
            zeroed[i] = '\0';
        QByteArray stray = good;
        stray.insert(stray.indexOf("The whole"), "}}}");
        int ok = 0;
        QStringList detail;
        for (const QByteArray &bad : {zeroed, good.mid(1), stray}) {
            writeAll(file, bad);
            auto q = Project::open(folder, &err);
            BinderItem *qa = q ? q->item(aId) : nullptr;
            bool told = false;
            if (q)
                QObject::connect(q->docs(), &DocumentStore::damagedFile, [&told](const QString &, bool, const QString &, const QString &) { told = true; });
            QTextDocument *d = qa ? q->docs()->text(qa->id) : nullptr;
            const bool shown = d && d->toPlainText().contains(QLatin1String("still here"));
            if (d) {
                QTextCursor c(d);
                c.movePosition(QTextCursor::End);
                c.insertText(QStringLiteral(" more"));
            }
            if (q)
                q->save(&err);
            const bool same = readAll(file) == bad;
            const bool flagged = q && q->docs()->isDamaged(aId) && q->docs()->isUnreadable(aId);
            ok += (shown && same && flagged && told) ? 1 : 0;
            detail << QStringLiteral("shown %1 same %2 flagged %3 told %4 %5").arg(shown).arg(same).arg(flagged).arg(told).arg(q ? QString() : err);
        }
        check(ok == 3, QStringLiteral("RTF storage: a damaged text file is shown as far as it can be read, read-only, told, and never written over"),
              detail.join(QLatin1String("; ")));
    }
    // Markdown storage: Windows-1252 and UTF-16 written by another editor
    {
        auto p = Project::create(W, QStringLiteral("Other Encoding"), &err);
        BinderItem *a0 = textDoc(p.get(), p->draft(), QStringLiteral("A"), QStringLiteral("x"));
        p->save(&err);
        const QString aId = a0->id;
        const QString file = p->contentDir(aId) + QStringLiteral("/text.md");
        const QString folder = p->folderPath();
        p.reset();
        const QString umlauts = QStringLiteral("Grüße aus Köln, sagte Jörg mit „Anführung“.");
        QByteArray latin;
        for (const QChar c : umlauts)
            latin += c.unicode() == 0x201E ? char(0x84) : c.unicode() == 0x201C ? char(0x93) : char(c.unicode() & 0xFF);
        QByteArray utf16("\xFF\xFE", 2);
        for (const QChar c : umlauts) {
            utf16 += char(c.unicode() & 0xFF);
            utf16 += char(c.unicode() >> 8);
        }
        int ok = 0;
        QStringList detail;
        for (const QByteArray &bad : {latin, utf16}) {
            writeAll(file, bad);
            auto q = Project::open(folder, &err);
            QTextDocument *d = q ? q->docs()->text(aId) : nullptr;
            const QString shownText = d ? d->toPlainText() : QString();
            if (d) {
                QTextCursor c(d);
                c.insertText(QStringLiteral("typed "));
            }
            if (q)
                q->save(&err);
            const bool same = readAll(file) == bad;
            const bool rightLetters = shownText.contains(QStringLiteral("Grüße")) && shownText.contains(QStringLiteral("Köln"));
            ok += (q && q->docs()->isDamaged(aId) && same && rightLetters) ? 1 : 0;
            detail << shownText.left(40) + QStringLiteral(" same %1 damaged %2 letters %3 %4").arg(same).arg(q && q->docs()->isDamaged(aId)).arg(rightLetters).arg(q ? QString() : err);
        }
        check(ok == 2, QStringLiteral("a text file saved by another editor in Windows-1252 or UTF-16 is shown with its letters, read-only, never written over"),
              detail.join(QLatin1String("; ")));
    }
    // control characters: told once (audit B F-4)
    {
        auto p = Project::create(W, QStringLiteral("Control"), &err);
        BinderItem *a = textDoc(p.get(), p->draft(), QStringLiteral("A"), QStringLiteral("ctrl") + QChar(1) + QStringLiteral("char"));
        int told = 0;
        QObject::connect(p->docs(), &DocumentStore::charactersLeftOut, [&told](const QString &, bool, int n) { told += n; });
        p->save(&err);
        QTextCursor c(p->docs()->text(a->id));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("x") + QChar(2));
        p->save(&err);
        check(told == 1, QStringLiteral("control characters that cannot be stored are told once per document (audit B F-4)"),
              QString::number(told));
    }
}

// ------------------------------------------------------------------ failed saves

static void failedSaves(const QString &W)
{
    std::printf("== A failed save cannot be missed; mind maps keep and report theirs (audit A S-01, U-01, U-02; B U-2, S-1, U-1)\n");
    QString err;
    auto made = Project::create(W, QStringLiteral("Failing"), &err);
    BinderItem *a = textDoc(made.get(), made->draft(), QStringLiteral("Chapter"), QStringLiteral("Typed text."));
    BinderItem *board = textDoc(made.get(), made->draft(), QStringLiteral("Plot"), QStringLiteral("x"));
    made->save(&err);
    const QString folder = made->folderPath();
    const QString aId = a->id, boardId = board->id;
    made.reset();

    MainWindow w;
    w.resize(1400, 900);
    w.show();
    settle(300);
    w.openProject(folder);
    settle(500);
    Project *p = w.project();
    waitFor([&] { return w.saveStateText() == QLatin1String("Saved"); }, 8000);
    check(p && w.saveStateText() == QLatin1String("Saved"), QStringLiteral("the status bar says \"Saved\" after opening"), w.saveStateText());
    // a failing save while typing: the text's folder cannot be written
    {
        Block block(p->contentDir(aId));
        QTextCursor c(p->docs()->text(aId));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" More text."));
        waitFor([&] { return w.saveFailedSince().isValid(); }, 6000);
        settle(300);
        auto *notice = w.findChild<QMessageBox *>(QStringLiteral("saveFailedNotice"));
        const bool modal = QApplication::activeModalWidget() != nullptr;
        check(w.saveFailedSince().isValid() && w.saveStateText().startsWith(QLatin1String("Not saved")) && w.isWindowModified(),
              QStringLiteral("a failed save: the status bar says \"Not saved ...\" and the title is marked, for as long as it fails"),
              w.saveStateText());
        check(notice && notice->isVisible() && !modal && notice->text().contains(QLatin1String("Chapter")),
              QStringLiteral("the notice names the document, is not modal and does not take the keyboard"),
              notice ? notice->text() : QStringLiteral("no notice"));
        shot(&w, QStringLiteral("v0700-save-failed"));
        if (notice)
            shot(notice, QStringLiteral("v0700-save-failed-notice"));
        // the writer's next key goes into the text, not to the notice
        if (notice) {
            QWidget *fw = QApplication::focusWidget();
            check(fw != notice && !notice->isAncestorOf(fw), QStringLiteral("the keyboard stays where the writer types"));
        }
        block.undo();
    }
    // the next attempt works: the sign is gone
    waitFor([&] { return !w.saveFailedSince().isValid(); }, 25000);
    check(!w.saveFailedSince().isValid() && w.saveStateText() == QLatin1String("Saved") && !w.isWindowModified()
              && readAll(p->contentDir(aId) + QStringLiteral("/text.md")).contains("More text"),
          QStringLiteral("the program tries again by itself; once a save works, the sign is gone and the text is on the disk"),
          w.saveStateText());

    // a mind map that cannot be saved (audit A U-01)
    {
        MindMapDoc *d = mindmaps::docFor(p, boardId);
        d->change(QStringLiteral("Add"), [d] {
            mindmap::Note n;
            n.id = 1;
            n.text = QStringLiteral("First note");
            d->board.notes << n;
        });
        check(d->saveOrTell(), QStringLiteral("a board saves"));
        const QString boardFile = d->path();
        QString told;
        auto conn = QObject::connect(p, &Project::saveFailed, [&told](const QString &e) { told = e; });
        Block block(p->contentDir(boardId));
        d->change(QStringLiteral("Add"), [d] {
            mindmap::Note n;
            n.id = 2;
            n.text = QStringLiteral("Second note");
            d->board.notes << n;
        });
        settle(1800);
        QObject::disconnect(conn);
        check(!told.isEmpty() && p->hasUnsavedChanges() && p->unsavedTitles().join(QLatin1Char(' ')).contains(QLatin1String("Plot")),
              QStringLiteral("a board whose save fails is reported as a failed save and counted as not saved"), told);
        const QString emergency = p->writeEmergencyCopy(W + QStringLiteral("/emergency"), &err);
        bool boardThere = false;
        QDirIterator it(emergency, QDir::Files);
        while (it.hasNext())
            boardThere = boardThere || readAll(it.next()).contains("Second note");
        check(boardThere, QStringLiteral("the board not saved goes into the emergency copy"), emergency);
        block.undo();
        Q_UNUSED(boardFile);
    }
    w.closeProject();
    settle(300);

    // a damaged board file: read-only, never written (audit A U-02)
    {
        auto q = Project::open(folder, &err);
        const QString boardFile = mindmap::pathFor(q->contentDir(boardId));
        const QByteArray full = readAll(boardFile);
        const QByteArray cut = full.left(full.size() * 2 / 3);
        writeAll(boardFile, cut);
        MindMapDoc *d = mindmaps::docFor(q.get(), boardId);
        d->change(QStringLiteral("Add"), [d] {
            mindmap::Note n;
            n.id = 9;
            n.text = QStringLiteral("New");
            d->board.notes << n;
        });
        d->save(&err);
        check(d->isUnreadable() && d->locked() && readAll(boardFile) == cut,
              QStringLiteral("a board file that cannot be read is read-only and never written over"), d->loadError());
        mindmaps::closeAll(q.get(), true);
    }
}

// ------------------------------------------------------------------ backups

static void backups(const QString &W)
{
    std::printf("== Backups per project, restored by the program, made beside the window (audit A F-02, U-05, U-06, U-09; B F-3)\n");
    QString err, out;
    const QString bdir = W + QStringLiteral("/backups");
    QDir().mkpath(W + QStringLiteral("/one"));
    QDir().mkpath(W + QStringLiteral("/two"));
    auto a = Project::create(W + QStringLiteral("/one"), QStringLiteral("My Novel"), &err);
    auto b = Project::create(W + QStringLiteral("/two"), QStringLiteral("My Novel"), &err);
    textDoc(a.get(), a->draft(), QStringLiteral("A"), QStringLiteral("Text of A."));
    textDoc(b.get(), b->draft(), QStringLiteral("B"), QStringLiteral("Text of B."));
    // a backup of the time before 0.70.0, named by the title only
    writeAll(bdir + QStringLiteral("/My Novel bak 2026-11-01 10-00-00.zip"), "old");
    for (int i = 0; i < 3; ++i) {
        backup::backupProject(a.get(), bdir, 2, &out, &err);
        QTest::qWait(1100);
    }
    for (int i = 0; i < 3; ++i) {
        backup::backupProject(b.get(), bdir, 2, &out, &err);
        QTest::qWait(1100);
    }
    const QStringList ofA = backup::backupsFor(a.get(), bdir), ofB = backup::backupsFor(b.get(), bdir);
    check(ofA.size() == 2 && ofB.size() == 2 && backup::projectFolder(a.get(), bdir) != backup::projectFolder(b.get(), bdir),
          QStringLiteral("two projects with the same title keep their backups apart; neither deletes the other's"),
          QStringLiteral("%1 / %2").arg(ofA.size()).arg(ofB.size()));
    check(QFile::exists(bdir + QStringLiteral("/My Novel bak 2026-11-01 10-00-00.zip")),
          QStringLiteral("a backup made before 0.70.0 is never deleted automatically"));
    // the lock file stays out (audit A U-09)
    {
        writeAll(a->settingsDir() + QStringLiteral("/lock"), "host 1\ndevice=box\nuser=writer\n");
        backup::backupProject(a.get(), bdir, 0, &out, &err);
        ZipReader zr(out);
        bool lock = false;
        for (const QString &n : zr.fileNames())
            lock = lock || n.endsWith(QLatin1String("settings/lock"));
        check(zr.isValid() && !lock, QStringLiteral("a backup does not hold settings/lock (the computer's and the login's name)"));
        QFile::remove(a->settingsDir() + QStringLiteral("/lock"));
    }
    // progress and Cancel (audit A U-06)
    {
        QByteArray noise(24 * 1024 * 1024, '\0');
        QRandomGenerator rng(3);
        for (int i = 0; i < noise.size(); i += 4) {
            const quint32 v = rng.generate();
            memcpy(noise.data() + i, &v, 4);
        }
        writeAll(a->folderPath() + QStringLiteral("/Files/noise.bin"), noise);
        int calls = 0;
        // (Cancel at the first progress call: the 24 MB file is packed in pieces of a megabyte, and Cancel is seen
        // within the file, wherever the file comes in the folder's order)
        backup::setProgressHandler([&calls](const QString &what, qint64, qint64, bool *stop) {
            if (!what.isEmpty()) {
                ++calls;
                if (calls == 1 && stop)
                    *stop = true;
            }
        });
        const int before = int(backup::backupsFor(a.get(), bdir).size());
        QString e2;
        const bool ok = backup::backupProject(a.get(), bdir, 0, &out, &e2);
        backup::setProgressHandler(nullptr);
        bool parts = false;
        for (const QString &f : QDir(backup::projectFolder(a.get(), bdir)).entryList({QStringLiteral("*.part")}, QDir::Files))
            parts = parts || !f.isEmpty();
        check(calls >= 1 && !ok && e2.contains(QLatin1String("cancelled")) && int(backup::backupsFor(a.get(), bdir).size()) == before && !parts,
              QStringLiteral("a backup runs beside the window with progress, and Cancel leaves no backup behind"),
              QStringLiteral("calls %1, %2").arg(calls).arg(e2));
        QFile::remove(a->folderPath() + QStringLiteral("/Files/noise.bin"));
    }
    a->save(&err);
    const QString aFolder = a->folderPath();
    const QString title = a->title();
    a.reset();
    b.reset();
    // Restore (audit A U-05)
    {
        AppSettings::instance().backupDir = bdir;
        MainWindow w;
        w.show();
        settle(200);
        w.openProject(aFolder);
        settle(400);
        const QStringList list = w.project() ? backup::backupsFor(w.project(), bdir) : QStringList();
        const bool ok = !list.isEmpty() && w.restoreBackupFile(list.last());
        settle(600);
        Project *now = w.project();
        check(ok && now && now->folderPath() != aFolder && now->folderPath().contains(QLatin1String("(restored"))
                  && now->title().contains(QLatin1String("(restored")),
              QStringLiteral("File > Back Up > Restore From Backup unpacks into a new folder beside the project, with a title "
                             "that says so, and opens it"),
              now ? now->folderPath() + QLatin1String(" / ") + now->title() : QStringLiteral("no project"));
        check(w.findChild<QAction *>(QStringLiteral("restoreBackup")) != nullptr, QStringLiteral("the command is in the menu"));
        w.closeProject();
        settle(200);
        Q_UNUSED(title);
    }
}

// ------------------------------------------------------------------ pictures in Merge and Split

static void picturesMove(const QString &W)
{
    std::printf("== Merge and Split keep the pictures (audit A U-03)\n");
    QString err;
    auto made = Project::create(W, QStringLiteral("Pictures"), &err);
    BinderItem *a = textDoc(made.get(), made->draft(), QStringLiteral("With Picture"), QStringLiteral("Before the picture. "));
    BinderItem *b = textDoc(made.get(), made->draft(), QStringLiteral("Plain"), QStringLiteral("Plain text."));
    BinderItem *c = textDoc(made.get(), made->draft(), QStringLiteral("To Split"), QStringLiteral("First part. Second part "));
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(QColor(200, 30, 30));
    for (BinderItem *it : {a, c}) {
        QTextDocument *d = made->docs()->text(it->id);
        const QString name = QStringLiteral("pasted-%1.png").arg(it->id.left(6));
        // as the editor's picture command adds it: under the name resolved against the document's folder too
        d->addResource(QTextDocument::ImageResource, QUrl(name), img);
        d->addResource(QTextDocument::ImageResource, d->baseUrl().resolved(QUrl(name)), img);
        QTextCursor cur(d);
        cur.movePosition(QTextCursor::End);
        QTextImageFormat f;
        f.setName(name);
        cur.insertImage(f);
        cur.insertText(QStringLiteral(" after."));
    }
    made->save(&err);
    const QString folder = made->folderPath();
    const QString aId = a->id, bId = b->id, cId = c->id;
    made.reset();
    MainWindow w;
    w.show();
    settle(200);
    w.openProject(folder);
    settle(400);
    Project *p = w.project();
    // the pictures the saved text names, and how many of them are files in its folder
    auto pictureFiles = [](Project *pr, const QString &id) {
        const QString md = QString::fromUtf8(readAll(pr->contentDir(id) + QStringLiteral("/text.md")));
        static const QRegularExpression re(QStringLiteral("!\\[[^\\]]*\\]\\(([^)\\s]+)\\)"));
        int named = 0, there = 0;
        for (auto it = re.globalMatch(md); it.hasNext();) {
            ++named;
            there += QFile::exists(QDir(pr->contentDir(id)).filePath(it.next().captured(1))) ? 1 : 0;
        }
        return qMakePair(named, there);
    };
    // Merge: With Picture + Plain, into Plain (the first is the target: select Plain first in the binder order)
    {
        QList<BinderItem *> sel{p->item(bId), p->item(aId)};
        w.findChild<BinderPanel *>()->select(sel, true);
        settle(200);
        answerLater(400, [](QMessageBox *m) {
            if (QAbstractButton *y = m->button(QMessageBox::Yes))
                y->click();
        });
        w.findChild<QAction *>(QStringLiteral("merge"))->trigger();
        settle(500);
        p->save(&err);
        const QString target = p->item(bId) && !p->isInTrash(p->item(bId)) ? bId : aId;
        const auto r = pictureFiles(p, target);
        p->model()->emptyTrash();
        settle(200);
        const auto r2 = pictureFiles(p, target);
        check(r.first == 1 && r.second == 1 && r2.second == 1,
              QStringLiteral("Merge Documents: the picture is saved into the merged document's folder and survives Empty Trash"),
              QStringLiteral("named %1, files %2, after Empty Trash %3: %4 | %5").arg(r.first).arg(r.second).arg(r2.second)
                  .arg(QString::fromUtf8(readAll(p->contentDir(target) + QStringLiteral("/text.md"))).left(300), err));
    }
    // Split at Selection: the picture goes into the new document
    {
        EditorPane *pane = firstPane(w);
        if (pane)
            pane->showItems({p->item(cId)}, false, int(ViewMode::Single));
        settle(400);
        TextEditor *e = pane ? pane->currentEditor() : nullptr;
        if (e)
            e->setFocus();
        if (e) {
            QTextCursor cur = e->textCursor();
            cur.setPosition(int(QStringLiteral("First part. ").size()));
            e->setTextCursor(cur);
        }
        w.findChild<QAction *>(QStringLiteral("split"))->trigger();
        settle(400);
        p->save(&err);
        BinderItem *orig = p->item(cId);
        BinderItem *next = orig && orig->parent && orig->row() + 1 < orig->parent->children.size() ? orig->parent->children.at(orig->row() + 1) : nullptr;
        const auto r = next ? pictureFiles(p, next->id) : qMakePair(0, 0);
        check(e && next && r.first == 1 && r.second == 1,
              QStringLiteral("Split at Selection: the picture is saved into the new document's folder"),
              QStringLiteral("named %1, files %2").arg(r.first).arg(r.second));
    }
    w.closeProject();
    settle(200);
}

// ------------------------------------------------------------------ dialogs, settings, project files

static void dialogsAndSettings(const QString &W, const QString &src)
{
    std::printf("== Questions, project settings, the latest save, Options, the project file (audit A F-03, U-04, S-09, S-05; B F-2)\n");
    QString err;
    // every yes-or-no question names its default (a source check)
    {
        QStringList without;
        for (const QString &root : {src + QStringLiteral("/src"), src + QStringLiteral("/plugins")}) {
            QDirIterator it(root, {QStringLiteral("*.cpp")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                const QString s = QString::fromUtf8(readAll(f));
                for (qsizetype i = s.indexOf(QLatin1String("QMessageBox::question(")); i >= 0; i = s.indexOf(QLatin1String("QMessageBox::question("), i + 1)) {
                    int depth = 0, commas = 0;
                    bool quote = false;
                    for (qsizetype k = i + 21; k < s.size(); ++k) {
                        const QChar ch = s.at(k);
                        if (ch == QLatin1Char('"') && s.at(k - 1) != QLatin1Char('\\'))
                            quote = !quote;
                        if (quote)
                            continue;
                        if (ch == QLatin1Char('('))
                            ++depth;
                        else if (ch == QLatin1Char(')') && --depth == 0)
                            break;
                        else if (ch == QLatin1Char(',') && depth == 1)
                            ++commas;
                    }
                    if (commas < 4)
                        without << QFileInfo(f).fileName();
                }
            }
        }
        check(without.isEmpty(), QStringLiteral("no question of the program leaves its default to Qt (Yes under KDE's Fusion)"),
              without.join(QLatin1String(", ")));
    }
    // Empty Trash: Enter answers Cancel
    {
        auto made = Project::create(W, QStringLiteral("Trash Question"), &err);
        BinderItem *t = textDoc(made.get(), made->draft(), QStringLiteral("Going"), QStringLiteral("Text."));
        made->model()->moveToTrash({t});
        made->save(&err);
        const QString folder = made->folderPath();
        const QString id = t->id;
        made.reset();
        MainWindow w;
        w.show();
        settle(200);
        w.openProject(folder);
        settle(300);
        QString defaultText;
        answerLater(400, [&defaultText](QMessageBox *m) {
            defaultText = m->defaultButton() ? m->defaultButton()->text() : QString();
            shot(m, QStringLiteral("v0700-empty-trash-question"));
            QTest::keyClick(m, Qt::Key_Return);
        });
        w.findChild<QAction *>(QStringLiteral("emptyTrash"))->trigger();
        settle(500);
        check(w.project() && w.project()->item(id) && QFileInfo::exists(w.project()->contentDir(id)) && defaultText.remove(QLatin1Char('&')) == QLatin1String("Cancel"),
              QStringLiteral("Empty Trash: Cancel is the default, Enter deletes nothing (audit A F-03, B S-3)"), defaultText);
        w.closeProject();
        settle(200);
    }
    // a project's own settings make saving only safer (audit A U-04)
    {
        auto made = Project::create(W, QStringLiteral("Settings From Elsewhere"), &err);
        made->save(&err);
        const QString folder = made->folderPath();
        writeAll(made->settingsDir() + QStringLiteral("/options.ini"),
                 "[app]\nautosave=86400\nautoQuit=true\nautoQuitMinutes=1\n[backup]\nonClose=false\nkeep=1\n");
        made.reset();
        MainWindow w;
        w.show();
        settle(200);
        w.openProject(folder);
        settle(300);
        const AppSettings &s = AppSettings::instance();
        check(s.autosaveSeconds <= 2 && s.backupOnClose && s.backupKeep >= 5 && !s.autoQuit,
              QStringLiteral("a project folder cannot switch off autosave, backups or set the automatic quit"),
              QStringLiteral("autosave %1, onClose %2, keep %3, autoQuit %4").arg(s.autosaveSeconds).arg(s.backupOnClose).arg(s.backupKeep).arg(s.autoQuit));
        // Options opens for all projects (audit A S-05)
        OptionsDialog dlg(&w);
        check(dlg.optionScope() == OptionsDialog::AllProjects, QStringLiteral("File > Options opens for all projects with a project open"));
        auto *lang = dlg.findChild<QComboBox *>(QStringLiteral("optLanguage"));
        check(lang && lang->count() == 2 && lang->findData(QStringLiteral("de")) >= 0, QStringLiteral("File > Options > General > Startup offers English and German"));
        dlg.reject();
        w.closeProject();
        settle(200);
    }
    // the latest save stays at 30 s whatever the delay (audit A S-09; here 3 s against a delay of 300 s)
    {
        auto p = Project::create(W, QStringLiteral("Late Save"), &err);
        BinderItem *t = textDoc(p.get(), p->draft(), QStringLiteral("T"), QStringLiteral("x"));
        p->save(&err);
        p->setAutosaveDelay(300000);
        p->setMaxSaveWait(3000);
        QTextCursor c(p->docs()->text(t->id));
        c.insertText(QStringLiteral("typed once "));
        settle(4000);
        check(!p->hasUnsavedChanges(), QStringLiteral("with a long autosave delay the latest save still comes in time (one change, then a pause)"));
    }
    // the project file of a folder with several (audit B F-2)
    {
        auto p = Project::create(W, QStringLiteral("My Novel"), &err);
        p->save(&err);
        const QString folder = p->folderPath();
        const QString own = p->indexPath();
        p.reset();
        QFile::copy(own, folder + QStringLiteral("/My Novel (conflicted copy 2026-11-10)") + QLatin1String(QB_PROJECT_FILE_SUFFIX));
        QFile::copy(own, folder + QStringLiteral("/My Novel (1)") + QLatin1String(QB_PROJECT_FILE_SUFFIX));
        const QString chosen = Project::resolveProjectFile(folder);
        auto q = Project::open(folder, &err);
        const bool named = QFileInfo(own).completeBaseName() == QDir(folder).dirName();
        check((named ? chosen == own : true) && q && q->openNotice.contains(QLatin1String("conflicted copy")),
              QStringLiteral("a folder with several project files opens the one named like it (or the newest) and tells about the others"),
              chosen + QLatin1String(" / ") + (q ? q->openNotice.left(200) : err));
    }
}

// ------------------------------------------------------------------ damaged and hostile files

static QByteArray projectFileWithDepth(int depth)
{
    QByteArray x = "<?xml version=\"1.0\"?>\n<QuillbinderProject version=\"1\" title=\"Deep\"><Binder>"
                   "<Item id=\"DRAFT\" type=\"Draft\"><Title>Draft</Title><Children>";
    for (int i = 0; i < depth; ++i)
        x += "<Item id=\"F" + QByteArray::number(i) + "\" type=\"Folder\"><Title>L" + QByteArray::number(i) + "</Title><Children>";
    for (int i = 0; i < depth; ++i)
        x += "</Children></Item>";
    x += "</Children></Item><Item id=\"RESEARCH\" type=\"Research\"><Title>Research</Title></Item>"
         "<Item id=\"TRASH\" type=\"Trash\"><Title>Trash</Title></Item></Binder></QuillbinderProject>\n";
    return x;
}

static void hostileFiles(const QString &W, const QString &src, const QString &buildDir)
{
    std::printf("== Damaged and hostile files (audit A F-08, F-07, F-06, U-11, F-10; B L-1)\n");
    QString err;
    // depth
    {
        const QString dir = W + QStringLiteral("/deep");
        writeAll(dir + QStringLiteral("/Deep.qbx"), projectFileWithDepth(3000));
        QElapsedTimer t;
        t.start();
        auto p = Project::open(dir, &err);
        int deepest = 0;
        if (p)
            for (BinderItem *it : p->allItems())
                deepest = qMax(deepest, it->level());
        const bool saved = p && p->save(&err);
        check(p && saved && deepest <= 202 && p->openNotice.contains(QLatin1String("moved up")),
              QStringLiteral("a binder nested 3,000 levels deep opens flattened to 200 levels, with a notice, and saves (%1 ms)").arg(t.elapsed()),
              QStringLiteral("deepest %1, %2").arg(deepest).arg(err));
        writeAll(dir + QStringLiteral("/Deep.qbx"), projectFileWithDepth(6000));
        t.restart();
        auto q = Project::open(dir, &err);
        check(!q && err.contains(QLatin1String("nested")) && t.elapsed() < 5000,
              QStringLiteral("nested 6,000 levels deep: refused as damaged, quickly (%1 ms)").arg(t.elapsed()), err);
    }
    // duplicate ids
    {
        auto p = Project::create(W, QStringLiteral("Twins"), &err);
        BinderItem *one = textDoc(p.get(), p->draft(), QStringLiteral("One"), QStringLiteral("The text of one."));
        textDoc(p.get(), p->draft(), QStringLiteral("Two"), QStringLiteral("x"));
        p->save(&err);
        const QString file = p->indexPath();
        const QString folder = p->folderPath();
        const QString oneId = one->id;
        BinderItem *two = p->draft()->children.at(1);
        const QString twoId = two->id;
        p.reset();
        QByteArray x = readAll(file);
        x.replace("id=\"" + twoId.toUtf8() + "\"", "id=\"" + oneId.toUtf8() + "\"");
        writeAll(file, x);
        auto q = Project::open(folder, &err);
        BinderItem *secondNow = q ? q->draft()->children.value(1) : nullptr;
        const QString secondId = secondNow ? secondNow->id : QString();
        if (q && secondNow) {
            q->model()->moveToTrash({secondNow});
            q->model()->emptyTrash();
        }
        check(q && !secondId.isEmpty() && secondId != oneId && q->openNotice.contains(QLatin1String("same id"))
                  && q->docs()->plainText(oneId).contains(QLatin1String("The text of one")),
              QStringLiteral("two items with one id: the second gets a new one, and emptying the Trash of it keeps the first's text"),
              q ? q->openNotice.left(200) : err);
    }
    // foreign XML
    {
        const QString dir = W + QStringLiteral("/foreign");
        const QByteArray other = "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\"><rect/></svg>";
        writeAll(dir + QStringLiteral("/X.qbx"), other);
        auto p = Project::open(dir, &err);
        check(!p && readAll(dir + QStringLiteral("/X.qbx")) == other && err.contains(QLatin1String("not a project file")),
              QStringLiteral("XML that is not a project file is refused and left as it is"), err);
    }
    // counts
    {
        auto p = Project::create(W, QStringLiteral("Counts"), &err);
        BinderItem *a = textDoc(p.get(), p->draft(), QStringLiteral("A"), QStringLiteral("x"));
        BinderItem *b = textDoc(p.get(), p->draft(), QStringLiteral("B"), QStringLiteral("x"));
        p->save(&err);
        const QString file = p->indexPath(), folder = p->folderPath();
        const QString ida = a->id, idb = b->id;
        p.reset();
        QByteArray x = readAll(file);
        x.replace(QByteArray("words=\""), QByteArray("words=\"2147483647\" x=\""));
        writeAll(file, x);
        auto q = Project::open(folder, &err);
        check(q && q->draftWordCount() >= 0 && q->draft()->totalWordCount() >= 0,
              QStringLiteral("word counts near INT_MAX in a project file add up without overflowing"),
              q ? QString::number(q->draftWordCount()) : err);
        Q_UNUSED(ida);
        Q_UNUSED(idb);
    }
    // boards
    {
        mindmap::Board b;
        const bool read = b.fromXml("<ScappleDocument><Notes><Note ID=\"2147483647\" Position=\"1e308,nan\" FontSize=\"1e9\" Width=\"1e30\">"
                                    "<String>a</String><ConnectedNoteIDs>-2147483648, 5</ConnectedNoteIDs></Note>"
                                    "<Note ID=\"-2147483648\" Position=\"0,0\"><String>b</String></Note></Notes></ScappleDocument>");
        bool sane = true;
        for (const mindmap::Note &n : b.notes)
            sane = sane && std::isfinite(n.pos.x()) && std::isfinite(n.pos.y()) && qAbs(n.pos.x()) <= 1e7 && n.fontSize <= 1000 && n.width <= 1e5
                   && n.id > -3000000 && n.id < 3000000;
        check(read && sane, QStringLiteral("a board's huge ids, places and sizes are bounded when it is read"));
    }
    // Word 97: a damaged character count (needs the dump tool of the importer add-on)
    {
        const QString dump = buildDir + QStringLiteral("/plugins/importers/qb_doc_dump");
        const QString sample = src + QStringLiteral("/tests/import/features.doc");
        if (QFile::exists(dump) && QFile::exists(sample)) {
            QByteArray d = readAll(sample);
            // the FIB is at the start of the WordDocument stream; the stream's place in a compound file is not fixed,
            // so every 32-bit value equal to the real character count is set to 0x7FFFFFF0 (crude, like the fuzzing)
            QProcess::execute(dump, {sample, QStringLiteral("--plain")});
            int changed = 0;
            QRandomGenerator rng(11);
            int crashes = 0, slow = 0;
            for (int round = 0; round < 60; ++round) {
                QByteArray m = d;
                for (int k = 0; k < 6; ++k) {
                    const int at = 512 + int(rng.bounded(qMax(1, int(m.size()) - 520)));
                    const quint32 v = rng.bounded(3) == 0 ? 0x7FFFFFF0u : rng.generate();
                    memcpy(m.data() + at, &v, 4);
                    ++changed;
                }
                const QString f = W + QStringLiteral("/mut.doc");
                writeAll(f, m);
                int rc = 0;
                QElapsedTimer t;
                t.start();
                run(dump, {f, QStringLiteral("--plain")}, &rc, 20000);
                if (rc == -1)
                    ++crashes;
                if (rc == -2 || t.elapsed() > 10000)
                    ++slow;
            }
            check(crashes == 0 && slow == 0, QStringLiteral("Word 97: 60 damaged copies of a sample are read without a crash or a stall"),
                  QStringLiteral("crashes %1, stalls %2").arg(crashes).arg(slow));
        } else {
            info(QStringLiteral("(Word 97: the dump tool or the sample is not there; left out)"));
        }
    }
    // speed (audit A F-10)
    {
        QString para;
        for (int i = 0; i < 4000; ++i)
            para += QStringLiteral("word **bold%1** ").arg(i);
        QElapsedTimer t;
        t.start();
        QTextDocument d;
        md::read(para, &d, Formatting());
        const qint64 mdMs = t.elapsed();
        QString fountain = QStringLiteral("INT. ROOM - DAY\n\nANNA\n");
        for (int i = 0; i < 8000; ++i)
            fountain += QStringLiteral("Line %1 of a very long speech.\n").arg(i);
        t.restart();
        const script::Screenplay sp = script::fromFountain(fountain);
        const qint64 fountainMs = t.elapsed();
        QByteArray rtf("{\\rtf1 ");
        for (int i = 0; i < 9900; ++i)
            rtf += "word {\\b bold} ";
        rtf += "}";
        t.restart();
        QTextDocument r;
        rtf::read(rtf, &r, Formatting());
        const qint64 rtfMs = t.elapsed();
        check(mdMs < 2000 && fountainMs < 2000 && rtfMs < 3000 && !sp.paras.isEmpty(),
              QStringLiteral("reading time: 4,000 bold spans in one paragraph %1 ms (was 7.5 s), a dialogue of 8,000 lines %2 ms (was 9.8 s)")
                  .arg(mdMs).arg(fountainMs));
        info(QStringLiteral("RTF with 9,900 bold spans in one paragraph: %1 ms").arg(rtfMs));
    }
}

// ------------------------------------------------------------------ build and release

static void buildAndRelease(const QString &W, const QString &src, const QString &buildDir)
{
    std::printf("== Build and release (audit A S-04, S-03, S-02, U-07)\n");
    const QString cmake = QString::fromUtf8(readAll(src + QStringLiteral("/CMakeLists.txt")));
    check(cmake.contains(QLatin1String("option(QB_TEST_SWITCHES \"Honor the test switches that load plugins from other folders or skip checks (test builds only)\" OFF)")),
          QStringLiteral("the test switches are off unless a build asks for them (a build as the README says behaves like a release)"));
    // the release workflow: the key only in the two signing jobs, the tests beside the release
    {
        const QString wf = QString::fromUtf8(readAll(src + QStringLiteral("/.github/workflows/release.yml")));
        const QStringList jobs = {QStringLiteral("version"), QStringLiteral("build"), QStringLiteral("tests"), QStringLiteral("sign-packages"),
                                  QStringLiteral("assemble"), QStringLiteral("sign-checksums"), QStringLiteral("publish")};
        QMap<QString, QString> body;
        for (int i = 0; i < jobs.size(); ++i) {
            const qsizetype a = wf.indexOf(QStringLiteral("\n  ") + jobs.at(i) + QStringLiteral(":\n"));
            const qsizetype b = i + 1 < jobs.size() ? wf.indexOf(QStringLiteral("\n  ") + jobs.at(i + 1) + QStringLiteral(":\n")) : wf.size();
            body.insert(jobs.at(i), a >= 0 && b > a ? wf.mid(a, b - a) : QString());
        }
        bool keyOnlyThere = true;
        for (const QString &j : jobs)
            if (body.value(j).contains(QLatin1String("secrets.QB_SIGNING_KEY")) != (j == QLatin1String("sign-packages") || j == QLatin1String("sign-checksums")))
                keyOnlyThere = false;
        const bool noThirdParty = !body.value(QStringLiteral("sign-packages")).contains(QLatin1String("container:"))
                                  && !body.value(QStringLiteral("sign-packages")).contains(QLatin1String("cloud-setup"))
                                  && !body.value(QStringLiteral("sign-checksums")).contains(QLatin1String("cloud-setup"));
        check(keyOnlyThere && noThirdParty, QStringLiteral("release workflow: the signing key reaches only the two signing jobs, which run no third-party code"));
        // (since 2026-11-11 the release does not wait for the tests: the writer's decision, so that the README's links
        // work as soon as the release is built and signed; the suite runs beside it and reports)
        const auto needs = [&](const QString &job) {
            const QString b = body.value(job);
            const qsizetype i = b.indexOf(QLatin1String("needs:"));
            return i < 0 ? QString() : b.mid(i, b.indexOf(QLatin1Char('\n'), i) - i);
        };
        check(body.value(QStringLiteral("tests")).contains(QLatin1String("run-all-tests.sh")) && !needs(QStringLiteral("publish")).contains(QLatin1String("tests"))
                  && !needs(QStringLiteral("sign-checksums")).contains(QLatin1String("tests"))
                  && needs(QStringLiteral("publish")).contains(QLatin1String("sign-checksums")),
              QStringLiteral("release workflow: the full test suite runs beside the release, which waits only for the signed files"),
              needs(QStringLiteral("publish")) + QLatin1String(" / ") + needs(QStringLiteral("sign-checksums")));
    }
    // make-package.py sign, and what the program says about the signature
    {
        const QString key = W + QStringLiteral("/key.pem");
        run(QStringLiteral("openssl"), {QStringLiteral("genpkey"), QStringLiteral("-algorithm"), QStringLiteral("ed25519"), QStringLiteral("-out"), key});
        QProcess pk;
        pk.start(QStringLiteral("openssl"), {QStringLiteral("pkey"), QStringLiteral("-in"), key, QStringLiteral("-pubout"), QStringLiteral("-outform"), QStringLiteral("DER")});
        pk.waitForFinished();
        const QByteArray pub = pk.readAllStandardOutput().right(32);
        qputenv("QB_TEST_SIGNING_KEY", pub.toHex());
        const QString addon = W + QStringLiteral("/addon");
        writeAll(addon + QStringLiteral("/manifest.json"), R"({"id": "test-sign", "name": "Test", "version": "1.0", "description": "x"})");
        writeAll(addon + QStringLiteral("/payload/themes/t.json"), R"({"name": "T", "base": "light", "colors": {}})");
        const QString pkg = W + QStringLiteral("/test-sign.qbpkg");
        const QString tool = src + QStringLiteral("/tools/make-package.py");
        run(QStringLiteral("python3"), {QStringLiteral("-I"), tool, QStringLiteral("addon"), addon, pkg});
        const signing::State before = addons::inspect(pkg).signature;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QB_SIGNING_KEY_FILE"), key);
        int rc = 0;
        const QString o = run(QStringLiteral("python3"), {QStringLiteral("-I"), tool, QStringLiteral("sign"), pkg}, &rc, 60000, env);
        const signing::State after = addons::inspect(pkg).signature;
        check(before == signing::State::Unsigned && after == signing::State::Valid,
              QStringLiteral("make-package.py sign signs a finished package (the release workflow's signing job)"), o);
        // the launcher's check of an installed update (audit A U-07)
        const QString core = W + QStringLiteral("/core/9.9.9");
        const QByteArray program = "#!/bin/sh\necho program\n";
        const QByteArray sha = QCryptographicHash::hash(program, QCryptographicHash::Sha256).toHex();
        writeAll(core + QStringLiteral("/quillbinder"), program);
        const QByteArray manifest = QStringLiteral(R"({"format": 1, "id": "core", "kind": "core", "version": "9.9.9", "files": [{"path": "quillbinder", "sha256": "%1", "size": %2}]})")
                                        .arg(QString::fromLatin1(sha)).arg(program.size()).toUtf8();
        writeAll(core + QStringLiteral("/package-manifest.json"), manifest);
        // the signature as make-package.py writes it
        writeAll(W + QStringLiteral("/m.json"), manifest);
        run(QStringLiteral("openssl"), {QStringLiteral("pkeyutl"), QStringLiteral("-sign"), QStringLiteral("-rawin"), QStringLiteral("-inkey"), key,
                                        QStringLiteral("-in"), W + QStringLiteral("/m.json"), QStringLiteral("-out"), W + QStringLiteral("/m.sig")});
        const QByteArray raw = readAll(W + QStringLiteral("/m.sig"));
        QJsonObject so;
        so.insert(QStringLiteral("algorithm"), QStringLiteral("ed25519"));
        so.insert(QStringLiteral("key"), signing::keyId(pub));
        so.insert(QStringLiteral("signature"), QString::fromLatin1(raw.toBase64()));
        so.insert(QStringLiteral("signs"), QStringLiteral("manifest.json"));
        writeAll(core + QStringLiteral("/package-manifest.sig"), QJsonDocument(so).toJson());
        QString why;
        const bool good = launcher::coreOrigin(core, QStringLiteral("quillbinder"), sha, &why);
        const bool otherProgram = launcher::coreOrigin(core, QStringLiteral("quillbinder"), QByteArray(64, 'a'), &why);
        QByteArray tampered = manifest;
        tampered.replace("9.9.9", "9.9.8");
        writeAll(core + QStringLiteral("/package-manifest.json"), tampered);
        const bool changed = launcher::coreOrigin(core, QStringLiteral("quillbinder"), sha, &why);
        check(good && !otherProgram && !changed,
              QStringLiteral("the launcher starts an installed update only with the project's signature over the program file it names"),
              why);
        qunsetenv("QB_TEST_SIGNING_KEY");
    }
    // the self-test with the font check (audit A S-08)
    {
        const QString prog = buildDir + QStringLiteral("/quillbinder");
        int rc = 0;
        const QString o = run(prog, {QStringLiteral("--selftest")}, &rc, 60000);
        check(rc == 0 && o.contains(QLatin1String("selftest ok")), QStringLiteral("the self-test of a program update checks the font code too"), o.right(300));
    }
}

// ------------------------------------------------------------------ German, the dark page

static void germanAndDark(const QString &W, const QString &src)
{
    std::printf("== The German interface and the dark page (audit A S-06, S-07; the writer's decisions)\n");
    // every text translated, and the compiled file the program carries is the one of the .ts
    {
        const QString ts = QString::fromUtf8(readAll(src + QStringLiteral("/translations/quillbinder_de.ts")));
        const int messages = int(ts.count(QLatin1String("<message")));
        const int unfinished = int(ts.count(QLatin1String("type=\"unfinished\"")));
        check(messages > 4000 && unfinished == 0, QStringLiteral("translations/quillbinder_de.ts: %1 texts, all translated").arg(messages),
              QStringLiteral("%1 unfinished").arg(unfinished));
        QTranslator t;
        QTranslator q;
        const bool loaded = t.load(QStringLiteral(":/translations/quillbinder_de.qm")) && q.load(QStringLiteral(":/translations/qt_de.qm"));
        QCoreApplication::installTranslator(&t);
        QCoreApplication::installTranslator(&q);
        const QString file = QCoreApplication::translate("qb::MainWindow", "&File");
        const QString cancel = QCoreApplication::translate("QPlatformTheme", "Cancel");
        const QString ctrl = QCoreApplication::translate("QShortcut", "Ctrl");
        check(loaded && file == QLatin1String("&Datei") && cancel == QLatin1String("Abbrechen") && ctrl == QLatin1String("Strg"),
              QStringLiteral("the program carries the German texts (menus, Qt's buttons, key names)"),
              file + QLatin1String(" / ") + cancel + QLatin1String(" / ") + ctrl);
        // the main window in German
        {
            MainWindow w;
            w.resize(1400, 900);
            w.show();
            settle(400);
            QStringList titles;
            for (QAction *a : w.menuBar()->actions())
                titles << a->text().remove(QLatin1Char('&'));
            check(titles.contains(QLatin1String("Datei")) && titles.contains(QLatin1String("Bearbeiten")) && titles.contains(QLatin1String("Hilfe")),
                  QStringLiteral("with German set, the menus speak German"), titles.join(QLatin1Char(' ')));
            QString err;
            auto made = Project::create(W, QStringLiteral("Deutsch"), &err);
            textDoc(made.get(), made->draft(), QStringLiteral("Kapitel"), QStringLiteral("Ein Text."));
            made->save(&err);
            const QString folder = made->folderPath();
            made.reset();
            // (the project gets the look for new projects: a dark theme with the standard paper, to see the dark page)
            AppSettings &as = AppSettings::instance();
            const QString themeBefore = as.theme;
            as.theme = QStringLiteral("dark");
            as.save();
            w.openProject(folder);
            settle(500);
            shot(&w, QStringLiteral("v0700-german-window-dark"));
            check(theme::color(theme::Role::EditorPaper) == QColor(0x1e, 0x1e, 0x1e) && theme::color(theme::Role::EditorText) == QColor(Qt::white),
                  QStringLiteral("a new project with a dark theme shows the very dark gray page with white text"),
                  theme::color(theme::Role::EditorPaper).name() + QLatin1Char(' ') + theme::color(theme::Role::EditorText).name());
            as.theme = themeBefore;
            as.save();
            // an accelerator used twice in one menu is told (not a failure: Qt then cycles between them);
            // the menus are checked as they open (the letters are sorted out then)
            QStringList twice;
            for (QAction *top : w.menuBar()->actions())
                if (top->menu())
                    emit top->menu()->aboutToShow();
            settle(100);
            for (QAction *top : w.menuBar()->actions()) {
                if (!top->menu())
                    continue;
                QMap<QChar, QStringList> seen;
                for (QAction *a : top->menu()->actions()) {
                    const QString t = a->text();
                    const qsizetype i = t.indexOf(QLatin1Char('&'));
                    if (i >= 0 && i + 1 < t.size() && t.at(i + 1) != QLatin1Char('&'))
                        seen[t.at(i + 1).toLower()] << t;
                }
                for (auto it = seen.cbegin(); it != seen.cend(); ++it)
                    if (it.value().size() > 1)
                        twice << top->text() + QLatin1String(": ") + it.value().join(QLatin1String(" / "));
            }
            info(QStringLiteral("German menus: %1 letter(s) used twice in a menu%2").arg(twice.size()).arg(twice.isEmpty() ? QString() : QLatin1String(": ") + twice.mid(0, 6).join(QLatin1String("; "))));
            w.closeProject();
            settle(200);
        }
        QCoreApplication::removeTranslator(&q);
        QCoreApplication::removeTranslator(&t);
        check(QCoreApplication::translate("qb::MainWindow", "&File") == QLatin1String("&File"), QStringLiteral("without the German texts the program speaks English (the standard)"));
        check(settingslayer::programOnly(QStringLiteral("app/language")), QStringLiteral("the language is the program's, never a project's"));
    }
    // the dark page
    {
        theme::Theme dark;
        theme::load(QStringLiteral("dark"), &dark);
        const QMap<QString, QColor> standard = AppSettings::standardOwnColors();
        const QMap<theme::Role, QColor> c = theme::resolvedColors(dark, standard);
        check(c.value(theme::Role::EditorPaper) == QColor(0x1e, 0x1e, 0x1e) && c.value(theme::Role::EditorText) == QColor(Qt::white),
              QStringLiteral("a dark theme with the standard paper: a very dark gray page with white text"),
              c.value(theme::Role::EditorPaper).name() + QLatin1Char(' ') + c.value(theme::Role::EditorText).name());
        QMap<QString, QColor> own = standard;
        own.insert(QStringLiteral("editorPaper"), QColor(0x33, 0x44, 0x55));
        const QMap<theme::Role, QColor> c2 = theme::resolvedColors(dark, own);
        check(c2.value(theme::Role::EditorPaper) == QColor(0x33, 0x44, 0x55), QStringLiteral("a page color the writer chose stays"));
        theme::Theme light;
        theme::load(QStringLiteral("system"), &light);
        Q_UNUSED(light);
    }
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
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    const QString src = QStringLiteral(QB_TEST_SOURCE_DIR) + QStringLiteral("/..");
    const QString buildDir = QStringLiteral(QB_TEST_BUILD_DIR);
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.70.0")) >= 0,
          QStringLiteral("version 0.70.0 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings::instance().load();
    QTemporaryDir work;
    const QString W = work.path();
    // a window left open by mistake would stop the run: told and closed after 10 s
    QTimer watcher;
    QElapsedTimer modalSince;
    QObject::connect(&watcher, &QTimer::timeout, [&modalSince] {
        QWidget *m = QApplication::activeModalWidget();
        if (!m) {
            modalSince.invalidate();
            return;
        }
        if (!modalSince.isValid())
            modalSince.start();
        if (modalSince.elapsed() > 10000) {
            auto *b = qobject_cast<QMessageBox *>(m);
            info(QStringLiteral("(a window stayed open and was closed: %1 %2 \"%3\")")
                     .arg(QString::fromLatin1(m->metaObject()->className()), m->objectName(), b ? b->text().left(200) : m->windowTitle()));
            m->close();
            modalSince.invalidate();
        }
    });
    watcher.start(1000);

    // QB_V0700_ONLY=markdown,rtf,damaged,saves,backups,pictures,dialogs,hostile,release,german runs only these
    const QStringList only = qEnvironmentVariable("QB_V0700_ONLY").split(QLatin1Char(','), Qt::SkipEmptyParts);
    auto want = [&only](const char *n) { return only.isEmpty() || only.contains(QLatin1String(n)); };
    if (only.contains(QLatin1String("mdspeed"))) { // (profiling: one long paragraph of bold spans)
        QString para;
        for (int i = 0; i < 1500; ++i)
            para += QStringLiteral("word **bold%1** ").arg(i);
        QTextDocument d;
        md::read(para, &d, Formatting());
        return 0;
    }
    if (want("markdown"))
        markdownRoundTrips();
    if (want("rtf"))
        rtfReader();
    if (want("damaged"))
        damagedFiles(W);
    if (want("saves"))
        failedSaves(W);
    if (want("backups"))
        backups(W);
    if (want("pictures"))
        picturesMove(W);
    if (want("dialogs"))
        dialogsAndSettings(W, src);
    if (want("hostile"))
        hostileFiles(W, src, buildDir);
    if (want("release"))
        buildAndRelease(W, src, buildDir);
    if (want("german"))
        germanAndDark(W, src);

    std::printf("%d failure(s)\n", failures);
    return failures;
}
