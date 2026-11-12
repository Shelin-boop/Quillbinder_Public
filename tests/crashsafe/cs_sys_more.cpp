// The system test, stage 2: organizing, search, targets (S-O01 to S-O06), compile, export, print,
// compare (S-C01 to S-C06), import and media (S-M01 to S-M06), look and settings (S-L01 to S-L05,
// S-L07), section 6.2. S-L06 is in stage 1 (cs_cases.cpp).
#include "cs.h"

#include "core/Addons.h"
#include "core/Backup.h"
#include "import/Compare.h"
#include "import/CompareFiles.h"
#include "text/AutoCorrect.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/CompileDialog.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "compile/PluginManager.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/SearchTools.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTreeWidget>
#include <QUrl>

#include <random>

namespace cs {

using namespace qb;

namespace {

void atEnd(TextEditor *e)
{
    QTextCursor c = e->textCursor();
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    e->setFocus(Qt::OtherFocusReason);
}

// the compile dialog: an output format by its text, a file, Compile
void compileLater(int ms, const QString &exporter, const QString &file, bool *done, const std::function<void(CompileDialog *)> &more = {})
{
    withModal<CompileDialog>(ms, [exporter, file, done, more](CompileDialog *d) {
        if (auto *cb = d->findChild<QComboBox *>(QStringLiteral("compileExporter"))) {
            for (int i = 0; i < cb->count(); ++i)
                if (cb->itemText(i).contains(exporter, Qt::CaseInsensitive)) {
                    cb->setCurrentIndex(i);
                    break;   // 0.64.2: the first match (the program's own writer, not "... via Pandoc")
                }
            fact(QStringLiteral("compile: output formats offered"), [cb] {
                QStringList s;
                for (int i = 0; i < cb->count(); ++i)
                    s << cb->itemText(i);
                return s.join(QLatin1String(", "));
            }());
        }
        if (more)
            more(d);
        fileDialogLater(300, file);
        if (auto *b = d->findChild<QPushButton *>(QStringLiteral("compileNow"))) {
            if (done)
                *done = true;
            b->click();
        }
        settle(500);
        if (d->isVisible())
            d->reject();
    });
}

int countFiles(const QString &dir, const QString &pattern)
{
    int n = 0;
    QDirIterator it(dir, {pattern}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

} // namespace

// ================================================================ S-O01 project search
void caseSO01()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx2-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        BinderPanel *bp = binderOf(w);
        if (!p || !bp)
            continue;
        chooseByMenu(w, QStringLiteral("projectSearch"));
        settle(200);
        struct Q {
            QString what;
            SearchQuery q;
        };
        QList<Q> qs;
        SearchQuery base;
        base.text = QStringLiteral("the");
        qs << Q{QStringLiteral("everywhere"), base};
        SearchQuery t = base;
        t.inText = t.inSynopsis = t.inNotes = t.inKeywords = t.inCustom = false;
        qs << Q{QStringLiteral("titles only"), t};
        SearchQuery cw = base;
        cw.caseSensitive = cw.wholeWord = true;
        cw.text = QStringLiteral("The");
        qs << Q{QStringLiteral("case and whole word"), cw};
        SearchQuery rx = base;
        rx.regex = true;
        rx.text = QStringLiteral("\\bw[a-z]+s\\b");
        qs << Q{QStringLiteral("a regular expression"), rx};
        SearchQuery none = base;
        none.text = QStringLiteral("zzzqqq-no-such-word");
        qs << Q{QStringLiteral("no hits"), none};
        for (const Q &q : qs) {
            QElapsedTimer tm;
            tm.start();
            bp->runSearch(q.q);
            settle(200);
            const int hits = bp->resultsList() ? bp->resultsList()->count() : -1;
            fact(QStringLiteral("S-O01 %1, %2: results (ms)").arg(fx, q.what), QStringLiteral("%1 (%2)").arg(hits).arg(tm.elapsed()));
            if (q.what == QLatin1String("no hits"))
                check(hits == 0, QStringLiteral("%1: a search with no hits shows none").arg(fx));
        }
        bp->runSearch(qs.first().q);
        settle(200);
        if (bp->resultsList() && bp->resultsList()->count()) {
            QListWidgetItem *li = bp->resultsList()->item(0);
            QTest::mouseClick(bp->resultsList()->viewport(), Qt::LeftButton, Qt::NoModifier, bp->resultsList()->visualItemRect(li).center());
            settle(300);
            shot(w, QStringLiteral("search-hit-%1").arg(fx));
        }
        bp->hideSearch();
        checkCleanWindows(w);
        finishProject(w);
        delete w;
    }
}

// ================================================================ S-O02 project replace
void caseSO02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx2-scriv")));
    Project *p = w->project();
    if (!p)
        return;
    auto replaceOnce = [&](const QString &find, const QString &with, QString *result) {
        withModal<ProjectReplaceDialog>(300, [find, with, result](ProjectReplaceDialog *d) {
            if (auto *f = d->findChild<QLineEdit *>(QStringLiteral("replaceFind")))
                f->setText(find);
            if (auto *t = d->findChild<QLineEdit *>(QStringLiteral("replaceWith")))
                t->setText(with);
            if (auto *c = d->findChild<QPushButton *>(QStringLiteral("replaceCount")))
                c->click();
            settle(1500);
            if (auto *r = d->findChild<QLabel *>(QStringLiteral("replaceResult")))
                *result = r->text();
            answerBoxLater(200, QMessageBox::Yes);
            if (auto *a = d->findChild<QPushButton *>(QStringLiteral("replaceAllButton")))
                a->click();
            settle(3000);
            if (auto *r = d->findChild<QLabel *>(QStringLiteral("replaceResult")))
                *result += QStringLiteral(" | ") + r->text();
            if (d->isVisible())
                d->reject();
        });
        QElapsedTimer t;
        t.start();
        chooseByMenu(w, QStringLiteral("projectReplace"));
        settle(1000);
        return t.elapsed();
    };
    const QString before = docText(p, textItems(p).value(50));
    QString r1, r2;
    const qint64 t1 = replaceOnce(QStringLiteral("river"), QStringLiteral("XRIVERX"), &r1);
    fact(QStringLiteral("S-O02 replace (ms): counts"), QStringLiteral("%1: %2").arg(t1).arg(r1));
    const qint64 t2 = replaceOnce(QStringLiteral("XRIVERX"), QStringLiteral("river"), &r2);
    fact(QStringLiteral("S-O02 reversed (ms): counts"), QStringLiteral("%1: %2").arg(t2).arg(r2));
    check(docText(p, textItems(p).value(50)) == before, QStringLiteral("replacing and reversing gives the text back"));
    finishProject(w);
    delete w;
}

