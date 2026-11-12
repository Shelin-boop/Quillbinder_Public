// The system test, stage 1: settings, writing, snapshots, speech, data safety
// (C-27, S-W15, S-W18, S-I11, S-L06, S-R01, S-D01).
#include "cs.h"

#include "compile/PluginManager.h"
#include "core/CrashGuard.h"
#include "core/Replace.h"
#include "core/SettingsFile.h"
#include "core/SettingsLayer.h"
#include "core/Snapshots.h"
#include "import/ScrivenerFormat.h"
#include "text/AutoCorrect.h"
#include "tools/ToolApi.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/SearchTools.h"
#include "ui/TextEditor.h"
#include "ui/ToolHost.h"

#include <QAbstractButton>
#include <QSettings>
#include <QTextFrame>
#include <QTextTable>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QThread>
#include <QtEndian>

#include <atomic>
#include <cmath>
#include <fcntl.h>
#include <random>
#include <unistd.h>

namespace cs {

namespace {

QString allText(Project *p, const QList<BinderItem *> &items)
{
    QString s;
    for (BinderItem *it : items)
        s += rawText(p->docs()->text(it->id));
    return s;
}

QString sansSpace(QString s)
{
    s.remove(QRegularExpression(QStringLiteral("[\\s\\x{2029}]")));
    return s;
}

} // namespace

// ================================================================ C-27 settings layer
void caseC27()
{
    const QString fx1 = fixtureCopy(QStringLiteral("fx1-qb"));
    auto a = Project::open(fx1, nullptr);
    QString err;
    const QString second = ctx().work + QStringLiteral("/second");
    QDir().mkpath(second);
    // a second project: its own two values
    auto b = Project::create(second, QStringLiteral("Second"), &err);
    check(a && b, QStringLiteral("(two projects)"), err);
    if (!a || !b)
        return;
    const QString iniA = settingslayer::fileFor(a->settingsDir());
    const QString iniB = settingslayer::fileFor(b->settingsDir());
    {
        QSettings s(iniA, QSettings::IniFormat);
        s.setValue(QStringLiteral("editing/capitalizeI"), true);
        s.setValue(QStringLiteral("behaviour/backLinks"), false);
        s.setValue(QStringLiteral("editing/ellipsis"), true);
    }
    {
        QSettings s(iniB, QSettings::IniFormat);
        s.setValue(QStringLiteral("editing/quoteStyle"), QStringLiteral("de"));
        s.setValue(QStringLiteral("editing/spellLanguage"), QStringLiteral("de_DE"));
    }
    // which of the candidate keys belong to the program only
    const QStringList candidates = {QStringLiteral("app/recent"), QStringLiteral("app/recentProjects"), QStringLiteral("app/reopenLast"),
                                    QStringLiteral("app/projectFirst"), QStringLiteral("window/geometry"), QStringLiteral("window/state"),
                                    QStringLiteral("layouts/Writing"), QStringLiteral("app/freezeSeconds"), QStringLiteral("app/crashWatchdog"),
                                    QStringLiteral("look/newProjects"), QStringLiteral("app/firstStartDone"), QStringLiteral("dialogs/native")};
    QStringList programOnlyKeys;
    for (const QString &k : candidates)
        if (settingslayer::programOnly(k))
            programOnlyKeys << k;
    fact(QStringLiteral("C-27 program-only among the candidates"), programOnlyKeys.join(QLatin1String(", ")));
    // the model: values the program file, A and B should hold
    QMap<QString, QString> prog, ownA, ownB;
    auto load = [](const QString &ini) {
        QMap<QString, QString> m;
        QSettings s(ini, QSettings::IniFormat);
        for (const QString &k : s.allKeys())
            m.insert(k, s.value(k).toString());
        return m;
    };
    {
        QSettings s(settingslayer::programFile(), QSettings::IniFormat);
        for (const QString &k : s.allKeys())
            prog.insert(k, s.value(k).toString());
    }
    ownA = load(iniA);
    ownB = load(iniB);
    const QStringList keys = QStringList{QStringLiteral("editing/quoteStyle"), QStringLiteral("editing/spellLanguage"),
                                         QStringLiteral("editing/capitalizeI"), QStringLiteral("behaviour/backLinks"),
                                         QStringLiteral("editing/ellipsis"), QStringLiteral("editing/spellUnderline"),
                                         QStringLiteral("cs/test-a"), QStringLiteral("cs/test-b"), QStringLiteral("cs/Umlaut-Größe"),
                                         QStringLiteral("cs/deep/slash/key")}
                             + programOnlyKeys;
    std::mt19937 r(ctx().seed);
    int active = 0;   // 0 none, 1 A, 2 B
    int wrong = 0;
    QStringList wrongLines;
    auto expectRead = [&](const QString &k) -> QString {
        // 0.64.1: a key outside the allow list is never a project's, whatever its file holds
        const bool projects = settingslayer::projectFirst() && !settingslayer::programOnly(k);
        if (active == 1 && ownA.contains(k) && projects)
            return ownA.value(k);
        if (active == 2 && ownB.contains(k) && projects)
            return ownB.value(k);
        return prog.value(k);
    };
    for (int i = 0; i < 1000; ++i) {
        const unsigned op = r() % 20;
        if (op == 0) {
            active = int(r() % 3);
            settingslayer::sync();
            settingslayer::activate(active == 1 ? iniA : active == 2 ? iniB : QString());
        } else if (op < 9) {
            const QString k = keys.at(int(r() % keys.size()));
            const QString v = QStringLiteral("v%1").arg(i);
            {
                QSettings s;
                s.setValue(k, v);
            }
            settingslayer::sync();
            // where it goes: the project's file only when the project holds the key
            const bool projects = settingslayer::projectFirst() && !settingslayer::programOnly(k);
            if (active == 1 && ownA.contains(k) && projects)
                ownA[k] = v;
            else if (active == 2 && ownB.contains(k) && projects)
                ownB[k] = v;
            else
                prog[k] = v;
        } else {
            const QString k = keys.at(int(r() % keys.size()));
            QSettings s;
            const QString got = s.value(k).toString();
            const QString want = expectRead(k);
            if (got != want) {
                ++wrong;
                if (wrongLines.size() < 8)
                    wrongLines << QStringLiteral("op %1, project %2, %3: read \"%4\", expected \"%5\"").arg(i).arg(active).arg(k, got, want);
            }
        }
    }
    settingslayer::sync();
    check(wrong == 0, QStringLiteral("1,000 seeded reads and writes: each read gives the project's value where it holds the key, else the program's"),
          wrongLines.join(QLatin1Char('\n')));
    // the files on disk
    {
        const QMap<QString, QString> fileA = load(iniA), fileB = load(iniB);
        QStringList bad;
        for (auto it = ownA.begin(); it != ownA.end(); ++it)
            if (!settingslayer::programOnly(it.key()) && fileA.value(it.key()) != it.value())
                bad << QStringLiteral("A %1").arg(it.key());
        for (auto it = ownB.begin(); it != ownB.end(); ++it)
            if (!settingslayer::programOnly(it.key()) && fileB.value(it.key()) != it.value())
                bad << QStringLiteral("B %1").arg(it.key());
        for (const QString &k : fileA.keys() + fileB.keys())
            if (settingslayer::programOnly(k))
                bad << QStringLiteral("program-only key in a project file: %1").arg(k);
        check(bad.isEmpty(), QStringLiteral("each project file holds its own keys with the last values, no program-only key"),
              bad.join(QLatin1String(", ")));
        QSettings s(settingslayer::programFile(), QSettings::IniFormat);
        QStringList progBad;
        for (const QString &k : keys)
            if (prog.contains(k) && s.value(k).toString() != prog.value(k))
                progBad << k;
        check(progBad.isEmpty(), QStringLiteral("the program's file holds the program's values"), progBad.join(QLatin1String(", ")));
    }
    // "the program's settings always": no project value is used
    {
        settingslayer::activate(iniB);
        settingslayer::setProjectFirst(false);
        settingslayer::activate(iniB);
        QSettings s;
        const QString got = s.value(QStringLiteral("editing/quoteStyle")).toString();
        check(got == prog.value(QStringLiteral("editing/quoteStyle")),
              QStringLiteral("with the program's settings always, the project's own value is not used"), got);
        settingslayer::setProjectFirst(true);
        settingslayer::activate(QString());
        check(load(iniB).value(QStringLiteral("editing/quoteStyle")) == ownB.value(QStringLiteral("editing/quoteStyle")),
              QStringLiteral("... and the project keeps it for later"));
    }
    // damaged project files
    struct Bad {
        QString name;
        QByteArray data;
        bool readOnly = false;
    };
    QByteArray big = "[cs]\nbig=" + QByteArray(1 << 20, 'x') + "\n";
    QByteArray planted = "[app]\n";
    for (const QString &k : programOnlyKeys)
        planted += k.section(QLatin1Char('/'), 1).toUtf8() + "=planted\n";
    const QList<Bad> bad = {{QStringLiteral("garbage"), QByteArray("\x00\x01\xff\xfe[[[=\n\x7f", 10) + QByteArray(4000, '\x9c')},
                            {QStringLiteral("empty"), QByteArray()},
                            {QStringLiteral("CRLF line ends"), "[editing]\r\nquoteStyle=fr\r\n"},
                            {QStringLiteral("a byte order mark"), "\xEF\xBB\xBF[editing]\nquoteStyle=sv\n"},
                            {QStringLiteral("program-only keys planted"), planted},
                            {QStringLiteral("a value of 1 MB"), big},
                            {QStringLiteral("keys with umlauts and slashes"), "[cs]\nGr%C3%B6%C3%9Fe=1\na%2Fb%2Fc=2\n"},
                            {QStringLiteral("read-only"), "[editing]\nquoteStyle=de\n", true}};
    const QString progBefore = readText(settingslayer::programFile());
    for (const Bad &bd : bad) {
        QFile::setPermissions(iniA, QFile::ReadOwner | QFile::WriteOwner);
        writeText(iniA, bd.data);
        if (bd.readOnly)
            QFile::setPermissions(iniA, QFile::ReadOwner);
        QElapsedTimer t;
        t.start();
        settingslayer::activate(iniA);
        QString seenProjectFirst;
        {
            QSettings s;
            (void)s.value(QStringLiteral("editing/quoteStyle"));
            s.setValue(QStringLiteral("editing/quoteStyle"), QStringLiteral("en"));
            seenProjectFirst = s.value(QStringLiteral("app/projectFirst")).toString();
        }
        settingslayer::sync();
        settingslayer::activate(QString());
        const qint64 ms = t.elapsed();
        QSettings ps(settingslayer::programFile(), QSettings::IniFormat);
        const bool programIntact = ps.status() == QSettings::NoError && !ps.allKeys().isEmpty();
        check(ms < 5000 && programIntact, QStringLiteral("a damaged project settings file (%1): no crash, the program's file intact (%2 ms)").arg(bd.name).arg(ms));
        if (bd.name.startsWith(QLatin1String("program-only")))
            fact(QStringLiteral("C-27 planted program-only keys"), settingslayer::projectFirst() ? QStringLiteral("ignored (project settings first stays on)")
                                                                                                 : QStringLiteral("taken from the project"));
        if (bd.readOnly)
            fact(QStringLiteral("C-27 read-only project settings file, a change"), load(iniA).value(QStringLiteral("editing/quoteStyle")));
    }
    QFile::setPermissions(iniA, QFile::ReadOwner | QFile::WriteOwner);
    check(settingslayer::projectFirst(), QStringLiteral("planted keys never switch \"project settings first\" off"));
    // 0.64.1: the allow list decides. Keys that name a program, a folder, a file or an address, and keys
    // outside the allowed groups, are never taken from a project; the app keys of the list are.
    {
        writeText(iniA, "[app]\ncitationsApp=planted\nsharedTemplates=planted\nforename=Planted\nautosave=77\n"
                        "[backup]\ndir=planted\nkeep=9\n[tools]\nconverterPath=planted\nserverUrl=planted\n"
                        "[cs]\noutside=planted\n[window]\ngeometry=planted\n");
        settingslayer::activate(iniA);
        QStringList taken, refused;
        {
            QSettings s;
            for (const QString &k : {QStringLiteral("app/citationsApp"), QStringLiteral("app/sharedTemplates"), QStringLiteral("backup/dir"),
                                     QStringLiteral("tools/converterPath"), QStringLiteral("tools/serverUrl"), QStringLiteral("cs/outside"),
                                     QStringLiteral("window/geometry")})
                if (s.value(k).toString() == QLatin1String("planted"))
                    taken << k;
            if (s.value(QStringLiteral("app/forename")).toString() != QLatin1String("Planted"))
                refused << QStringLiteral("app/forename");
            if (s.value(QStringLiteral("app/autosave")).toString() != QLatin1String("77"))
                refused << QStringLiteral("app/autosave");
            if (s.value(QStringLiteral("backup/keep")).toString() != QLatin1String("9"))
                refused << QStringLiteral("backup/keep");
        }
        settingslayer::activate(QString());
        check(taken.isEmpty(), QStringLiteral("no program, folder, file, address or key outside the allow list is taken from a project (0.64.1)"),
              taken.join(QLatin1String(", ")));
        check(refused.isEmpty(), QStringLiteral("the keys of the allow list are taken from a project"), refused.join(QLatin1String(", ")));
    }
    note(QStringLiteral("program file %1 bytes before, %2 after").arg(progBefore.size()).arg(readText(settingslayer::programFile()).size()));
}

// ================================================================ S-W15 split and merge
void caseSW15()
{
    // Scrivener format first: a few splits, reopened in this process
    {
        MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
        if (Project *p = w->project()) {
            BinderItem *it = itemByTitle(p, QStringLiteral("Scene 12"));
            if (TextEditor *e = showDoc(w, it)) {
                const QString before = rawText(e->document());
                QTextCursor c(e->document());
                c.setPosition(before.size() / 2);
                e->setTextCursor(c);
                if (QAction *s = action(w, QStringLiteral("split")))
                    s->trigger();
                settle(500);
                const QString parts = rawText(p->docs()->text(it->id)) + allText(p, {textItems(p, it->parent).value(textItems(p, it->parent).indexOf(it) + 1)});
                check(sansSpace(parts) == sansSpace(before), QStringLiteral("Scrivener format: Split at Selection keeps every character"));
                const QString scriv = p->scrivenerPath;
                QString err;
                p->save(&err);
                const QJsonObject m = modelOf(p, false);
                w->closeProject();
                w->openScrivenerProject(scriv);
                settle(500);
                if (w->project())
                    compareModels(m, modelOf(w->project(), false), QStringLiteral("S-W15 Scrivener format, reopened after a split"));
            }
            w->closeProject();
        }
        delete w;
        settle(300);
    }
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    // ---- the Documents menu at different places
    struct Place {
        QString scene, what;
        std::function<int(QTextDocument *)> pos;
    };
    const QList<Place> places = {
        {QStringLiteral("Scene 2"), QStringLiteral("the middle of a paragraph"), [](QTextDocument *d) { return d->begin().next().position() + 10; }},
        {QStringLiteral("Scene 3"), QStringLiteral("a paragraph start"), [](QTextDocument *d) { return d->begin().next().next().position(); }},
        {QStringLiteral("Scene 4"), QStringLiteral("the very start"), [](QTextDocument *) { return 0; }},
        {QStringLiteral("Scene 6"), QStringLiteral("the very end"), [](QTextDocument *d) { return d->characterCount() - 1; }},
        {QStringLiteral("Scene 11"), QStringLiteral("inside a comment's span"), [](QTextDocument *d) {
             const QString t = d->begin().text();
             return int(t.indexOf(QLatin1Char(' '))) + 2;
         }},
        {QStringLiteral("Scene 5"), QStringLiteral("next to a picture"), [](QTextDocument *d) {
             for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                 for (auto f = b.begin(); !f.atEnd(); ++f)
                     if (f.fragment().charFormat().isImageFormat())
                         return f.fragment().position() + 1;
             return 5;
         }},
    };
    for (const Place &pl : places) {
        BinderItem *it = itemByTitle(p, pl.scene);
        TextEditor *e = showDoc(w, it);
        if (!e)
            continue;
        const QString before = rawText(e->document());
        const QString shapeBefore = tableShape(e->document());
        const QList<BinderItem *> siblingsBefore = textItems(p, it->parent);
        QTextCursor c(e->document());
        c.setPosition(qBound(0, pl.pos(e->document()), e->document()->characterCount() - 1));
        e->setTextCursor(c);
        step(QStringLiteral("Documents > Split > at Selection: %1").arg(pl.what));
        const bool chosen = chooseByMenu(w, QStringLiteral("split"));
        settle(500);
        const QList<BinderItem *> siblings = textItems(p, it->parent);
        QString parts;
        for (BinderItem *x : siblings)
            if (x == it || !siblingsBefore.contains(x))
                parts += rawText(p->docs()->text(x->id));
        fact(QStringLiteral("S-W15 split at %1").arg(pl.what), chosen ? QStringLiteral("%1 new document(s)").arg(siblings.size() - siblingsBefore.size())
                                                                      : QStringLiteral("not offered"));
        if (chosen)
            check(sansSpace(parts) == sansSpace(before), QStringLiteral("split at %1: no text lost or doubled").arg(pl.what),
                  QStringLiteral("%1 characters before, %2 after").arg(sansSpace(before).size()).arg(sansSpace(parts).size()));
        // what happened to notes, synopsis, label, keywords of the new part
        for (BinderItem *x : siblings)
            if (!siblingsBefore.contains(x))
                fact(QStringLiteral("S-W15 the new part after %1").arg(pl.what),
                     QStringLiteral("synopsis %1, notes %2, label %3 (original %4), keywords %5 (original %6)")
                         .arg(x->synopsis.isEmpty() ? QStringLiteral("empty") : QStringLiteral("set"))
                         .arg(p->docs()->notes(x->id)->isEmpty() ? QStringLiteral("empty") : QStringLiteral("set"))
                         .arg(x->label)
                         .arg(it->label)
                         .arg(x->keywords.join(QLatin1Char(',')), it->keywords.join(QLatin1Char(','))));
        // undo in the original document
        TextEditor *orig = showDoc(w, it);
        if (orig && chosen) {
            QTest::keyClick(orig, Qt::Key_Z, Qt::ControlModifier);
            settle(200);
            const bool back = rawText(orig->document()) == before && tableShape(orig->document()) == shapeBefore;
            fact(QStringLiteral("S-W15 undo after the split at %1").arg(pl.what), back ? QStringLiteral("the original text is back")
                                                                                         : QStringLiteral("the original text is not back"));
            if (back)   // redo keeps the files consistent with the split
                QTest::keyClick(orig, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            settle(100);
        }
    }
    // ---- 0.64.1: inside a table cell and a picture's caption the split is refused (a note, nothing changes);
    // at the paragraph right after a table the table stays whole in the first part
    {
        BinderItem *withTable = nullptr, *withFigure = nullptr;
        for (BinderItem *x : textItems(p)) {
            QTextDocument *d = p->docs()->text(x->id);
            for (QTextFrame *f : d->rootFrame()->childFrames()) {
                if (qobject_cast<QTextTable *>(f)) {
                    if (!withTable)
                        withTable = x;
                } else if (!withFigure) {
                    withFigure = x;
                }
            }
        }
        struct FramePlace {
            BinderItem *it;
            QString what;
            bool refused;   // refused, or split with the table whole
        };
        const QList<FramePlace> fps = {{withTable, QStringLiteral("inside a table cell"), true},
                                       {withFigure, QStringLiteral("inside a picture's caption"), true},
                                       {withTable, QStringLiteral("the paragraph right after a table"), false}};
        for (const FramePlace &fp : fps) {
            if (!fp.it) {
                note(QStringLiteral("(no document with a %1 in FX-1: %2 left out)").arg(fp.refused && fp.what.contains(QLatin1String("caption")) ? QStringLiteral("figure") : QStringLiteral("table"), fp.what));
                continue;
            }
            TextEditor *e = showDoc(w, fp.it);
            if (!e)
                continue;
            QTextDocument *d = e->document();
            QTextFrame *frame = nullptr;
            for (QTextFrame *f : d->rootFrame()->childFrames())
                if ((qobject_cast<QTextTable *>(f) != nullptr) == !fp.what.contains(QLatin1String("caption"))) {
                    frame = f;
                    break;
                }
            if (!frame)
                continue;
            int at = frame->firstPosition() + 2;
            if (fp.what.contains(QLatin1String("caption")))
                at = frame->lastPosition() - 1;
            else if (!fp.refused)
                at = frame->lastPosition() + 1;   // the block Qt keeps right after the frame
            QTextCursor c(d);
            c.setPosition(qBound(0, at, d->characterCount() - 1));
            e->setTextCursor(c);
            const QString before = rawText(d);
            const QString shapeBefore = tableShape(d);
            const QList<BinderItem *> siblingsBefore = textItems(p, fp.it->parent);
            step(QStringLiteral("Documents > Split > at Selection: %1").arg(fp.what));
            const bool chosen = chooseByMenu(w, QStringLiteral("split"));
            settle(500);
            const QList<BinderItem *> siblings = textItems(p, fp.it->parent);
            if (fp.refused) {
                check(siblings.size() == siblingsBefore.size() && rawText(p->docs()->text(fp.it->id)) == before,
                      QStringLiteral("split %1: refused, nothing changes (0.64.1)").arg(fp.what),
                      QStringLiteral("chosen %1, %2 new document(s)").arg(chosen).arg(siblings.size() - siblingsBefore.size()));
            } else {
                QString parts;
                for (BinderItem *x : siblings)
                    if (x == fp.it || !siblingsBefore.contains(x))
                        parts += rawText(p->docs()->text(x->id));
                const QString firstShape = tableShape(p->docs()->text(fp.it->id));
                check(chosen && sansSpace(parts) == sansSpace(before) && firstShape.contains(shapeBefore.section(QLatin1Char('\n'), 0, 0)),
                      QStringLiteral("split %1: no text lost, the table whole in the first part (0.64.1)").arg(fp.what),
                      firstShape.left(200));
            }
        }
    }
    // ---- Split with Selection as Title
    {
        BinderItem *it = itemByTitle(p, QStringLiteral("Scene 7"));
        if (TextEditor *e = showDoc(w, it)) {
            const QString before = rawText(e->document());
            const QList<BinderItem *> siblingsBefore = textItems(p, it->parent);
            QTextCursor c(e->document());
            c.setPosition(e->document()->begin().next().position());
            c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
            const QString sel = c.selectedText();
            e->setTextCursor(c);
            const bool chosen = chooseByMenu(w, QStringLiteral("splitTitle"));
            settle(500);
            BinderItem *added = nullptr;
            for (BinderItem *x : textItems(p, it->parent))
                if (!siblingsBefore.contains(x))
                    added = x;
            check(chosen && added && added->title == sel, QStringLiteral("Split with Selection as Title: the selection is the new title"),
                  added ? added->title : QStringLiteral("(no new document)"));
            if (added) {
                const QString parts = rawText(p->docs()->text(it->id)) + rawText(p->docs()->text(added->id));
                const bool same = sansSpace(parts) == sansSpace(before);
                const bool selectionGone = sansSpace(parts + sel) == sansSpace(before);
                fact(QStringLiteral("S-W15 Split with Selection as Title, the selected text"), same ? QStringLiteral("stays in the text")
                                                                                                     : selectionGone ? QStringLiteral("leaves the text (it is the title)")
                                                                                                                     : QStringLiteral("other"));
                check(same || selectionGone, QStringLiteral("Split with Selection as Title: no other text lost or doubled"));
            }
        }
    }
    // ---- Merge of three scenes with notes, synopses, labels, keywords, comments
    if (auto *bp = w->findChild<BinderPanel *>()) {
        QList<BinderItem *> three = {itemByTitle(p, QStringLiteral("Scene 13")), itemByTitle(p, QStringLiteral("Scene 14")),
                                     itemByTitle(p, QStringLiteral("Scene 15"))};
        three.removeAll(nullptr);
        if (three.size() == 3) {
            QString texts;
            for (BinderItem *x : three)
                texts += rawText(p->docs()->text(x->id));
            const QString notes = p->docs()->notes(three.at(1)->id)->toPlainText();
            bp->select(three, true);
            settle(200);
            step(QStringLiteral("Documents > Merge of three scenes"));
            // the program asks first ("Merge 3 documents into ...? The others are moved to the Trash.")
            withModal<QMessageBox>(300, [](QMessageBox *b) {
                fact(QStringLiteral("S-W15 the question before Merge"), b->text());
                if (QAbstractButton *yes = b->button(QMessageBox::Yes))
                    yes->click();
                else
                    b->accept();
            });
            const bool chosen = chooseByMenu(w, QStringLiteral("merge"));
            settle(600);
            BinderItem *merged = nullptr;
            for (BinderItem *x : three)
                if (p->item(x->id) && !p->isInTrash(x) && x->parent)
                    merged = merged ? merged : x;
            const QString got = merged ? rawText(p->docs()->text(merged->id)) : QString();
            check(chosen && merged && sansSpace(got) == sansSpace(texts), QStringLiteral("Merge of three scenes: the texts follow each other, nothing lost"),
                  QStringLiteral("%1 characters wanted, %2 found").arg(sansSpace(texts).size()).arg(sansSpace(got).size()));
            if (merged)
                fact(QStringLiteral("S-W15 Merge: notes of the second scene"), p->docs()->notes(merged->id)->toPlainText().contains(notes.left(40))
                                                                                    ? QStringLiteral("kept in the merged document")
                                                                                    : QStringLiteral("not in the merged document"));
        }
    }
    // ---- the right-click menu: Split at Selection, Split with Selection as Title, Open Link, 20 times each
    step(QStringLiteral("right-click menu: Split at Selection, with Selection as Title, Open Link"));
    BinderItem *scratch = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Right-click splits"));
    {
        QTextCursor c(p->docs()->text(scratch->id));
        for (int i = 0; i < 90; ++i) {
            if (i)
                c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
            c.insertText(QStringLiteral("Paragraph %1 of the scratch scene, with some words.").arg(i + 1));
        }
    }
    BinderItem *target = itemByTitle(p, QStringLiteral("Scene 1"));
    const QString scratchText = rawText(p->docs()->text(scratch->id));
    int splits = 0, titled = 0, links = 0, menusMissing = 0;
    BinderItem *current = scratch;
    auto editorFor = [&](int where) -> TextEditor * {
        // 0: the main editor; 1: the second editor of a split; 2: Combined Text of the Draft's last items
        if (where == 1) {
            if (QAction *sh = action(w, QStringLiteral("splitH")); sh && visiblePanes(w).size() < 2)
                sh->trigger();
            settle(300);
            const QList<EditorPane *> panes = visiblePanes(w);
            if (panes.size() >= 2) {
                panes.at(1)->showItems({current}, false, int(ViewMode::Single));
                settle(300);
                return panes.at(1)->currentEditor();
            }
        }
        if (where == 0 || where == 1) {
            if (QAction *ns = action(w, QStringLiteral("noSplit")); ns && visiblePanes(w).size() > 1)
                ns->trigger();
            settle(200);
            return showDoc(w, current);
        }
        EditorPane *pane = visiblePane(w);
        if (!pane)
            return nullptr;
        pane->showItems({current}, false, int(ViewMode::Combined));
        settle(400);
        return pane->currentEditor();
    };
    for (int i = 0; i < 60; ++i) {
        const int kind = i % 3;   // split, split with title, open link
        TextEditor *e = editorFor((i / 3) % 3);
        if (!e)
            continue;
        e->setFocus();
        QTextDocument *d = e->document();
        if (kind == 2) {
            // a document link at the start, right-clicked, Open Link
            QTextCursor lc(d);
            lc.movePosition(QTextCursor::End);
            // 0.64.2: a link as the program makes it (underlined, link color); a bare anchor is read back underlined
            const QTextCharFormat lf = text::linkFormat(QStringLiteral("qb://") + (target ? target->id : QString()));
            lc.insertText(QStringLiteral(" link"), lf);
            lc.insertText(QStringLiteral(" "), QTextCharFormat());
            const int at = lc.position() - 3;
            QStringList entries;
            const bool ok = rightClickChoose(e, at, QStringLiteral("Open Link"), &entries);
            settle(300);
            if (ok)
                ++links;
            else
                ++menusMissing;
            // back to the scratch part
            showDoc(w, current);
            continue;
        }
        QTextBlock b = d->begin();
        for (int k = 0; k < 2 && b.next().isValid(); ++k)
            b = b.next();
        QTextCursor c(d);
        c.setPosition(b.position());
        if (kind == 1)
            c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        const int before = int(textItems(p).size());
        const bool ok = rightClickChoose(e, -1, kind == 0 ? QStringLiteral("Split at Selection") : QStringLiteral("Split with Selection as Title"));
        settle(400);
        if (!ok) {
            ++menusMissing;
            continue;
        }
        (kind == 0 ? splits : titled)++;
        if (int(textItems(p).size()) > before) {
            // the newest part becomes the one to split next
            for (BinderItem *x : textItems(p))
                if (x->title.startsWith(QLatin1String("Paragraph")) || x->title.startsWith(QLatin1String("Right-click")) || x->parent == scratch->parent)
                    if (x != scratch && x->id != current->id && !x->title.startsWith(QLatin1String("Scene")))
                        current = x;
        }
    }
    if (QAction *ns = action(w, QStringLiteral("noSplit")))
        ns->trigger();
    settle(300);
    fact(QStringLiteral("S-W15 right-click menu"), QStringLiteral("%1 splits, %2 splits with title, %3 links opened, %4 entries not found or off")
                                                       .arg(splits).arg(titled).arg(links).arg(menusMissing));
    check(splits >= 15 && titled >= 15 && links >= 15,
          QStringLiteral("right-click menu in the main editor, the second editor and Combined Text: the splits and Open Link run (crash of 0.60.5 fixed)"),
          QStringLiteral("%1 / %2 / %3").arg(splits).arg(titled).arg(links));
    // every paragraph of the scratch scene is still somewhere
    {
        QString all;
        for (BinderItem *x : textItems(p))
            all += rawText(p->docs()->text(x->id)) + QLatin1Char(' ') + x->title + QLatin1Char(' ');
        QStringList lost;
        for (int i = 1; i <= 90; ++i)
            if (!all.contains(QStringLiteral("Paragraph %1 of").arg(i)) && !all.contains(QStringLiteral("Paragraph %1").arg(i)))
                lost << QString::number(i);
        check(lost.isEmpty(), QStringLiteral("after the right-click splits every paragraph of the scratch scene is in a document or a title"),
              lost.join(QLatin1Char(',')));
        Q_UNUSED(scratchText);
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-W18 quote conversion
namespace {
struct QuoteCount {
    int marks = 0;
    int documents = 0;
};
QuoteCount predict(Project *p, bool typographic, const QString &style)
{
    QuoteCount q;
    for (BinderItem *it : p->allItems()) {
        if (!it->parent || p->isInTrash(it))
            continue;
        int n = int(autocorrect::convertQuotes(it->title, typographic, style).size()) + int(autocorrect::convertQuotes(it->synopsis, typographic, style).size());
        if (it->hasText()) {
            QTextDocument *d = p->docs()->text(it->id);
            for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
                n += int(autocorrect::convertQuotes(b.text(), typographic, style).size());
                QString lastNote;
                for (auto f = b.begin(); !f.atEnd(); ++f) {
                    const QTextCharFormat cf = f.fragment().charFormat();
                    if (isLinkedNote(NoteKind(cf.intProperty(props::NoteKind)))) {
                        const QString id = cf.stringProperty(props::NoteId);
                        if (id != lastNote)
                            n += int(autocorrect::convertQuotes(cf.stringProperty(props::NoteText), typographic, style).size());
                        lastNote = id;
                    }
                }
            }
            QTextDocument *nd = p->docs()->notes(it->id);
            for (QTextBlock b = nd->begin(); b.isValid(); b = b.next())
                n += int(autocorrect::convertQuotes(b.text(), typographic, style).size());
        }
        q.marks += n;
        if (n)
            ++q.documents;
    }
    return q;
}

QString runsWithoutMarks(QTextDocument *d)
{
    // the format runs, with every quote character counted as one neutral character
    QString out;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        out += QStringLiteral("B%1|").arg(b.blockFormat().stringProperty(props::Style));
        for (auto f = b.begin(); !f.atEnd(); ++f) {
            const QTextCharFormat cf = f.fragment().charFormat();
            out += QStringLiteral("%1%2%3:%4;").arg(cf.fontWeight() > QFont::Normal).arg(cf.fontItalic()).arg(cf.intProperty(props::NoteKind)).arg(f.fragment().length());
        }
    }
    return out;
}

ReplaceResult convertThroughOptions(MainWindow *w, bool typographic, const QString &style, const QList<BinderItem *> &selection,
                                    bool cancelAfterCount, ReplaceResult *counted)
{
    ReplaceResult done;
    OptionsDialog dlg(w, 0);
    dlg.showSection(OptionsDialog::SCorrections);
    dlg.show();
    settle(300);
    if (auto *qs = dlg.findChild<QComboBox *>(QStringLiteral("optQuoteStyle")))
        qs->setCurrentIndex(qs->findData(style));
    auto *button = dlg.findChild<QPushButton *>(typographic ? QStringLiteral("optConvertQuotesSmart") : QStringLiteral("optConvertQuotesStraight"));
    bool opened = false;
    withModal<ConvertQuotesDialog>(400, [&](ConvertQuotesDialog *d) {
        opened = true;
        if (!selection.isEmpty()) {
            d->setSelection(selection);
            // the scope "Selected documents only" is a choice of its own in the window
            for (QRadioButton *rb : d->findChildren<QRadioButton *>())
                if (rb->text().remove(QLatin1Char('&')).startsWith(QLatin1String("Selected documents")))
                    rb->click();
        }
        const ReplaceResult c = d->count();
        if (counted)
            *counted = c;
        shot(d, QStringLiteral("convert-quotes"));
        if (!cancelAfterCount)
            done = d->convert(false);
        d->reject();
    });
    if (button)
        button->click();
    settle(500);
    dlg.reject();
    settle(200);
    check(button && opened, QStringLiteral("File > Options > Corrections: the convert button opens its window"));
    return done;
}
} // namespace

void caseSW18()
{
    // Scrivener format: converted and reopened
    {
        MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
        if (Project *p = w->project()) {
            const QuoteCount want = predict(p, true, QStringLiteral("en"));
            ReplaceResult counted;
            const ReplaceResult done = convertThroughOptions(w, true, QStringLiteral("en"), {}, false, &counted);
            check(done.total() == want.marks, QStringLiteral("Scrivener format: the conversion changes as many marks as predicted (%1)").arg(want.marks),
                  QString::number(done.total()));
            const QString scriv = p->scrivenerPath;
            QString err;
            p->save(&err);
            const QJsonObject m = modelOf(p, true);
            w->closeProject();
            w->openScrivenerProject(scriv);
            settle(500);
            if (w->project())
                compareModels(m, modelOf(w->project(), true), QStringLiteral("S-W18 Scrivener format, reopened after the conversion"));
            w->closeProject();
        }
        delete w;
        settle(300);
    }
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    // what each text holds before
    QHash<QString, QString> rawBefore, runsBefore;
    QHash<QString, int> snapsBefore;
    for (BinderItem *it : textItems(p, p->root())) {
        rawBefore.insert(it->id, rawText(p->docs()->text(it->id)));
        runsBefore.insert(it->id, runsWithoutMarks(p->docs()->text(it->id)));
        snapsBefore.insert(it->id, int(snapshots::list(p, it->id).size()));
    }
    const QuoteCount want = predict(p, true, QStringLiteral("en"));
    note(QStringLiteral("predicted: %1 marks in %2 documents").arg(want.marks).arg(want.documents));
    // Cancel after the count changes nothing
    step(QStringLiteral("Convert to Typographic Quotes, counted, cancelled"));
    ReplaceResult counted;
    convertThroughOptions(w, true, QStringLiteral("en"), {}, true, &counted);
    bool unchanged = true;
    for (auto it = rawBefore.begin(); it != rawBefore.end(); ++it)
        unchanged = unchanged && rawText(p->docs()->text(it.key())) == it.value();
    check(unchanged && counted.total() == want.marks, QStringLiteral("Count says what will change (%1); Cancel changes nothing").arg(counted.total()),
          QStringLiteral("counted %1, predicted %2").arg(counted.total()).arg(want.marks));
    // the conversion, the whole project, every place
    step(QStringLiteral("Convert to Typographic Quotes, English, the whole project"));
    const ReplaceResult done = convertThroughOptions(w, true, QStringLiteral("en"), {}, false, nullptr);
    check(done.total() == want.marks, QStringLiteral("the conversion changes the predicted marks (%1)").arg(want.marks), QString::number(done.total()));
    int formatChanged = 0, lengthChanged = 0, missingSnapshots = 0;
    QStringList changedIds;
    for (auto it = rawBefore.begin(); it != rawBefore.end(); ++it) {
        QTextDocument *d = p->docs()->text(it.key());
        const QString now = rawText(d);
        if (now == it.value())
            continue;
        changedIds << it.key();
        if (now.size() != it.value().size())
            ++lengthChanged;
        if (runsWithoutMarks(d) != runsBefore.value(it.key()))
            ++formatChanged;
        if (int(snapshots::list(p, it.key()).size()) <= snapsBefore.value(it.key()))
            ++missingSnapshots;
    }
    check(lengthChanged == 0 && formatChanged == 0, QStringLiteral("only the marks change: lengths and formatting stay (%1 texts changed)").arg(changedIds.size()),
          QStringLiteral("%1 texts changed length, %2 changed formatting").arg(lengthChanged).arg(formatChanged));
    check(missingSnapshots == 0, QStringLiteral("a snapshot of every changed text"), QString::number(missingSnapshots));
    // roll one back from its snapshot
    if (!changedIds.isEmpty()) {
        const QString id = changedIds.first();
        const QList<Snapshot> list = snapshots::list(p, id);
        if (!list.isEmpty()) {
            snapshots::rollBack(p, list.first());
            settle(200);
            check(rawText(p->docs()->text(id)) == rawBefore.value(id), QStringLiteral("rolling back the snapshot of the conversion gives the text before"));
        }
    }
    // the German style, then the selected documents back to straight marks
    step(QStringLiteral("German style; then Convert to Straight Quotes for two selected documents"));
    convertThroughOptions(w, false, QStringLiteral("en"), {}, false, nullptr);
    convertThroughOptions(w, true, QStringLiteral("de"), {}, false, nullptr);
    BinderItem *s2 = itemByTitle(p, QStringLiteral("Scene 2"));
    BinderItem *s3 = itemByTitle(p, QStringLiteral("Scene 3"));
    BinderItem *s4 = itemByTitle(p, QStringLiteral("Scene 4"));
    const QString s4Before = s4 ? rawText(p->docs()->text(s4->id)) : QString();
    if (s2 && s3) {
        convertThroughOptions(w, false, QStringLiteral("de"), {s2, s3}, false, nullptr);
        const bool straight = !rawText(p->docs()->text(s2->id)).contains(QChar(0x201E)) && !rawText(p->docs()->text(s3->id)).contains(QChar(0x201E));
        check(straight && (!s4 || rawText(p->docs()->text(s4->id)) == s4Before),
              QStringLiteral("scope \"selected documents\": only those two change"),
              QStringLiteral("the two straight: %1; scene 4 unchanged: %2").arg(straight).arg(!s4 || rawText(p->docs()->text(s4->id)) == s4Before));
    }
    // typing quote marks in the German style
    {
        AppSettings &as = AppSettings::instance();
        as.smartQuotes = true;
        as.quoteStyle = QStringLiteral("de");
        as.save();
        BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Typed quotes"));
        if (TextEditor *e = showDoc(w, it)) {
            keysInto(e, QStringLiteral("\"Hallo\" sagte sie, 'ja' und Peter's Haus."));
            settle(100);
            const QString t = e->document()->toPlainText();
            fact(QStringLiteral("S-W18 typed in the German style"), t);
            check(t.startsWith(QStringLiteral("„Hallo“")), QStringLiteral("typing \" in the German style gives „...“"), t);
            if (t.contains(QStringLiteral("‚ja’")))
                note(QStringLiteral("known since 0.61.1: ' after a letter in the German style gives ’ where a closing ‘ belongs"));
        }
        as.smartQuotes = false;
        as.save();
    }
    // the large project: time and feedback (U-10)
    finishProject(w);
    delete w;
    {
        MainWindow *big = openWindow(fixtureCopy(QStringLiteral("fx2-qb")));
        if (big->project()) {
            QElapsedTimer t;
            t.start();
            ReplaceResult c2;
            convertThroughOptions(big, true, QStringLiteral("en"), {}, false, &c2);
            fact(QStringLiteral("S-W18 conversion over FX-2 (380,000 words, RTF)"), QStringLiteral("%1 s, %2 marks").arg(t.elapsed() / 1000.0, 0, 'f', 1).arg(c2.total()));
            big->closeProject();
        }
        delete big;
    }
}

// ================================================================ S-I11 Delete Old Snapshots
namespace {
int scrivSnapshotFiles(const QString &scriv)
{
    int n = 0;
    QDirIterator it(scriv + QStringLiteral("/Snapshots"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileName() != QLatin1String("index.xml"))
            ++n;
    }
    return n;
}

void deleteOld(MainWindow *w, const QString &answer, QString *question)
{
    withModal<QMessageBox>(400, [answer, question](QMessageBox *b) {
        if (question)
            *question = b->text() + QLatin1Char(' ') + b->informativeText();
        shot(b, QStringLiteral("delete-snapshots-") + answer);
        if (answer == QLatin1String("esc"))
            QTest::keyClick(b, Qt::Key_Escape);
        else if (answer == QLatin1String("enter"))
            QTest::keyClick(b, Qt::Key_Return);
        else
            for (QAbstractButton *x : b->buttons())
                if (x->text().remove(QLatin1Char('&')) == QLatin1String("Delete Snapshots"))
                    x->click();
    });
    chooseByMenu(w, QStringLiteral("deleteSnapshots"));
    settle(500);
}
} // namespace

void caseSI11()
{
    // Scrivener format
    {
        MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
        if (Project *p = w->project()) {
            QString err;
            for (int i = 1; i <= 3; ++i)
                if (BinderItem *it = itemByTitle(p, QStringLiteral("Scene %1").arg(i)))
                    snapshots::take(p, it->id, QStringLiteral("In Scrivener format %1").arg(i), &err);
            p->save(&err);
            const QString scriv = p->scrivenerPath;
            // a snapshot only Scrivener has: a copy of a snapshot file under a name the program never read
            QString planted;
            {
                QDirIterator it(scriv + QStringLiteral("/Snapshots"), QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString f = it.next();
                    if (it.fileName().endsWith(QLatin1String(".rtf"))) {
                        planted = QFileInfo(f).absolutePath() + QStringLiteral("/2001-01-01-00-00-00+0000.rtf");
                        QFile::copy(f, planted);
                        break;
                    }
                }
            }
            const int before = scrivSnapshotFiles(scriv);
            deleteOld(w, QStringLiteral("ok"), nullptr);
            const int after = scrivSnapshotFiles(scriv);
            fact(QStringLiteral("S-I11 snapshot files in the .scriv"), QStringLiteral("%1 before, %2 after").arg(before).arg(after));
            check(after < before && (planted.isEmpty() || QFileInfo::exists(planted)),
                  QStringLiteral("Scrivener format: deleted snapshots leave the .scriv; one only Scrivener has stays"),
                  planted.isEmpty() ? QStringLiteral("(no snapshot file to copy)") : QString());
            w->closeProject();
        }
        delete w;
        settle(300);
    }
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QString err;
    // a snapshot of a document then moved to the Trash, and one left from a document no longer there
    if (BinderItem *it = itemByTitle(p, QStringLiteral("Scene 33"))) {
        snapshots::take(p, it->id, QStringLiteral("Before the Trash"), &err);
        if (auto *bp = w->findChild<BinderPanel *>()) {
            bp->select({it}, true);
            settle(200);
            if (QAction *tr = action(w, QStringLiteral("trash")))
                tr->trigger();
            settle(300);
        }
    }
    {
        const QString orphan = p->snapshotsDir(QStringLiteral("cs-gone-document"));
        QDir().mkpath(orphan);
        writeText(orphan + QStringLiteral("/2020-01-01-10-00-00.md"), "An old snapshot of a document no longer there.\n");
    }
    QHash<QString, QString> texts;
    for (BinderItem *it : textItems(p, p->root()))
        texts.insert(it->id, rawText(p->docs()->text(it->id)));
    const snapshots::Totals before = snapshots::totals(p);
    QString question;
    deleteOld(w, QStringLiteral("esc"), &question);
    const int afterEsc = snapshots::totals(p).snapshots;
    deleteOld(w, QStringLiteral("enter"), nullptr);
    const int afterEnter = snapshots::totals(p).snapshots;
    note(QStringLiteral("question: %1").arg(question));
    check(question.contains(QString::number(before.snapshots)) && question.contains(QRegularExpression(QStringLiteral("\\d+(\\.\\d+)? ?(kB|MB|GB|bytes)"))),
          QStringLiteral("the question names the number (%1) and the size of the snapshots").arg(before.snapshots), question);
    check(afterEsc == before.snapshots && afterEnter == before.snapshots, QStringLiteral("Esc and Enter keep them (Cancel is the default)"));
    deleteOld(w, QStringLiteral("ok"), nullptr);
    const snapshots::Totals after = snapshots::totals(p);
    QStringList changed;
    for (auto it = texts.begin(); it != texts.end(); ++it)
        if (rawText(p->docs()->text(it.key())) != it.value())
            changed << it.key();
    check(after.snapshots == 0, QStringLiteral("after OK no snapshot is left (also of the Trash and of a document no longer there)"),
          QString::number(after.snapshots));
    check(changed.isEmpty(), QStringLiteral("the texts stay as they are"), changed.join(QLatin1String(", ")));
    check(!p->hasUnsavedChanges(), QStringLiteral("the project was saved in full at once"));
    // a new snapshot afterwards
    if (BinderItem *it = itemByTitle(p, QStringLiteral("Scene 1"))) {
        snapshots::take(p, it->id, QStringLiteral("A new one"), &err);
        check(snapshots::list(p, it->id).size() == 1, QStringLiteral("a new snapshot works afterwards"));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-L06 settings for one project
void caseSL06()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QString first = p->indexPath();
    const QString ini = settingslayer::fileFor(p->settingsDir());
    const int ownBefore = OptionsDialog::projectValueCount();
    AppSettings &as = AppSettings::instance();
    const QString programQuote = as.quoteStyle;
    // File > Options in "This project": ten settings on different pages
    QStringList touched;
    {
        // 0.70.0 (audit A S-05): Options opens for all projects; the writer picks "This project" (as the menu's
        // scope list does)
        OptionsDialog::setNextScope(OptionsDialog::ThisProject);
        OptionsDialog dlg(w);
        dlg.show();
        settle(300);
        auto *scope = dlg.findChild<QComboBox *>(QStringLiteral("optScope"));
        check(scope && dlg.optionScope() == OptionsDialog::ThisProject, QStringLiteral("Options in \"This project\" with a project open"));
        for (const char *name : {"optSnapshotOnSave", "optBlockCursor", "optBackLinks", "optSubdocsAsFolders", "optCapI", "optBackupManual"})
            if (auto *cb = dlg.findChild<QCheckBox *>(QLatin1String(name))) {
                cb->setChecked(!cb->isChecked());
                touched << QLatin1String(name);
            }
        for (const char *name : {"optNewAlign", "optToolbarIcons", "optSpellUnderline"})
            if (auto *cb = dlg.findChild<QComboBox *>(QLatin1String(name)); cb && cb->count() > 1) {
                cb->setCurrentIndex((cb->currentIndex() + 1) % cb->count());
                touched << QLatin1String(name);
            }
        if (auto *qs = dlg.findChild<QComboBox *>(QStringLiteral("optQuoteStyle"))) {
            qs->setCurrentIndex(qs->findData(QStringLiteral("fr")));
            touched << QStringLiteral("optQuoteStyle");
        }
        // a program-only setting changed in project scope
        if (auto *fs = dlg.findChild<QSpinBox *>(QStringLiteral("optFreezeSeconds"))) {
            fact(QStringLiteral("S-L06 the freeze watchdog's time in \"This project\""), fs->isEnabled() ? QStringLiteral("offered") : QStringLiteral("not offered"));
            if (fs->isEnabled()) {
                fs->setValue(fs->maximum());
                touched << QStringLiteral("optFreezeSeconds");
            }
        }
        for (const char *name : {"optionsDefaults", "optionsManage"})
            if (auto *b = dlg.findChild<QWidget *>(QLatin1String(name)))
                fact(QStringLiteral("S-L06 %1 in project scope").arg(QLatin1String(name)), b->isVisible() && b->isEnabled() ? QStringLiteral("offered") : QStringLiteral("not offered"));
        dlg.apply();
        shot(&dlg, QStringLiteral("options-this-project"));
        if (auto *info = dlg.findChild<QLabel *>(QStringLiteral("optScopeInfo")))
            fact(QStringLiteral("S-L06 the line below the choice"), info->text());
        dlg.reject();
    }
    settle(300);
    const int ownAfter = OptionsDialog::projectValueCount();
    note(QStringLiteral("changed: %1; own values %2 -> %3").arg(touched.join(QLatin1String(", "))).arg(ownBefore).arg(ownAfter));
    check(ownAfter >= ownBefore + 8, QStringLiteral("the changed settings are kept with the project"), QStringLiteral("%1 -> %2").arg(ownBefore).arg(ownAfter));
    {
        QSettings s(ini, QSettings::IniFormat);
        QStringList programKeys;
        for (const QString &k : s.allKeys())
            if (settingslayer::programOnly(k))
                programKeys << k;
        check(programKeys.isEmpty(), QStringLiteral("no program-only setting reaches the project's file"), programKeys.join(QLatin1String(", ")));
    }
    check(as.quoteStyle == QLatin1String("fr"), QStringLiteral("the project's value is used while it is open"), as.quoteStyle);
    // another project: the program's values
    const QString second = ctx().work + QStringLiteral("/second");
    QDir().mkpath(second);
    QString err;
    {
        auto b = Project::create(second, QStringLiteral("Second"), &err);
        if (b) {
            b->save(&err);
            const QString secondFile = b->indexPath();
            b.reset();
            check(w->openProject(secondFile), QStringLiteral("(the other project opens)"));
            settle(500);
            check(as.quoteStyle == programQuote, QStringLiteral("the other project uses the program's values"), as.quoteStyle);
            check(w->openProject(first), QStringLiteral("(back to the first project)"));
            settle(500);
            check(as.quoteStyle == QLatin1String("fr"), QStringLiteral("back in the first project its values apply again"), as.quoteStyle);
        }
    }
    // a change in "All projects" to a setting the project has its own value for
    {
        // Apply, then Cancel; the window goes (its destructor puts the project's values on top again)
        OptionsDialog::setNextScope(OptionsDialog::AllProjects);
        {
            OptionsDialog dlg(w);
            dlg.show();
            settle(200);
            if (auto *qs = dlg.findChild<QComboBox *>(QStringLiteral("optQuoteStyle")))
                qs->setCurrentIndex(qs->findData(QStringLiteral("sv")));
            dlg.apply();
            dlg.reject();
        }
        settle(300);
        QSettings prog(settingslayer::programFile(), QSettings::IniFormat);
        check(as.quoteStyle == QLatin1String("fr") && prog.value(QStringLiteral("editing/quoteStyle")).toString() == QLatin1String("sv"),
              QStringLiteral("All projects: the program gets the new value, the project keeps its own"),
              as.quoteStyle + QLatin1Char('/') + prog.value(QStringLiteral("editing/quoteStyle")).toString());
    }
    // the switch: the program's settings always, then back
    {
        OptionsDialog::setNextScope(OptionsDialog::AllProjects);
        {
            OptionsDialog dlg(w);
            dlg.show();
            settle(200);
            if (auto *pf = dlg.findChild<QCheckBox *>(QStringLiteral("optProjectFirst")))
                pf->setChecked(false);
            dlg.apply();
            dlg.reject();
        }
        settle(300);
        check(as.quoteStyle == QLatin1String("sv") && QFileInfo::exists(ini), QStringLiteral("the program's settings always: the project's values are not used, its file stays"),
              as.quoteStyle);
        OptionsDialog::setNextScope(OptionsDialog::AllProjects);
        {
            OptionsDialog dlg2(w);
            dlg2.show();
            settle(200);
            if (auto *pf = dlg2.findChild<QCheckBox *>(QStringLiteral("optProjectFirst")))
                pf->setChecked(true);
            dlg2.apply();
            dlg2.reject();
        }
        settle(300);
        check(as.quoteStyle == QLatin1String("fr"), QStringLiteral("back to a project's own settings first: its values again"), as.quoteStyle);
    }
    // the project settings file with the section Options of This Project, loaded into another project
    {
        const QString file = ctx().work + QStringLiteral("/settings.qbsettings");
        check(settingsfile::save(file, w->project(), &err), QStringLiteral("Save Project Settings to File"), err);
        QJsonObject doc;
        settingsfile::load(file, &doc, &err);
        // the sections of a settings file are under "sections"
        const QJsonValue opts = doc.value(QStringLiteral("sections")).toObject().value(settingsfile::key(settingsfile::Options));
        check(!opts.isUndefined() && !opts.toObject().isEmpty(), QStringLiteral("the file has the section Options of This Project"));
        auto b = Project::open(second + QStringLiteral("/Second.quillbinder"), &err);
        if (!b) {
            const QString qbx = projectFileIn(second);   // 0.64.2: not the spare copy in settings
            if (!qbx.isEmpty())
                b = Project::open(qbx, &err);
        }
        if (b) {
            QStringList notes;
            settingsfile::applyData(b.get(), settingsfile::Options, opts, &notes);
            QSettings bs(settingslayer::fileFor(b->settingsDir()), QSettings::IniFormat);
            check(bs.value(QStringLiteral("editing/quoteStyle")).toString() == QLatin1String("fr"),
                  QStringLiteral("loaded into another project: it gets the options"), notes.join(QLatin1String("; ")));
        }
    }
    // Use Program Settings
    {
        OptionsDialog dlg(w);
        dlg.show();
        settle(200);
        withModal<QMessageBox>(300, [](QMessageBox *b) {
            for (QAbstractButton *x : b->buttons())
                if (b->buttonRole(x) == QMessageBox::YesRole || b->buttonRole(x) == QMessageBox::AcceptRole)
                    x->click();
        });
        if (auto *reset = dlg.findChild<QAbstractButton *>(QStringLiteral("optScopeReset")))
            reset->click();
        settle(400);
        dlg.reject();
        settle(300);
        check(OptionsDialog::projectValueCount() == 0, QStringLiteral("Use Program Settings removes the project's own values"),
              QString::number(OptionsDialog::projectValueCount()));
        check(as.quoteStyle == QLatin1String("sv"), QStringLiteral("... and the program's values apply"), as.quoteStyle);
    }
    // 0.64.1: Load Options from File with programs and places in it asks first (loadOptionsPlaces);
    // refused, the other settings are taken and those keys are not; agreed, all are taken
    {
        step(QStringLiteral("Load Options from File with a program and a folder in it"));
        const QString file = ctx().work + QStringLiteral("/someone-elses.ini");
        const QString evilApp = QStringLiteral("/usr/bin/false-citations");
        const QString evilDir = ctx().work + QStringLiteral("/someone-elses-backups");
        auto writeFile = [&](const QString &quote) {
            QFile::remove(file);
            QSettings f(file, QSettings::IniFormat);
            f.setValue(QStringLiteral("quillbinderOptions/version"), 1);
            f.setValue(QStringLiteral("app/citationsApp"), evilApp);
            f.setValue(QStringLiteral("backup/dir"), evilDir);
            f.setValue(QStringLiteral("editing/quoteStyle"), quote);
            f.sync();
        };
        for (int answer = 0; answer < 2; ++answer) {
            writeFile(answer == 0 ? QStringLiteral("fr") : QStringLiteral("de"));
            OptionsDialog dlg(w);
            QString asked, name;
            withModal<QMessageBox>(300, [answer, &asked, &name](QMessageBox *b) {
                name = b->objectName();
                asked = b->text();
                (answer == 0 ? b->button(QMessageBox::No) : b->button(QMessageBox::Yes))->click();
            });
            QString err;
            const bool ok = dlg.loadOptionsFromFile(file, &err);
            settle(300);
            QSettings s;
            const QString app = s.value(QStringLiteral("app/citationsApp")).toString();
            const QString dir = s.value(QStringLiteral("backup/dir")).toString();
            const QString quote = s.value(QStringLiteral("editing/quoteStyle")).toString();
            fact(QStringLiteral("S-L06 Load Options with places, answer %1").arg(answer == 0 ? QStringLiteral("No") : QStringLiteral("Yes")),
                 QStringLiteral("question %1; citations %2, backup folder %3, quotes %4").arg(name, app, dir, quote));
            check(ok && name == QLatin1String("loadOptionsPlaces") && asked.contains(evilApp) && asked.contains(evilDir),
                  QStringLiteral("Load Options asks first and lists the program and the folder (0.64.1)"), err + asked.left(200));
            if (answer == 0)
                check(app != evilApp && dir != evilDir && quote == QLatin1String("fr"),
                      QStringLiteral("refused: the other settings are taken, the program and the folder are not"), QStringLiteral("%1 | %2 | %3").arg(app, dir, quote));
            else
                check(app == evilApp && dir == evilDir && quote == QLatin1String("de"), QStringLiteral("agreed: all are taken"),
                      QStringLiteral("%1 | %2 | %3").arg(app, dir, quote));
        }
        // back to what the case had, so that nothing else of the run starts this program
        QSettings s;
        s.remove(QStringLiteral("app/citationsApp"));
        s.remove(QStringLiteral("backup/dir"));
        s.sync();
        AppSettings::instance().load();
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-R01 recording with the fake engine
namespace {
QObject *speechTool()
{
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        if (t->id() == QLatin1String("speech-transcription"))
            return dynamic_cast<QObject *>(t);
    return nullptr;
}
bool callBool(QObject *o, const char *slot)
{
    bool r = false;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(bool, r));
    return r;
}
QString callString(QObject *o, const char *slot)
{
    QString r;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(QString, r));
    return r;
}
// 16 kHz mono: loud noise with a tone for speech, very quiet noise for pauses (milliseconds)
QVector<float> makeAudio(const QList<QPair<bool, int>> &parts)
{
    QVector<float> out;
    std::mt19937 r(7);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    for (const auto &p : parts)
        for (int i = 0; i < p.second * 16; ++i)
            out << (p.first ? 0.35f * u(r) + 0.3f * std::sin(2 * float(M_PI) * 220.f * float(i) / 16000.f) : 0.002f * u(r));
    return out;
}
QByteArray pcm16(const QVector<float> &s)
{
    QByteArray pcm(int(s.size()) * 2, Qt::Uninitialized);
    for (int i = 0; i < s.size(); ++i)
        qToLittleEndian(qint16(qBound(-32767, int(s[i] * 32767), 32767)), pcm.data() + 2 * i);
    return pcm;
}
void writeWav(const QString &file, const QVector<float> &s)
{
    const QByteArray pcm = pcm16(s);
    QByteArray h;
    auto u32 = [&h](quint32 v) { h.append(char(v & 0xff)).append(char((v >> 8) & 0xff)).append(char((v >> 16) & 0xff)).append(char(v >> 24)); };
    auto u16 = [&h](quint16 v) { h.append(char(v & 0xff)).append(char(v >> 8)); };
    h += "RIFF";
    u32(quint32(36 + pcm.size()));
    h += "WAVEfmt ";
    u32(16);
    u16(1);
    u16(1);
    u32(16000);
    u32(32000);
    u16(2);
    u16(16);
    h += "data";
    u32(quint32(pcm.size()));
    writeText(file, h + pcm);
}
QList<QPair<bool, int>> pattern(int seconds)
{
    QList<QPair<bool, int>> p;
    int ms = 0, k = 0;
    while (ms < seconds * 1000) {
        const int speak = 1200 + (k % 4) * 400;
        const int pause = (k % 5 == 4) ? 3500 : 1000;
        p << qMakePair(true, speak) << qMakePair(false, pause);
        ms += speak + pause;
        ++k;
    }
    return p;
}
int pieces(const QString &text)
{
    return int(text.count(QRegularExpression(QStringLiteral("Piece \\d+"))));
}
bool piecesInOrder(const QString &text)
{
    int last = 0;
    QRegularExpressionMatchIterator it = QRegularExpression(QStringLiteral("Piece (\\d+)")).globalMatch(text);
    while (it.hasNext()) {
        const int n = it.next().captured(1).toInt();
        if (n != last + 1)
            return false;
        last = n;
    }
    return true;
}
} // namespace

void caseSR01()
{
    qputenv("QB_SPEECH_FAKE", "1");
    QObject *tool = speechTool();
    check(tool != nullptr, QStringLiteral("(the speech tool is loaded)"));
    if (!tool)
        return;
    const QString model = ctx().work + QStringLiteral("/fake-model.bin");
    writeText(model, QByteArray(1024, 'm'));
    toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("modelFile"), model);
    toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("model"), QStringLiteral("small"));
    const QString shortWav = ctx().work + QStringLiteral("/short.wav");
    const QString longWav = ctx().work + QStringLiteral("/long.wav");
    writeWav(shortWav, makeAudio(pattern(20)));
    writeWav(longWav, makeAudio(pattern(150)));
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QJsonObject projectBefore = modelOf(p, false);
    QAction *rec = action(w, QStringLiteral("tool:speech-transcription.record"));
    check(rec != nullptr, QStringLiteral("the microphone button is on the main toolbar"));
    auto waitIdle = [tool](int ms) { return waitFor([tool] { return !callBool(tool, "isBusy") && !callBool(tool, "isRecording"); }, ms); };
    // start and stop 50 times quickly with the button (the default input: the null sink's monitor)
    step(QStringLiteral("the button: start and stop 50 times"));
    if (rec) {
        for (int i = 0; i < 100; ++i) {
            rec->trigger();
            settle(20 + int(i % 7) * 15);
        }
        if (callBool(tool, "isRecording"))
            rec->trigger();
        check(waitIdle(60000), QStringLiteral("50 quick starts and stops: the tool comes to rest"), callString(tool, "lastError"));
    }
    // a recording while typing, switching documents, changing tabs
    step(QStringLiteral("a recording while typing and switching documents"));
    BinderItem *typed = itemByTitle(p, QStringLiteral("Scene 20"));
    BinderItem *other = itemByTitle(p, QStringLiteral("Scene 21"));
    QString typedModel;
    {
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, shortWav));
        const QString file = callString(tool, "recordingFile");
        check(started, QStringLiteral("a recording starts"), callString(tool, "lastError"));
        if (TextEditor *e = showDoc(w, typed))
            typedModel = typeLikeWriter(e, atParagraphs(77, 120).join(QLatin1Char('\n')), 77);
        showDoc(w, other);
        settle(300);
        TextEditor *back = showDoc(w, typed);
        check(back && rawText(back->document()) == typedModel, QStringLiteral("the typed text is all there after switching documents during the recording"));
        waitIdle(60000);
        const QString text = readText(file);
        check(pieces(text) >= 5 && piecesInOrder(text), QStringLiteral("every piece of the recording is in its file, in order (%1)").arg(pieces(text)), text.left(300));
    }
    // the typing gates while recording
    {
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, longWav));
        if (TextEditor *e = showDoc(w, typed)) {
            QTextCursor c(e->document());
            c.movePosition(QTextCursor::End);
            e->setTextCursor(c);
            for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
                typingGate(e, profile, 60, QStringLiteral("S-R01 main editor while recording"));
            typedModel = rawText(e->document());
        }
        QMetaObject::invokeMethod(tool, "stopRecording", Qt::DirectConnection);
        waitIdle(60000);
    }
    // the Transcriptions tab: rename, copy, delete, also the one being written; typing into it
    step(QStringLiteral("the Transcriptions tab during a recording"));
    QObject *panel = w->findChild<QWidget *>(QStringLiteral("speechPanel"));
    if (panel) {
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, shortWav));
        settle(1500);
        auto *edit = qobject_cast<QPlainTextEdit *>(w->findChild<QPlainTextEdit *>(QStringLiteral("speechText")));
        if (edit) {
            const QString before = edit->toPlainText();
            edit->setFocus();
            keysInto(edit, QStringLiteral("typed while written"));
            check(edit->isReadOnly() && !edit->toPlainText().contains(QLatin1String("typed while written")),
                  QStringLiteral("the transcription being written is read-only"), before.left(80));
        }
        bool renamed = false;
        QMetaObject::invokeMethod(panel, "rename", Qt::DirectConnection, Q_RETURN_ARG(bool, renamed), Q_ARG(QString, QStringLiteral("Renamed while written")));
        fact(QStringLiteral("S-R01 rename of the transcription being written"), renamed ? QStringLiteral("renamed") : QStringLiteral("refused"));
        QMetaObject::invokeMethod(panel, "copyAll", Qt::DirectConnection);
        bool removed = false;
        QMetaObject::invokeMethod(panel, "remove", Qt::DirectConnection, Q_RETURN_ARG(bool, removed), Q_ARG(bool, false));
        fact(QStringLiteral("S-R01 delete of the transcription being written"), removed ? QStringLiteral("deleted") : QStringLiteral("refused"));
        waitIdle(60000);
        // an older one: type into it
        QStringList titles;
        QMetaObject::invokeMethod(panel, "titles", Qt::DirectConnection, Q_RETURN_ARG(QStringList, titles));
        if (!titles.isEmpty()) {
            bool sel = false;
            QMetaObject::invokeMethod(panel, "selectTranscript", Qt::DirectConnection, Q_RETURN_ARG(bool, sel), Q_ARG(QString, titles.last()));
            settle(200);
            if (edit && !edit->isReadOnly()) {
                QTextCursor ec = edit->textCursor();
                ec.movePosition(QTextCursor::End);
                edit->setTextCursor(ec);
                keysInto(edit, QStringLiteral(" Edited by hand."));
                settle(1500);
                QString file;
                QMetaObject::invokeMethod(panel, "currentFile", Qt::DirectConnection, Q_RETURN_ARG(QString, file));
                check(readText(file).contains(QLatin1String("Edited by hand.")), QStringLiteral("an older transcription edited by hand is saved"));
            }
        }
    }
    // a microphone that goes away while recording, and no microphone at all
    step(QStringLiteral("the microphone removed while recording"));
    {
        const QString fifo = ctx().work + QStringLiteral("/mic.fifo");
        QProcess pa;
        pa.start(QStringLiteral("pactl"), {QStringLiteral("load-module"), QStringLiteral("module-pipe-source"), QStringLiteral("source_name=csmic"),
                                           QStringLiteral("file=") + fifo, QStringLiteral("format=s16le"), QStringLiteral("rate=16000"),
                                           QStringLiteral("channels=1")});
        pa.waitForFinished(10000);
        const QString moduleId = QString::fromUtf8(pa.readAllStandardOutput()).trimmed();
        if (pa.exitCode() == 0 && !moduleId.isEmpty()) {
            toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("audioInput"), QByteArray("csmic"));
            std::atomic<bool> stopFeed(false);
            const QByteArray pcm = pcm16(makeAudio(pattern(30)));
            QThread *feeder = QThread::create([&] {
                QFile f(fifo);
                if (!f.open(QIODevice::WriteOnly))
                    return;
                for (int at = 0; at < pcm.size() && !stopFeed; at += 3200) {
                    if (f.write(pcm.mid(at, 3200)) < 0)
                        break;
                    f.flush();
                    QThread::msleep(100);
                }
            });
            settle(1500);
            bool started = false;
            QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, QString()));
            feeder->start();
            settle(4000);
            QProcess::execute(QStringLiteral("pactl"), {QStringLiteral("unload-module"), moduleId});
            stopFeed = true;
            const bool rested = waitFor([tool] { return !callBool(tool, "isRecording"); }, 15000);
            fact(QStringLiteral("S-R01 the microphone removed while recording"),
                 QStringLiteral("%1; message: %2").arg(rested ? QStringLiteral("the recording ended") : QStringLiteral("still recording after 15 s"),
                                                       callString(tool, "lastError")));
            if (!rested)
                QMetaObject::invokeMethod(tool, "stopRecording", Qt::DirectConnection);
            waitIdle(30000);
            feeder->wait(5000);
            delete feeder;
            check(started, QStringLiteral("a recording from the pipe microphone starts"), callString(tool, "lastError"));
        } else
            note(QStringLiteral("(no PulseAudio pipe source: the removed microphone is not checked)"));
        toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("audioInput"), QByteArray("no-such-microphone"));
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, QString()));
        settle(1500);
        fact(QStringLiteral("S-R01 a microphone that is not there"), QStringLiteral("%1; message: %2").arg(started ? QStringLiteral("started (with another input?)")
                                                                                                                   : QStringLiteral("not started"),
                                                                                                           callString(tool, "lastError")));
        if (callBool(tool, "isRecording"))
            QMetaObject::invokeMethod(tool, "stopRecording", Qt::DirectConnection);
        waitIdle(30000);
        toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("audioInput"), QByteArray());
    }
    // close the project while recording, open it again
    step(QStringLiteral("close the project while recording"));
    {
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, shortWav));
        const QString file = callString(tool, "recordingFile");
        settle(3000);
        const QString path = p->indexPath();
        const bool closed = w->closeProject();
        settle(500);
        waitIdle(60000);
        check(closed && piecesInOrder(readText(file)), QStringLiteral("the project closes during a recording; the transcription stays whole"),
              readText(file).left(200));
        w->openProject(path);
        settle(600);
        p = w->project();
    }
    if (!p)
        return;
    // the project's files are not touched by recording (except the scene typed into)
    {
        QJsonObject now = modelOf(p, false);
        QJsonObject was = projectBefore;
        for (const QString &k : now.keys())
            if (k.contains(QLatin1String("Scene 20")))
                now.remove(k), was.remove(k);
        compareModels(was, now, QStringLiteral("recording leaves the project as it was (but for the typed scene)"));
        if (BinderItem *t2 = itemByTitle(p, QStringLiteral("Scene 20")))
            check(rawText(p->docs()->text(t2->id)) == typedModel, QStringLiteral("the scene typed into during recordings is saved"));
    }
    // quit while recording: the program waits for the last pieces
    step(QStringLiteral("quit while recording"));
    {
        QString err;
        p->save(&err);
        writeModel(p);
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, shortWav));
        const QString file = callString(tool, "recordingFile");
        settle(4000);
        QElapsedTimer t;
        t.start();
        w->close();
        settle(300);
        waitFor([w] { return !w->isVisible(); }, 70000);
        const qint64 ms = t.elapsed();
        fact(QStringLiteral("S-R01 quit while recording"), QStringLiteral("the window closed after %1 s").arg(ms / 1000.0, 0, 'f', 1));
        check(!w->isVisible() && ms < 70000 && pieces(readText(file)) >= 1 && piecesInOrder(readText(file)),
              QStringLiteral("quit while recording: the window closes within a minute, the pieces written before are in the file"), readText(file).left(200));
    }
    delete w;
}