// ================================================================ S-O03 find by formatting
void caseSO03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    TextEditor *e = showDoc(w, textItems(p).value(1));
    withModal<FormatFindWindow>(300, [&](FormatFindWindow *d) {
        auto *kind = d->findChild<QComboBox *>(QStringLiteral("formatKind"));
        auto *next = d->findChild<QAbstractButton *>(QStringLiteral("formatNext"));
        auto *prev = d->findChild<QAbstractButton *>(QStringLiteral("formatPrevious"));
        QStringList seen;
        for (int i = 0; kind && i < kind->count(); ++i) {
            kind->setCurrentIndex(i);
            settle(50);
            int moves = 0;
            for (int k = 0; k < 4 && next; ++k) {
                next->click();
                settle(60);
                ++moves;
            }
            if (prev)
                prev->click();
            seen << QStringLiteral("%1: %2").arg(kind->itemText(i), visiblePane(w) && visiblePane(w)->currentItem() ? visiblePane(w)->currentItem()->title : QStringLiteral("-"));
        }
        fact(QStringLiteral("S-O03 where each kind led"), seen.join(QLatin1String("; ")));
        d->close();
    });
    chooseByMenu(w, QStringLiteral("findFormat"));
    settle(2500);
    Q_UNUSED(e)
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-O04 targets
void caseSO04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    withModal<QDialog>(300, [](QDialog *d) {
        const QList<QSpinBox *> spins = d->findChildren<QSpinBox *>();
        if (spins.size() >= 2) {
            spins.at(0)->setValue(90000);
            spins.at(1)->setValue(1500);
        }
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("targets"));
    settle(300);
    p->targets.draftTarget = 90000;
    p->targets.sessionTarget = 1500;
    p->targets.deadline = QDate::currentDate().addDays(30);
    p->markIndexDirty();
    BinderItem *it = textItems(p).value(3);
    TextEditor *e = showDoc(w, it);
    const int before = p->sessionTargetToday();
    if (e) {
        atEnd(e);
        typeInto(e, QLatin1Char(' ') + atParagraphs(3700, ctx().smoke ? 60 : ctx().quick ? 300 : 1000).join(QLatin1Char(' ')));
    }
    settle(1000);
    fact(QStringLiteral("S-O04 session target today"), QStringLiteral("%1 (before typing %2)").arg(p->sessionTargetToday()).arg(before));
    w = reopenWindow(w);
    p = w->project();
    check(p && p->targets.draftTarget == 90000 && p->targets.sessionTarget == 1500, QStringLiteral("the targets are kept after reopening"));
    finishProject(w);
    delete w;
}

// ================================================================ S-O05 writing history and statistics
void caseSO05()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx2-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        if (!p)
            continue;
        if (TextEditor *e = showDoc(w, textItems(p).value(2))) {
            atEnd(e);
            typeInto(e, QLatin1Char(' ') + atParagraphs(3800, 120).join(QLatin1Char(' ')));
        }
        p->save();
        QString history;
        withModal<QDialog>(400, [&history](QDialog *d) {
            for (QLabel *l : d->findChildren<QLabel *>())
                history += l->text() + QLatin1Char(' ');
            for (QTreeWidget *t : d->findChildren<QTreeWidget *>())
                if (t->topLevelItemCount())
                    history += t->topLevelItem(0)->text(0) + QLatin1Char(' ') + t->topLevelItem(0)->text(1);
            d->accept();
        });
        chooseByMenu(w, QStringLiteral("history"));
        settle(600);
        check(history.contains(QDate::currentDate().toString(Qt::ISODate)) || history.contains(QDate::currentDate().toString(QStringLiteral("d MMM"))) || !history.isEmpty(),
              QStringLiteral("%1: Writing History shows today").arg(fx), history.left(200));
        QElapsedTimer t;
        t.start();
        withModal<QDialog>(200, [](QDialog *d) { d->accept(); }, 100);
        chooseByMenu(w, QStringLiteral("statistics"));
        settle(300);
        fact(QStringLiteral("S-O05 %1 statistics (ms)").arg(fx), int(t.elapsed()));
        check(t.elapsed() < 10000 || fx.startsWith(QLatin1String("fx1")), QStringLiteral("%1: the statistics within 10 s").arg(fx));
        finishProject(w);
        delete w;
    }
}

// ================================================================ S-O06 project settings and settings files
void caseSO06()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    // a label used by 30 documents, then deleted
    const QList<BinderItem *> docs = textItems(p);
    const int labelId = p->labels.value(1).id;
    for (BinderItem *it : docs.mid(0, 30)) {
        it->label = labelId;
        p->notifyItemChanged(it);
    }
    withModal<ProjectSettingsDialog>(400, [](ProjectSettingsDialog *d) {
        QStringList pages;
        for (QListWidget *l : d->findChildren<QListWidget *>())
            for (int i = 0; i < l->count(); ++i) {
                l->setCurrentRow(i);
                settle(60);
                pages << l->item(i)->text();
            }
        fact(QStringLiteral("S-O06 Project Settings pages"), pages.join(QLatin1String(", ")));
        d->reject();   // Cancel first
    });
    chooseByMenu(w, QStringLiteral("projectSettings"));
    settle(800);
    for (int i = 0; i < int(p->labels.size()); ++i)
        if (p->labels.at(i).id == labelId) {
            p->labels.removeAt(i);
            break;
        }
    p->markIndexDirty();
    withModal<ProjectSettingsDialog>(400, [](ProjectSettingsDialog *d) { d->accept(); });
    chooseByMenu(w, QStringLiteral("projectSettings"));
    settle(800);
    // save and load settings files
    const QString file = ctx().work + QStringLiteral("/project.qbsettings");
    check(w->exportProjectSettings(file), QStringLiteral("Project > Save Project Settings to File"));
    // 0.64.2: a file equal to the project changes nothing (and keeps no copy): the project's labels change first
    p->labels.append(Label{9901, QStringLiteral("Added after saving the file"), QColor(10, 120, 200)});
    p->markIndexDirty();
    withModal<QDialog>(500, [](QDialog *d) { d->accept(); });
    check(w->importProjectSettings(file), QStringLiteral("Project > Load Project Settings from File with the per-section window"));
    settle(500);
    check(!QDir(p->settingsDir()).entryList({QStringLiteral("settings-before-loading-*")}, QDir::Files).isEmpty(), QStringLiteral("the copy of the settings before loading is kept"));
    w = reopenWindow(w);
    p = w->project();
    int labelled = 0;
    for (BinderItem *it : p ? textItems(p).mid(0, 30) : QList<BinderItem *>()) {
        bool known = false;
        for (const Label &l : p->labels)
            if (l.id == it->label)
                known = true;
        if (known && it->label == labelId)
            ++labelled;
    }
    check(labelled == 0, QStringLiteral("documents with the deleted label show none"));
    finishProject(w);
    delete w;
}

// ================================================================ S-C01 compiling through the dialog
void caseSC01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    struct Out {
        QString exporter, suffix;
    };
    for (const Out &o : {Out{QStringLiteral("PDF"), QStringLiteral("pdf")}, Out{QStringLiteral("E-book"), QStringLiteral("epub")}, Out{QStringLiteral("Word"), QStringLiteral("docx")},
                         Out{QStringLiteral("Markdown"), QStringLiteral("md")}, Out{QStringLiteral("Fountain"), QStringLiteral("fountain")},
                         Out{QStringLiteral("Final Draft"), QStringLiteral("fdx")}}) {
        const QString file = ctx().dir + QStringLiteral("/compiled.") + o.suffix;
        bool done = false;
        compileLater(400, o.exporter, file, &done, [](CompileDialog *d) {
            if (auto *le = d->findChild<QLineEdit *>(QStringLiteral("meta_language")))
                le->setText(QStringLiteral("de-DE"));
        });
        QElapsedTimer t;
        t.start();
        chooseByMenu(w, QStringLiteral("compile"));
        waitFor([&] { return QFileInfo(file).size() > 0; }, 120000);
        settle(500);
        fact(QStringLiteral("S-C01 %1 (ms)").arg(o.exporter), int(t.elapsed()));
        check(QFileInfo(file).size() > 0, QStringLiteral("File > Compile to %1 writes the file").arg(o.exporter));
    }
    const QString pt = pdfToText(ctx().dir + QStringLiteral("/compiled.pdf"));
    check(pt.contains(docText(p, textItems(p).value(0)).left(30).simplified().left(20)), QStringLiteral("the PDF holds the first scene's first sentence"));
    if (QFileInfo::exists(QStringLiteral("/usr/share/java/epubcheck.jar"))) {
        int rc = -1;
        const QString out = runTool(QStringLiteral("java"), {QStringLiteral("-jar"), QStringLiteral("/usr/share/java/epubcheck.jar"), ctx().dir + QStringLiteral("/compiled.epub")}, &rc, 180000);
        check(rc == 0, QStringLiteral("epubcheck passes"), out.right(400));
    } else {
        note(QStringLiteral("[L] no epubcheck"));
    }
    // the Preview
    withModal<CompileDialog>(400, [](CompileDialog *d) {
        if (auto *b = d->findChild<QPushButton *>(QStringLiteral("compilePreview")))
            b->click();
        settle(3000);
        closeDialogsLater(100);
        settle(200);
        d->reject();
    });
    chooseByMenu(w, QStringLiteral("compile"));
    settle(4000);
    finishProject(w);
    delete w;
}