// ================================================================ S-D01 autosave while typing on the large project
void caseSD01()
{
    // the timing lines of the saves (QB_TIMING, set by main for this case) go to a file
    const QString timingFile = ctx().dir + QStringLiteral("/timing.txt");
    const int fd = ::open(timingFile.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
        ::dup2(fd, 2);
    struct Variant {
        QString fixture;
        int seconds;
    };
    const QList<Variant> variants = {{QStringLiteral("fx2-scriv"), ctx().smoke ? 10 : 150}, {QStringLiteral("fx2-qb"), ctx().smoke ? 10 : 60}};
    for (int v = 0; v < variants.size(); ++v) {
        const Variant &var = variants.at(v);
        QElapsedTimer ot;
        ot.start();
        MainWindow *w = openWindow(fixtureCopy(var.fixture));
        fact(QStringLiteral("S-D01 %1 opened in").arg(var.fixture), QStringLiteral("%1 s").arg(ot.elapsed() / 1000.0, 0, 'f', 1));
        Project *p = w->project();
        if (!p) {
            delete w;
            continue;
        }
        // a 2,000-word document
        BinderItem *it = nullptr;
        for (BinderItem *x : textItems(p))
            if (x->title.startsWith(QLatin1String("Scene 10.")))
                it = x;
        if (!it)
            it = textItems(p).value(100);
        TextEditor *e = showDoc(w, it);
        if (!e) {
            delete w;
            continue;
        }
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("\n") + atParagraphs(99, 1500).join(QLatin1Char('\n')));
        e->setTextCursor(c);
        QString err;
        p->save(&err);
        settle(1000);
        std::fprintf(stderr, "cs: typing starts (%s)\n", qPrintable(var.fixture));
        std::fflush(stderr);
        for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
            typingGate(e, profile, var.seconds, QStringLiteral("S-D01 %1").arg(var.fixture));
        std::fprintf(stderr, "cs: typing ends (%s)\n", qPrintable(var.fixture));
        std::fflush(stderr);
        const QString typed = rawText(e->document());
        if (v == variants.size() - 1) {
            finishProject(w);
        } else {
            const QString scriv = p->scrivenerPath;
            const QString title = it->title;   // the items are gone after closeProject
            p->save(&err);
            w->closeProject();
            settle(500);
            w->openScrivenerProject(scriv);
            settle(800);
            BinderItem *again = w->project() ? itemByTitle(w->project(), title) : nullptr;
            const QString back = again ? rawText(w->project()->docs()->text(again->id)) : QString();
            check(again && back == typed, QStringLiteral("S-D01 %1: the typed text is there after reopening").arg(var.fixture),
                  again ? firstDifference(typed, back) : QStringLiteral("no item titled \"%1\"").arg(title));
            w->closeProject();
        }
        delete w;
        settle(500);
    }
    std::fflush(stderr);
    // the saves while typing
    QMap<QString, QList<int>> byKind;
    bool typing = false;
    for (const QString &line : readText(timingFile).split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("cs: typing starts")))
            typing = true;
        else if (line.startsWith(QLatin1String("cs: typing ends")))
            typing = false;
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^timing: (.+?) (\\d+) ms")).match(line);
        if (m.hasMatch() && typing)
            byKind[m.captured(1)] << m.captured(2).toInt();
    }
    QStringList summary;
    for (auto it = byKind.begin(); it != byKind.end(); ++it) {
        QList<int> v = it.value();
        std::sort(v.begin(), v.end());
        summary << QStringLiteral("%1: %2 times, median %3 ms, largest %4 ms").arg(it.key()).arg(v.size()).arg(v.at(v.size() / 2)).arg(v.last());
    }
    fact(QStringLiteral("S-D01 saves while typing"), summary.join(QLatin1String("; ")));
}

} // namespace cs