// ================================================================ S-C02 the compile format editor
void caseSC02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QString file = ctx().dir + QStringLiteral("/edited-format.docx");
    bool done = false;
    compileLater(400, QStringLiteral("Word"), file, &done, [](CompileDialog *d) {
        if (auto *formats = d->findChild<QListWidget *>(QStringLiteral("compileFormats"))) {
            for (QAbstractButton *b : d->findChildren<QAbstractButton *>())
                if (b->objectName().contains(QLatin1String("uplicate"))) {
                    b->click();
                    break;
                }
            settle(300);
            fact(QStringLiteral("S-C02 formats after Duplicate"), formats->count());
        }
        if (auto *tabs = d->findChild<QTabWidget *>(QStringLiteral("compileTabs")))
            for (int i = 0; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                settle(80);
                shot(d, QStringLiteral("compile-tab-%1").arg(i));
            }
    });
    chooseByMenu(w, QStringLiteral("compile"));
    waitFor([&] { return QFileInfo(file).size() > 0; }, 120000);
    check(QFileInfo(file).size() > 0, QStringLiteral("compiled with the duplicated format"));
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-C03 compile edge cases through the dialog
void caseSC03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QString special = ctx().dir + QStringLiteral("/Mein Buch – „Entwurf“ #1 (Ü).pdf");
    bool done = false;
    compileLater(400, QStringLiteral("PDF"), special, &done);
    chooseByMenu(w, QStringLiteral("compile"));
    waitFor([&] { return QFileInfo(special).size() > 0; }, 120000);
    check(QFileInfo(special).size() > 0, QStringLiteral("a file name with special characters"));
    const QDateTime first = QFileInfo(special).lastModified();
    settle(1100);
    compileLater(400, QStringLiteral("PDF"), special, &done);
    answerBoxLater(1500, QMessageBox::Yes);
    chooseByMenu(w, QStringLiteral("compile"));
    waitFor([&] { return QFileInfo(special).lastModified() > first; }, 120000);
    fact(QStringLiteral("S-C03 overwrite of an existing file"), QFileInfo(special).lastModified() > first ? QStringLiteral("replaced after the question") : QStringLiteral("kept"));
    // twice in quick succession, cancel
    compileLater(300, QStringLiteral("Word"), ctx().dir + QStringLiteral("/twice.docx"), &done);
    chooseByMenu(w, QStringLiteral("compile"));
    compileLater(300, QStringLiteral("Word"), ctx().dir + QStringLiteral("/twice.docx"), &done);
    chooseByMenu(w, QStringLiteral("compile"));
    settle(3000);
    withModal<CompileDialog>(300, [](CompileDialog *d) { d->reject(); });
    chooseByMenu(w, QStringLiteral("compile"));
    settle(600);
    // an empty Draft
    for (BinderItem *it : p->allItems())
        it->includeInCompile = false;
    BoxText msg;
    answerBoxLater(2500, QMessageBox::Ok, &msg);
    compileLater(400, QStringLiteral("PDF"), ctx().dir + QStringLiteral("/empty.pdf"), &done);
    chooseByMenu(w, QStringLiteral("compile"));
    settle(4000);
    fact(QStringLiteral("S-C03 an empty Draft"), msg.isEmpty() ? (QFileInfo::exists(ctx().dir + QStringLiteral("/empty.pdf")) ? QStringLiteral("a file was written") : QStringLiteral("nothing, no message")) : msg.left(160));
    for (BinderItem *it : p->allItems())
        it->includeInCompile = true;
    p->markIndexDirty();   // 0.64.2: the compile saved "off"; without this the "on" was never saved
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-C04 export and conversion
void caseSC04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QList<BinderItem *> docs = textItems(p).mid(0, 3);
    QStringList made;
    for (const QString &format : {QStringLiteral("rtf"), QStringLiteral("md"), QStringLiteral("docx"), QStringLiteral("txt"), QStringLiteral("fountain"), QStringLiteral("fdx")}) {
        const QString out = ctx().work + QStringLiteral("/export-") + format;
        QDir().mkpath(out);
        selectItems(w, docs);
        withModal<QDialog>(300, [format, out](QDialog *d) {
            if (auto *cb = d->findChild<QComboBox *>())
                for (int i = 0; i < cb->count(); ++i)
                    if (cb->itemData(i).toString() == format || cb->itemText(i).contains(format, Qt::CaseInsensitive))
                        cb->setCurrentIndex(i);
            for (QCheckBox *c : d->findChildren<QCheckBox *>())
                if (c->text().contains(QLatin1String("otes")))
                    c->setChecked(true);
            const QList<QLineEdit *> edits = d->findChildren<QLineEdit *>();
            if (!edits.isEmpty())
                edits.first()->setText(out);
            d->accept();
        });
        chooseByMenu(w, QStringLiteral("exportFiles"));
        settle(1000);
        made << QStringLiteral("%1: %2 files").arg(format).arg(countFiles(out, QStringLiteral("*")));
    }
    fact(QStringLiteral("S-C04 exported"), made.join(QLatin1String(", ")));
    // Convert Markdown <-> RTF
    const QString md = ctx().work + QStringLiteral("/convert.md");
    writeText(md, "# Title\n\nA *small* text with **bold**.\n");
    fileDialogLater(300, md);
    fileDialogLater(1200, ctx().work + QStringLiteral("/convert.rtf"));
    chooseByMenu(w, QStringLiteral("convertFile"));
    settle(2500);
    check(readText(ctx().work + QStringLiteral("/convert.rtf")).contains(QLatin1String("small")), QStringLiteral("File > Convert Markdown to RTF"));
    finishProject(w);
    delete w;
}

// ================================================================ S-C05 printing
void caseSC05()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    withModal<QDialog>(300, [](QDialog *d) { d->accept(); });
    chooseByMenu(w, QStringLiteral("printSettings"));
    settle(400);
    withModal<QDialog>(500, [](QDialog *d) {
        for (int i = 0; i < 50; ++i)
            QTest::keyClick(d, Qt::Key_PageDown);
        shot(d, QStringLiteral("print-preview"));
        d->reject();
    });
    chooseByMenu(w, QStringLiteral("printPreview"));
    settle(1500);
    BinderItem *it = textItems(p).value(4);
    showDoc(w, it);
    const QString pdf = ctx().dir + QStringLiteral("/printed.pdf");
    withModal<QDialog>(500, [pdf](QDialog *d) {
        for (QComboBox *cb : d->findChildren<QComboBox *>())
            for (int i = 0; i < cb->count(); ++i)
                if (cb->itemText(i).contains(QLatin1String("PDF"), Qt::CaseInsensitive) || cb->itemText(i).contains(QLatin1String("File")))
                    cb->setCurrentIndex(i);
        for (QLineEdit *le : d->findChildren<QLineEdit *>())
            if (le->isVisible() && le->isEnabled())
                le->setText(pdf);
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("print"));
    waitFor([&] { return QFileInfo(pdf).size() > 0; }, 60000);
    const QString t = pdfToText(pdf);
    check(t.contains(docText(p, it).simplified().left(25).left(15)), QStringLiteral("Print Current Document into a PDF file holds the text"), t.left(120));
    finishProject(w);
    delete w;
}

// ================================================================ S-C06 compare and transfer
void caseSC06()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QString docx = ctx().dir + QStringLiteral("/compare.docx");
    bool done = false;
    compileLater(400, QStringLiteral("Word"), docx, &done);
    chooseByMenu(w, QStringLiteral("compile"));
    waitFor([&] { return QFileInfo(docx).size() > 0; }, 120000);
    QString err;
    auto ft = compare::openFile(docx, p->formatting, &err);
    int edits = 0;
    if (ft && ft->canWrite()) {
        for (int i = 3; i < ft->paras.size() && edits < 15; i += 7) {
            if (ft->paras.at(i).text.size() < 10)
                continue;
            ft->replace(i, 0, 0, QStringLiteral("CHANGED%1 ").arg(edits), QVector<quint8>(9 + QString::number(edits).size(), 0));
            ++edits;
        }
        ft->save(docx, &err);
    }
    fact(QStringLiteral("S-C06 edits made in the Word file"), edits);
    // Step by Step: accept 10, reject 5; the windows of the compare are answered by their buttons
    int accepted = 0, rejected = 0;
    withModal<QDialog>(800, [&](QDialog *d) {
        for (int k = 0; k < 15; ++k) {
            const QString want = k < 10 ? QStringLiteral("Accept") : QStringLiteral("Reject");
            for (QAbstractButton *b : d->findChildren<QAbstractButton *>())
                if (b->isVisible() && b->isEnabled() && b->text().remove(QLatin1Char('&')).startsWith(want)) {
                    b->click();
                    (k < 10 ? accepted : rejected) += 1;
                    break;
                }
            settle(150);
        }
        shot(d, QStringLiteral("compare"));
        d->accept();
    });
    w->compareWithFile(docx);
    settle(2000);
    fact(QStringLiteral("S-C06 accepted / rejected"), QStringLiteral("%1 / %2").arg(accepted).arg(rejected));
    w = reopenWindow(w);
    p = w->project();
    int found = 0;
    for (BinderItem *it : p ? textItems(p) : QList<BinderItem *>())
        for (int k = 0; k < 10; ++k)
            if (docText(p, it).contains(QStringLiteral("CHANGED%1 ").arg(k)))
                ++found;
    check(found >= accepted && found <= 10, QStringLiteral("accepted changes are in the project after reopening, rejected ones not (%1)").arg(found));
    finishProject(w);
    delete w;
}

// ================================================================ S-M01 importing files
void caseSM01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QString dir = ctx().sourceDir + QStringLiteral("/tests/import");
    const QStringList files = QDir(dir).entryList(QDir::Files);
    int done = 0;
    for (const QString &f : files) {
        const int before = int(p->allItems().size());
        BoxText msg;
        answerBoxLater(1500, QMessageBox::Ok, &msg);
        fileDialogLater(300, dir + QLatin1Char('/') + f);
        chooseByMenu(w, QStringLiteral("importFiles"));
        settle(1500);
        closeDialogsLater(50);
        settle(100);
        if (int(p->allItems().size()) > before || !msg.isEmpty())
            ++done;
        fact(QStringLiteral("S-M01 ") + f, int(p->allItems().size()) > before ? QStringLiteral("imported") : (msg.isEmpty() ? QStringLiteral("nothing") : msg.left(120)));
    }
    check(done == files.size(), QStringLiteral("each file is imported or reported (%1 of %2)").arg(done).arg(files.size()));
    // dropped onto the binder
    QList<QUrl> urls;
    for (const QString &f : files.mid(0, 4))
        urls << QUrl::fromLocalFile(dir + QLatin1Char('/') + f);
    const QList<BinderItem *> dropped = w->importDropped(p->research(), -1, urls);
    check(!dropped.isEmpty(), QStringLiteral("files dropped onto the binder are imported"), QString::number(dropped.size()));
    finishProject(w);
    delete w;
}

// ================================================================ S-M02 import and split
void caseSM02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QString text;
    int parts = 0;
    for (quint32 s = 0; text::countWords(text) < 40000; ++s) {
        text += atParagraphs(3900 + s, 400).join(QLatin1String("\n\n")) + QStringLiteral("\n\n###\n\n");
        ++parts;
    }
    const QString file = ctx().work + QStringLiteral("/40000.txt");
    writeText(file, text.toUtf8());
    QString many;
    for (int i = 0; i < 2000; ++i)
        many += QStringLiteral("Part %1 text.\n###\n").arg(i);
    writeText(ctx().work + QStringLiteral("/2000.txt"), many.toUtf8());
    for (const QString &f : {file, ctx().work + QStringLiteral("/2000.txt")}) {
        const int before = int(p->allItems().size());
        fileDialogLater(300, f);
        withModal<QDialog>(1200, [](QDialog *d) {
            for (QLineEdit *le : d->findChildren<QLineEdit *>())
                if (le->isVisible())
                    le->setText(QStringLiteral("###"));
            d->accept();
        });
        QElapsedTimer t;
        t.start();
        chooseByMenu(w, QStringLiteral("importSplit"));
        settle(3000);
        fact(QStringLiteral("S-M02 %1: parts (ms)").arg(QFileInfo(f).fileName()), QStringLiteral("%1 (%2)").arg(int(p->allItems().size()) - before).arg(t.elapsed()));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-M03 importing web pages
void caseSM03()
{
    note(QStringLiteral("[A] S-M03 uses the local server of qb_m12_tests over HTTP; HTTPS needs a certificate the session does not have: HTTP and a closed port only"));
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    BoxText msg;
    answerBoxLater(3000, QMessageBox::Ok, &msg);
    QElapsedTimer t;
    t.start();
    BinderItem *none = w->importWebPage(QStringLiteral("http://127.0.0.1:9/nothing"), false, w->project()->research());
    fact(QStringLiteral("S-M03 an address that does not answer"), QStringLiteral("%1 in %2 ms: %3").arg(none ? QStringLiteral("imported") : QStringLiteral("refused")).arg(t.elapsed()).arg(msg.left(120)));
    check(t.elapsed() < 60000, QStringLiteral("no freeze for an address that does not answer"));
    finishProject(w);
    delete w;
}

// ================================================================ S-M04 research: pictures and PDF
void caseSM04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QList<QUrl> urls = {QUrl::fromLocalFile(ctx().sourceDir + QStringLiteral("/tests/import/lens.jpg"))};
    const QString pdf = ctx().work + QStringLiteral("/research.pdf");
    {
        auto tmp = projectFromTemplate(ctx().work, QStringLiteral("pdf source"), QStringLiteral("blank"));
        Q_UNUSED(tmp)
    }
    if (QFileInfo::exists(pdf))
        urls << QUrl::fromLocalFile(pdf);
    const QList<BinderItem *> media = w->importDropped(p->research(), -1, urls, false);
    for (BinderItem *m : media) {
        showDoc(w, m);
        chooseByMenu(w, QStringLiteral("zoomIn"));
        chooseByMenu(w, QStringLiteral("zoomIn"));
        selectItems(w, {m});
        chooseByMenu(w, QStringLiteral("openCopyholder"));
        chooseByMenu(w, QStringLiteral("quickRef"));
        settle(300);
    }
    w = reopenWindow(w);
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-M05 audio and video
void caseSM05()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QList<QUrl> urls;
    for (const QString &f : QDir(ctx().sourceDir + QStringLiteral("/tests/media")).entryList(QDir::Files))
        urls << QUrl::fromLocalFile(ctx().sourceDir + QStringLiteral("/tests/media/") + f);
    const QList<BinderItem *> media = w->importDropped(p->research(), -1, urls, false);
    fact(QStringLiteral("S-M05 media imported"), int(media.size()));
    for (BinderItem *m : media) {
        showDoc(w, m);
        settle(600);
        for (const QString &a : {QStringLiteral("mediaPlayPause"), QStringLiteral("mediaFaster"), QStringLiteral("mediaSlower"), QStringLiteral("mediaNormalSpeed"),
                                 QStringLiteral("mediaVolumeUp"), QStringLiteral("mediaVolumeDown"), QStringLiteral("mediaMute"), QStringLiteral("mediaMute"),
                                 QStringLiteral("mediaForward"), QStringLiteral("mediaBack"), QStringLiteral("mediaPauseRewind"), QStringLiteral("mediaStart"),
                                 QStringLiteral("mediaStop")}) {
            chooseByMenu(w, a);
            settle(100);
        }
    }
    // a timestamp into the notes, and Go to Timestamp
    if (!media.isEmpty()) {
        showDoc(w, media.first());
        chooseByMenu(w, QStringLiteral("mediaPlayPause"));
        settle(800);
        chooseByMenu(w, QStringLiteral("mediaTimestamp"));
        chooseByMenu(w, QStringLiteral("mediaGoToTimestamp"));
        // play in the copyholder while typing in the main editor
        selectItems(w, {media.first()});
        chooseByMenu(w, QStringLiteral("openCopyholder"));
        chooseByMenu(w, QStringLiteral("mediaPlayPause"));
        if (TextEditor *e = showDoc(w, textItems(p).value(3))) {
            atEnd(e);
            QElapsedTimer t;
            t.start();
            typeInto(e, QStringLiteral(" Typed while the media plays."));
            fact(QStringLiteral("S-M05 ms per key during playback"), double(t.elapsed()) / 28.0);
            for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
                typingGate(e, profile, 60, QStringLiteral("S-M05"));
        }
    }
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-M06 media without the add-on
void caseSM06()
{
    for (const addons::Installed &a : addons::installedAddons())
        if (a.manifest.id.contains(QLatin1String("media"))) {
            QString err;
            addons::removeAddon(a.manifest.id, &err);
        }
    // 0.64.2: a plugin loaded in this process stays loaded when its add-on is removed (the program loads it at
    // the start; the build tree has one too): the program is told to act as without it
    compile::PluginManager::instance().setMediaOffForTests(true);
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p) {
        compile::PluginManager::instance().setMediaOffForTests(false);
        return;
    }
    QList<QUrl> urls;
    // audio and video only: README.txt is a text and needs no add-on
    for (const QString &f : QDir(ctx().sourceDir + QStringLiteral("/tests/media")).entryList(QDir::Files))
        if (!f.endsWith(QLatin1String(".txt")))
            urls << QUrl::fromLocalFile(ctx().sourceDir + QStringLiteral("/tests/media/") + f);
    for (BinderItem *m : w->importDropped(p->research(), -1, urls, false)) {
        // in the pane's own view for it (showDoc forces the text view, which shows nothing for a media file)
        if (EditorPane *pane = visiblePane(w))
            pane->showItems({m}, false);
        settle(500);
        QString shown;
        for (QLabel *l : w->findChildren<QLabel *>())
            if (l->isVisible() && l->text().contains(QLatin1String("Media Playback")))
                shown = l->text();
        fact(QStringLiteral("S-M06 ") + m->title, shown.isEmpty() ? QStringLiteral("no message names the add-on") : shown.left(160));
        check(!shown.isEmpty(), QStringLiteral("%1: a message names the add-on").arg(m->title));
    }
    finishProject(w);
    delete w;
    compile::PluginManager::instance().setMediaOffForTests(false);
}

// ================================================================ S-L01 themes
void caseSL01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    chooseByMenu(w, QStringLiteral("splitV"));
    selectItems(w, {p->draft()->children.value(0)});
    chooseByMenu(w, QStringLiteral("viewCork"));
    if (visiblePanes(w).size() > 1)
        visiblePanes(w).at(1)->showItems({p->draft()->children.value(0)}, false, int(ViewMode::Outliner));
    chooseByMenu(w, QStringLiteral("inspect5"));
    for (const QString &t : {QStringLiteral("Default (Follow the Desktop)"), QStringLiteral("Light"), QStringLiteral("Dark Mode"), QStringLiteral("Sepia"), QStringLiteral("Slate"),
                             QStringLiteral("Midnight"), QStringLiteral("Ocean"), QStringLiteral("High Contrast")}) {
        check(menuPath(w, QStringLiteral("Window > Themes > ") + t), QStringLiteral("Window > Themes > %1").arg(t));
        settle(300);
        shot(w, QStringLiteral("theme-") + QString(t).replace(QLatin1Char(' '), QLatin1Char('-')));
    }
    withModal<QDialog>(300, [](QDialog *d) {
        if (auto *le = d->findChild<QLineEdit *>())
            le->setText(QStringLiteral("System test look"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("themeSave"));
    settle(400);
    const QString bad = ctx().work + QStringLiteral("/broken.qbtheme");
    writeText(bad, "{ not a theme");
    BoxText msg;
    fileDialogLater(300, bad);
    answerBoxLater(1200, QMessageBox::Ok, &msg);
    chooseByMenu(w, QStringLiteral("themeImport"));
    settle(1800);
    check(!msg.isEmpty(), QStringLiteral("a damaged theme file gives a message"), msg);
    for (const QString &a : {QStringLiteral("themeManage"), QStringLiteral("themeCustomize")}) {
        withModal<QDialog>(300, [a](QDialog *d) {
            shot(d, a);
            d->reject();
        });
        chooseByMenu(w, a);
        settle(400);
    }
    menuPath(w, QStringLiteral("Window > Themes > Light"));
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-L02 options
void caseSL02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QString settingsFile = QDir::homePath() + QStringLiteral("/.config/Quillbinder/Quillbinder.conf");
    for (const QString &scope : {QStringLiteral("All projects"), QStringLiteral("This project")}) {
        auto programBefore = std::make_shared<QByteArray>(readText(settingsFile).toUtf8());
        // every control changed once, then Cancel
        auto changeAndCancel = [scope, settingsFile, programBefore](OptionsDialog *d) {
            // 0.64.2: the file as it is in the dialog's scope (switching the scope applies the dialog first: that
            // writes the program's values, also the ones never set before, and is not what Cancel is about)
            *programBefore = readText(settingsFile).toUtf8();
            int changed = 0;
            for (QCheckBox *c : d->findChildren<QCheckBox *>())
                if (c->isEnabled() && c->objectName().startsWith(QLatin1String("opt"))) {
                    c->toggle();
                    ++changed;
                }
            for (QSpinBox *s : d->findChildren<QSpinBox *>())
                if (s->isEnabled()) {
                    s->setValue(s->maximum());
                    s->setValue(s->minimum());
                    ++changed;
                }
            for (QComboBox *c : d->findChildren<QComboBox *>())
                if (c->isEnabled() && c->objectName() != QLatin1String("optScope") && c->count() > 1) {
                    c->setCurrentIndex((c->currentIndex() + 1) % c->count());
                    ++changed;
                }
            fact(QStringLiteral("S-L02 %1: controls changed").arg(scope), changed);
            shot(d, QStringLiteral("options-") + QString(scope).replace(QLatin1Char(' '), QLatin1Char('-')));
            d->reject();
        };
        withModal<OptionsDialog>(400, [scope, changeAndCancel](OptionsDialog *d) {
            QComboBox *sc = d->findChild<QComboBox *>(QStringLiteral("optScope"));
            int want = -1;
            for (int i = 0; sc && i < sc->count(); ++i)
                if (sc->itemText(i).contains(scope))
                    want = i;
            if (sc && want >= 0 && sc->currentIndex() != want) {
                // the dialog closes and opens again in the other scope
                withModal<OptionsDialog>(600, changeAndCancel);
                sc->setCurrentIndex(want);
                return;
            }
            changeAndCancel(d);
        });
        chooseByMenu(w, QStringLiteral("options"));
        settle(1500);
        {   // 0.64.2: the lines that differ are named
            // compared as settings ("[group] key=value", sorted): the order of the lines is QSettings' affair
            const QByteArray after = readText(settingsFile).toUtf8();
            const QByteArray programBefore0 = *programBefore;
            auto entries = [](const QByteArray &text) {
                QStringList out;
                QString group;
                for (const QByteArray &raw : text.split('\n')) {
                    const QString l = QString::fromUtf8(raw).trimmed();
                    if (l.isEmpty())
                        continue;
                    if (l.startsWith(QLatin1Char('[')))
                        group = l;
                    else
                        out << group + QLatin1Char(' ') + l;
                }
                out.sort();
                return out;
            };
            const QStringList a = entries(programBefore0), b = entries(after);
            QStringList diff;
            for (const QString &l : b)
                if (!a.contains(l))
                    diff << QStringLiteral("+") + l.left(100);
            for (const QString &l : a)
                if (!b.contains(l))
                    diff << QStringLiteral("-") + l.left(100);
            fact(QStringLiteral("S-L02 %1: the file's bytes are the same").arg(scope), after == programBefore0 ? QStringLiteral("yes") : QStringLiteral("no"));
            check(a == b, QStringLiteral("%1: Cancel leaves the settings unchanged").arg(scope), diff.mid(0, 12).join(QLatin1String(" | ")));
        }
        // OK without changes, then reopen
        withModal<OptionsDialog>(400, [](OptionsDialog *d) { d->accept(); });
        chooseByMenu(w, QStringLiteral("options"));
        settle(1000);
        // the Convert Quotes buttons open their window
        withModal<OptionsDialog>(400, [](OptionsDialog *d) {
            if (auto *b = d->findChild<QAbstractButton *>(QStringLiteral("optConvertQuotesSmart"))) {
                withModal<ConvertQuotesDialog>(300, [](ConvertQuotesDialog *q) { q->reject(); });
                b->click();
                settle(600);
            }
            if (auto *def = d->findChild<QAbstractButton *>(QStringLiteral("optDefaults")))
                fact(QStringLiteral("S-L02 Defaults offered"), def->isEnabled() ? QStringLiteral("yes") : QStringLiteral("no"));
            d->reject();
        });
        chooseByMenu(w, QStringLiteral("options"));
        settle(1500);
    }
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-L03 toolbars, icon bar, shortcuts
void caseSL03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    for (const QString &a : {QStringLiteral("customizeToolbars"), QStringLiteral("customizeIconBar")}) {
        withModal<QDialog>(400, [a](QDialog *d) {
            for (QListWidget *l : d->findChildren<QListWidget *>())
                if (l->count() > 1) {
                    l->setCurrentRow(0);
                    QListWidgetItem *it = l->takeItem(0);
                    l->addItem(it);
                }
            shot(d, a);
            d->accept();
        });
        chooseByMenu(w, a);
        settle(600);
    }
    // a free key for Show Invisibles
    withModal<QDialog>(400, [](QDialog *d) {
        shot(d, QStringLiteral("keyboard-shortcuts"));
        d->reject();
    });
    chooseByMenu(w, QStringLiteral("keyboardShortcuts"));
    settle(600);
    QAction *inv = action(w, QStringLiteral("showInvisibles"));
    if (inv) {
        inv->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9));
        const bool before = inv->isChecked();
        if (TextEditor *e = showDoc(w, textItems(w->project()).value(1))) {
            // 0.64.2: a shortcut works in the active window (after the dialogs it was not)
            w->activateWindow();
            e->setFocus(Qt::OtherFocusReason);
            settle(100);
            QTest::keySequence(e, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9));
        }
        settle(200);
        check(inv->isChecked() != before, QStringLiteral("the new shortcut starts its command"));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-L04 fonts
void caseSL04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QString font;
    for (const QString &f : QDir(ctx().sourceDir + QStringLiteral("/tests/fonts")).entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files))
        font = ctx().sourceDir + QStringLiteral("/tests/fonts/") + f;
    if (!font.isEmpty()) {
        fileDialogLater(300, font);
        answerBoxLater(1500, QMessageBox::Ok);
        chooseByMenu(w, QStringLiteral("installFonts"));
        settle(2000);
    }
    answerBoxLater(600, QMessageBox::Ok); // "No new fonts were found ..." or the fonts found
    chooseByMenu(w, QStringLiteral("reloadFonts"));
    settle(1200);
    fact(QStringLiteral("S-L04 Open Font Folder / Font Manager"), QStringLiteral("%1 / %2").arg(action(w, QStringLiteral("fontFolder")) && action(w, QStringLiteral("fontFolder"))->isEnabled() ? QStringLiteral("offered") : QStringLiteral("not offered"),
                                                                                       action(w, QStringLiteral("fontManager")) && action(w, QStringLiteral("fontManager"))->isEnabled() ? QStringLiteral("offered") : QStringLiteral("not offered")));
    withModal<QDialog>(400, [](QDialog *d) {
        shot(d, QStringLiteral("show-fonts"));
        d->reject();
    });
    chooseByMenu(w, QStringLiteral("showFonts"));
    settle(600);
    // RTF naming a face that is no family. 0.64.2: a Markdown project takes the look of the project for imported
    // text (fonts are not kept, by design) but keeps italics; a project that keeps formatting (Scrivener format,
    // RTF texts) keeps the name as well
    const QString rtf = ctx().work + QStringLiteral("/sitka.rtf");
    writeText(rtf, "{\\rtf1\\ansi{\\fonttbl{\\f0 SitkaTextItalic;}}\\f0 Text in Sitka Text Italic.\\par}");
    auto sitka = [&rtf](MainWindow *win, Project *pr, bool nameKept) {
        const QList<BinderItem *> got = win->importDropped(pr->draft(), -1, {QUrl::fromLocalFile(rtf)}, false);
        if (got.isEmpty()) {
            check(false, QStringLiteral("the RTF file is imported"));
            return;
        }
        QTextCursor c(pr->docs()->text(got.first()->id));
        c.setPosition(3);
        const QString families = c.charFormat().fontFamilies().toStringList().join(QLatin1Char(','));
        fact(QStringLiteral("S-L04 the font of the Sitka text (%1)").arg(pr->storage == StorageFormat::Rtf ? QStringLiteral("RTF") : QStringLiteral("Markdown")),
             families);
        check(c.charFormat().fontItalic(), QStringLiteral("the text takes italics from the font name"));
        if (nameKept)
            check(families.contains(QLatin1String("Sitka")), QStringLiteral("the text keeps the font name"), families);
    };
    sitka(w, p, p->storage == StorageFormat::Rtf);
    finishProject(w);
    delete w;
    w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
    p = w->project();
    if (!p)
        return;
    sitka(w, p, p->storage == StorageFormat::Rtf);
    finishProject(w);
    delete w;
}

// ================================================================ S-L05 default formatting over a whole project
void caseSL05()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx2-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        if (!p)
            continue;
        withModal<QDialog>(300, [](QDialog *d) { d->accept(); });
        chooseByMenu(w, QStringLiteral("defaultTextFormatting"));
        settle(400);
        selectItems(w, {p->draft()});
        BinderPanel *bp = binderOf(w);
        if (bp)
            bp->tree()->selectAll();
        withModal<QDialog>(300, [](QDialog *d) { d->accept(); });
        answerBoxLater(900, QMessageBox::Yes);
        QElapsedTimer t;
        t.start();
        chooseByMenu(w, QStringLiteral("defaultFormatting"));
        settle(1500);
        fact(QStringLiteral("S-L05 %1: Text to Default Formatting over the Draft (ms)").arg(fx), int(t.elapsed()));
        finishProject(w);
        delete w;
    }
}

// ================================================================ S-L07 two books in two languages
void caseSL07()
{
    // FX-8: a German novel and an English paper, each with its own settings
    auto novel = projectFromTemplate(ctx().work, QStringLiteral("Roman"), QStringLiteral("novel"));
    auto paper = projectFromTemplate(ctx().work, QStringLiteral("Paper"), QStringLiteral("blank"));
    if (!novel || !paper)
        return;
    BinderItem *ns = novel->model()->createItem(novel->draft(), -1, ItemType::Text, QStringLiteral("Szene"));
    fillText(novel.get(), ns, atParagraphs(4000, 300, true));
    BinderItem *ps = paper->model()->createItem(paper->draft(), -1, ItemType::Text, QStringLiteral("Section"));
    fillText(paper.get(), ps, atParagraphs(4001, 300));
    // 0.64.2: these options are in the group "editing" (the case wrote "app", which a project may not set)
    writeText(novel->settingsDir() + QStringLiteral("/options.ini"), "[editing]\nspellLanguage=de_DE\nquoteStyle=de\nsmartQuotes=true\n");
    writeText(paper->settingsDir() + QStringLiteral("/options.ini"), "[editing]\nspellLanguage=en_US\nquoteStyle=en\nsmartQuotes=true\n");
    const QString novelPath = novel->indexPath(), paperPath = paper->indexPath();
    novel->save();
    paper->save();
    novel.reset();
    paper.reset();
    AppSettings::instance().spellCheck = true;
    AppSettings::instance().save();
    MainWindow *w = openWindow(novelPath);
    QStringList got;
    for (int round = 0; round < 5; ++round) {
        for (const QString &path : {novelPath, paperPath}) {
            if (w->project())
                w->closeProject();
            menuPath(w, QStringLiteral("File > Open Recent > ") + QFileInfo(path).completeBaseName());
            settle(800);
            Project *p = w->project();
            if (!p)
                continue;
            BinderItem *it = itemByTitle(p, path == novelPath ? QStringLiteral("Szene") : QStringLiteral("Section"));
            TextEditor *e = showDoc(w, it);
            if (!e || round > 0)
                continue;
            atEnd(e);
            QTest::keyClicks(e, QStringLiteral(" \"quoted\" and 'single' marks"));
            const QString tail = e->toPlainText().right(30);
            got << QFileInfo(path).completeBaseName() + QStringLiteral(": ") + tail;
            if (path == novelPath)
                check(tail.contains(QStringLiteral("„quoted“")) && tail.contains(QStringLiteral("‚single‘")), QStringLiteral("the novel types German marks"), tail);
            else
                check(tail.contains(QStringLiteral("“quoted”")) && tail.contains(QStringLiteral("‘single’")), QStringLiteral("the paper types English marks"), tail);
            fact(QStringLiteral("S-L07 spelling language in %1").arg(QFileInfo(path).completeBaseName()), AppSettings::instance().spellLanguage);
            if (path == novelPath)
                check(SpellChecker::instance().isCorrect(QStringLiteral("Straße")), QStringLiteral("German words with ß are not marked in the novel"));
        }
    }
    fact(QStringLiteral("S-L07 typed marks"), got.join(QLatin1String("; ")));
    AppSettings::instance().spellCheck = false;
    AppSettings::instance().save();
    if (w->project())
        finishProject(w);
    delete w;
}

} // namespace cs
