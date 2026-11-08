// Scripted run for version 0.61.1: the quote marks of a project converted from File > Options >
// Corrections (straight to typographic in a quote style and back; texts, titles, synopses, notes,
// comments; formatting kept; a snapshot first; the scope of selected documents), and Project >
// Delete Old Snapshots (the question, every snapshot gone, the texts kept, the command off without
// snapshots), also in Scrivener format: snapshots deleted in the program leave the .scriv (a single
// one and all, one of them written in another time zone), and the .scriv's fingerprint stays right.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0611_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Replace.h"
#include "core/Snapshots.h"
#include "compile/PluginManager.h"
#include "import/ScrivenerFormat.h"
#include "text/AutoCorrect.h"
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
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
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

template <typename T>
static void withModal(int delayMs, std::function<void(T *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<T *>(tl))
                if (d->isVisible()) {
                    fn(d);
                    return;
                }
        std::printf("      (the modal window did not open)\n");
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

static QString applied(const QString &text, bool typographic, const QString &style)
{
    QString s = text;
    for (const auto &c : autocorrect::convertQuotes(text, typographic, style))
        s[c.first] = c.second;
    return s;
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QStringList &paragraphs)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    for (int i = 0; i < paragraphs.size(); ++i) {
        if (i > 0)
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(paragraphs[i]);
    }
    return it;
}

static QString plain(Project *p, BinderItem *it) { return p->docs()->text(it->id)->toPlainText(); }

static QString commentText(QTextDocument *d)
{
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (NoteKind(cf.intProperty(props::NoteKind)) == NoteKind::Comment)
                return cf.stringProperty(props::NoteText);
        }
    return QString();
}

static bool boldAt(QTextDocument *d, const QString &around, int offset)
{
    const int at = int(d->toPlainText().indexOf(around));
    if (at < 0)
        return false;
    QTextCursor c(d);
    c.setPosition(at + offset + 1);
    return c.charFormat().fontWeight() >= QFont::Bold;
}

static int rtfCount(const QString &dir)
{
    return int(QDir(dir).entryList({QStringLiteral("*.rtf")}, QDir::Files).size());
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QString stateFingerprint(Project *p)
{
    return QJsonDocument::fromJson(readAll(scrivener::stateFile(p))).object().value(QStringLiteral("fingerprint")).toString();
}

static QString uuidOf(const QString &scriv, const QString &title)
{
    const QString x = QString::fromUtf8(readAll(scrivener::scrivxIn(scriv)));
    const int t = int(x.indexOf(QStringLiteral("<Title>") + title + QStringLiteral("</Title>")));
    if (t < 0)
        return QString();
    const int b = int(x.lastIndexOf(QStringLiteral("UUID=\""), t));
    return b < 0 ? QString() : x.mid(b + 6, 36);
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0611Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0611Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.61.1")) >= 0,
          QStringLiteral("version 0.61.1 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
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
    as.quoteStyle = QStringLiteral("en");
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    // ================================================================ the rules
    {
        const QString s = QStringLiteral("\"Hello,\" she said. \"It's 'fine'.\"");
        check(applied(s, true, QStringLiteral("en")) == QStringLiteral("“Hello,” she said. “It’s ‘fine’.”"),
              QStringLiteral("quotes: English marks, opening and closing, the apostrophe inside a word"),
              applied(s, true, QStringLiteral("en")));
        check(applied(s, true, QStringLiteral("de")) == QStringLiteral("„Hello,“ she said. „It’s ‚fine‘.“"),
              QStringLiteral("quotes: German marks „…“ ‚…‘"), applied(s, true, QStringLiteral("de")));
        check(applied(QStringLiteral("'Peter's boat'"), true, QStringLiteral("de")) == QStringLiteral("‚Peter’s boat‘"),
              QStringLiteral("quotes: German, an apostrophe inside a quoted word and the closing ‘"),
              applied(QStringLiteral("'Peter's boat'"), true, QStringLiteral("de")));
        check(applied(QStringLiteral("\"Oui\""), true, QStringLiteral("fr")) == QStringLiteral("«Oui»")
                  && applied(QStringLiteral("\"Ja\""), true, QStringLiteral("de2")) == QStringLiteral("»Ja«"),
              QStringLiteral("quotes: French and the other German style"));
        const QString n = QStringLiteral("Back in the '90s he said \"no.\"");
        check(applied(n, true, QStringLiteral("en")) == QStringLiteral("Back in the ’90s he said “no.”"),
              QStringLiteral("quotes: an apostrophe before a number ('90s) is ’"), applied(n, true, QStringLiteral("en")));
        const QString alone = QStringLiteral("A 12 \" board and a ' mark");
        check(autocorrect::convertQuotes(alone, true, QStringLiteral("en")).isEmpty(),
              QStringLiteral("quotes: a straight mark with spaces on both sides stays"));
        const QString nest = QStringLiteral("(\"Yes\") —\"Go\" \"'Hi,' she said.\"");
        check(applied(nest, true, QStringLiteral("en")) == QStringLiteral("(“Yes”) —“Go” “‘Hi,’ she said.”"),
              QStringLiteral("quotes: after a bracket, a dash and an opening mark they open"), applied(nest, true, QStringLiteral("en")));
        check(autocorrect::convertQuotes(QStringLiteral("“Done.” It’s ‘so’."), true, QStringLiteral("en")).isEmpty(),
              QStringLiteral("quotes: typographic marks already there stay"));
        const QString back = QStringLiteral("„It’s ‚fine‘.“ «Oui» ‹non› “a” ‘b’");
        check(applied(back, false, QString()) == QStringLiteral("\"It's 'fine'.\" \"Oui\" 'non' \"a\" 'b'"),
              QStringLiteral("quotes: every typographic mark and apostrophe back to straight"), applied(back, false, QString()));
    }

    // ================================================================ a project
    QTemporaryDir work;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Quotes"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    BinderItem *one = textDoc(p, p->draft(), QStringLiteral("The \"Lighthouse\""),
                              {QStringLiteral("\"Hello,\" she said."), QStringLiteral("It's Peter's 'boat' in the \"Storm\" now.")});
    one->synopsis = QStringLiteral("A \"quiet\" night");
    {
        QTextDocument *d = p->docs()->text(one->id);
        // "Storm" with its quote marks in bold
        const int at = int(d->toPlainText().indexOf(QStringLiteral("\"Storm\"")));
        QTextCursor c(d);
        c.setPosition(at);
        c.setPosition(at + 7, QTextCursor::KeepAnchor);
        QTextCharFormat b;
        b.setFontWeight(QFont::Bold);
        c.mergeCharFormat(b);
        // a comment on "she"
        const int she = int(d->toPlainText().indexOf(QStringLiteral("she")));
        c.setPosition(she);
        c.setPosition(she + 3, QTextCursor::KeepAnchor);
        c.mergeCharFormat(text::noteFormat(NoteKind::Comment, QStringLiteral("c1"), QStringLiteral("Is \"she\" right?")));
        QTextCursor n(p->docs()->notes(one->id));
        n.insertText(QStringLiteral("Check the \"date\"."));
    }
    BinderItem *two = textDoc(p, p->draft(), QStringLiteral("Second"), {QStringLiteral("No quotes here at all.")});
    BinderItem *three = textDoc(p, p->draft(), QStringLiteral("Third"), {QStringLiteral("He said, \"Later.\"")});
    BinderItem *binned = textDoc(p, p->draft(), QStringLiteral("Binned"), {QStringLiteral("A \"trashed\" line.")});
    p->model()->moveToTrash({binned});
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(900);
    EditorPane *pane = nullptr;
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible() && !pane)
            pane = ep;
    if (pane) {
        pane->showItems({one}, false, int(ViewMode::Single));
        settle(400);
    }

    // ================================================================ the buttons in Options > Corrections
    {
        OptionsDialog none(nullptr, 0);
        auto *b = none.findChild<QPushButton *>(QStringLiteral("optConvertQuotesSmart"));
        check(b && !b->isEnabled(), QStringLiteral("Options without a project: the convert buttons are off"));
    }
    ReplaceResult first;
    QString dlgTitle, styleShown, countText, doneText;
    bool straightThere = false;
    {
        OptionsDialog dlg(&w, 0);
        dlg.showSection(OptionsDialog::SCorrections, QStringLiteral("Corrections"));
        dlg.show();
        settle(300);
        auto *smart = dlg.findChild<QPushButton *>(QStringLiteral("optConvertQuotesSmart"));
        auto *straight = dlg.findChild<QPushButton *>(QStringLiteral("optConvertQuotesStraight"));
        straightThere = straight && straight->isEnabled();
        auto *style = dlg.findChild<QComboBox *>(QStringLiteral("optQuoteStyle"));
        check(smart && smart->isEnabled() && straightThere && style,
              QStringLiteral("Options > Corrections: Convert to Typographic Quotes and Convert to Straight Quotes are there"));
        if (style)
            style->setCurrentIndex(style->findData(QStringLiteral("en")));
        shot(&dlg, QStringLiteral("v0611-options-corrections"));
        if (smart) {
            withModal<ConvertQuotesDialog>(500, [&](ConvertQuotesDialog *d) {
                dlgTitle = d->windowTitle();
                styleShown = d->style();
                const ReplaceResult counted = d->count();
                countText = d->resultText();
                shot(d, QStringLiteral("v0611-convert-quotes"));
                first = d->convert(false);
                doneText = d->resultText();
                check(counted.total() == first.total(), QStringLiteral("(Count says what Convert does)"),
                      QStringLiteral("%1 / %2").arg(counted.total()).arg(first.total()));
                d->reject();
            });
            smart->click();
            settle(400);
        }
        dlg.reject();
    }
    check(dlgTitle == QLatin1String("Convert to Typographic Quotes") && styleShown == QLatin1String("en"),
          QStringLiteral("the button opens the window with the style chosen in Options"), dlgTitle + QLatin1String(" / ") + styleShown);
    std::printf("      count: %s\n      done: %s\n", qPrintable(countText), qPrintable(doneText));
    check(countText.contains(QLatin1String("18 quote marks to convert in 2 documents")), QStringLiteral("Count names the quote marks and documents"),
          countText);
    {
        QTextDocument *d = w.project()->docs()->text(one->id);
        const QString t = d->toPlainText();
        check(t == QStringLiteral("“Hello,” she said.\nIt’s Peter’s ‘boat’ in the “Storm” now."), QStringLiteral("text: converted"), t);
        check(boldAt(d, QStringLiteral("“Storm”"), 0) && boldAt(d, QStringLiteral("“Storm”"), 6),
              QStringLiteral("text: a bold quote mark stays bold"));
        check(one->title == QStringLiteral("The “Lighthouse”") && one->synopsis == QStringLiteral("A “quiet” night"),
              QStringLiteral("title and synopsis: converted"), one->title + QLatin1String(" / ") + one->synopsis);
        check(w.project()->docs()->notes(one->id)->toPlainText() == QStringLiteral("Check the “date”."),
              QStringLiteral("notes: converted"), w.project()->docs()->notes(one->id)->toPlainText());
        check(commentText(d) == QStringLiteral("Is “she” right?"), QStringLiteral("comment: converted"), commentText(d));
        check(plain(w.project(), three) == QStringLiteral("He said, “Later.”") && plain(w.project(), two) == QLatin1String("No quotes here at all."),
              QStringLiteral("other documents: converted where they have quotes"));
        check(plain(w.project(), binned) == QStringLiteral("A \"trashed\" line."), QStringLiteral("the Trash: left alone (not asked for)"));
        const QList<Snapshot> snaps = snapshots::list(w.project(), one->id);
        check(first.snapshots == 2 && !snaps.isEmpty() && snaps.first().title == QLatin1String("Before converting to typographic quotes"),
              QStringLiteral("a snapshot of each changed text first"), QString::number(first.snapshots));
        TextEditor *e = pane ? pane->currentEditor() : nullptr;
        check(e && e->toPlainText().startsWith(QStringLiteral("“Hello,”")), QStringLiteral("the open editor shows the conversion"));
    }

    // back to straight, only the selected document; then German marks
    {
        QString sel;
        ReplaceResult back;
        withModal<ConvertQuotesDialog>(500, [&](ConvertQuotesDialog *d) {
            for (QRadioButton *r : d->findChildren<QRadioButton *>())
                if (r->text().startsWith(QLatin1String("Selected")) && r->isEnabled()) {
                    r->setChecked(true);
                    sel = r->text();
                }
            back = d->convert(false);
            d->reject();
        });
        w.findChild<BinderPanel *>()->select({three}, true);
        settle(200);
        w.convertQuotes(false, QStringLiteral("en"));
        settle(300);
        check(!sel.isEmpty() && plain(w.project(), three) == QStringLiteral("He said, \"Later.\"")
                  && plain(w.project(), one).startsWith(QStringLiteral("“Hello,”")),
              QStringLiteral("straight again, only in the selected document"), sel + QLatin1String(" | ") + plain(w.project(), three));
        withModal<ConvertQuotesDialog>(500, [&](ConvertQuotesDialog *d) {
            d->convert(false);
            d->reject();
        });
        w.convertQuotes(true, QStringLiteral("de"));
        settle(300);
        check(plain(w.project(), three) == QStringLiteral("He said, „Later.“"), QStringLiteral("German marks from the German style"),
              plain(w.project(), three));
    }

    // ================================================================ Delete Old Snapshots
    Project *wp2 = w.project();
    snapshots::take(wp2, two->id, QStringLiteral("Two, first"));
    snapshots::take(wp2, two->id, QStringLiteral("Two, second"));
    snapshots::take(wp2, binned->id, QStringLiteral("In the Trash"));
    {
        // a folder left behind by a document that is no longer there
        const QString orphan = wp2->snapshotsDir(QStringLiteral("gone-document"));
        QDir().mkpath(orphan);
        QFile f(orphan + QLatin1String("/20250101-120000.md"));
        if (f.open(QIODevice::WriteOnly))
            f.write("old text\n");
    }
    const snapshots::Totals before = snapshots::totals(wp2);
    check(before.snapshots >= 6 && before.documents >= 5 && before.bytes > 0,
          QStringLiteral("(snapshots in the project: %1 of %2 documents, %3 bytes)").arg(before.snapshots).arg(before.documents).arg(before.bytes));
    QAction *del = w.findChild<QAction *>(QStringLiteral("deleteSnapshots"));
    QMenu *projectMenu = nullptr;
    for (QAction *m : w.menuBar()->actions())
        if (m->menu() && m->text().remove(QLatin1Char('&')) == QLatin1String("Project"))
            projectMenu = m->menu();
    {
        QStringList names;
        if (projectMenu)
            for (QAction *a : projectMenu->actions())
                names << a->objectName();
        const int t = int(names.indexOf(QStringLiteral("emptyTrash")));
        check(del && t >= 0 && names.value(t + 1) == QLatin1String("deleteSnapshots"),
              QStringLiteral("Project menu: Delete Old Snapshots right below Empty Trash"), names.join(QLatin1Char(' ')));
        if (projectMenu)
            emit projectMenu->aboutToShow();
        check(del && del->isEnabled() && del->text().remove(QLatin1Char('&')) == QStringLiteral("Delete Old Snapshots…"),
              QStringLiteral("Delete Old Snapshots is on while there are snapshots"));
    }
    const QString textBefore = plain(wp2, one);
    QString question, informative;
    withModal<QMessageBox>(400, [&](QMessageBox *b) {
        question = b->text();
        informative = b->informativeText();
        shot(b, QStringLiteral("v0611-delete-snapshots-question"));
        b->button(QMessageBox::Cancel)->click();
    });
    if (del)
        del->trigger();
    settle(300);
    std::printf("      question: %s\n      %s\n", qPrintable(question), qPrintable(informative));
    check(question == QStringLiteral("Delete all %1 snapshots of %2 documents forever?").arg(before.snapshots).arg(before.documents)
              && informative.contains(QRegularExpression(QStringLiteral("\\d (kB|MB|GB|bytes)"))),
          QStringLiteral("the question names the snapshots, the documents and their size (SI units)"), question + QLatin1Char(' ') + informative);
    check(snapshots::totals(wp2).snapshots == before.snapshots, QStringLiteral("Cancel: nothing deleted"));
    withModal<QMessageBox>(400, [&](QMessageBox *b) {
        for (QAbstractButton *x : b->buttons())
            if (x->text() == QLatin1String("Delete Snapshots"))
                x->click();
    });
    if (del)
        del->trigger();
    settle(500);
    bool anyFlag = false;
    for (BinderItem *it : wp2->allItems())
        anyFlag = anyFlag || it->hasSnapshots;
    check(snapshots::totals(wp2).snapshots == 0 && !anyFlag && QDir(wp2->snapshotsDir(QString())).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
          QStringLiteral("Delete: every snapshot gone (also in the Trash and of a document no longer there)"));
    check(plain(wp2, one) == textBefore && plain(wp2, binned) == QStringLiteral("A \"trashed\" line."),
          QStringLiteral("Delete: the texts stay as they are"));
    if (projectMenu)
        emit projectMenu->aboutToShow();
    check(del && !del->isEnabled(), QStringLiteral("without snapshots the command is off"));
    snapshots::take(wp2, two->id, QStringLiteral("New"));
    if (projectMenu)
        emit projectMenu->aboutToShow();
    check(del && del->isEnabled(), QStringLiteral("a new snapshot turns it on again"));
    {
        const QString folder = wp2->folderPath(), file = wp2->indexPath();
        snapshots::removeAll(wp2);
        wp2->save(&err);
        auto again = Project::open(file, &err);
        bool flags = false;
        if (again)
            for (BinderItem *it : again->allItems())
                flags = flags || it->hasSnapshots;
        check(again && !flags, QStringLiteral("opened again: no snapshots"), err);
        Q_UNUSED(folder);
    }

    // ================================================================ Scrivener format
    {
        QTemporaryDir sw;
        auto sp = templates::createProject(blank, sw.path(), QStringLiteral("Scriv Snaps"), &err);
        BinderItem *a = textDoc(sp.get(), sp->draft(), QStringLiteral("Alpha"), {QStringLiteral("Alpha text.")});
        BinderItem *b = textDoc(sp.get(), sp->draft(), QStringLiteral("Beta"), {QStringLiteral("Beta text.")});
        snapshots::take(sp.get(), a->id, QStringLiteral("A1"));
        snapshots::take(sp.get(), a->id, QStringLiteral("A2"));
        snapshots::take(sp.get(), b->id, QStringLiteral("B1"));
        sp->save(&err);
        const QString scriv = sw.path() + QLatin1String("/Snaps.scriv");
        auto first = scrivener::convertToScrivener(sp.get(), scriv, &err);
        check(first != nullptr, QStringLiteral("(saved in Scrivener format)"), err);
        first.reset();
        const QString ua = uuidOf(scriv, QStringLiteral("Alpha")), ub = uuidOf(scriv, QStringLiteral("Beta"));
        const QString da = scriv + QLatin1String("/Snapshots/") + ua + QLatin1String(".snapshots");
        const QString db = scriv + QLatin1String("/Snapshots/") + ub + QLatin1String(".snapshots");
        check(rtfCount(da) == 2 && rtfCount(db) == 1, QStringLiteral("(the .scriv has the snapshots: 2 and 1)"),
              QStringLiteral("%1 / %2").arg(rtfCount(da)).arg(rtfCount(db)));
        // a snapshot Scrivener wrote in another time zone (Pacific time)
        {
            QFile f(db + QLatin1String("/2025-10-27-02-20-40-0700.rtf"));
            if (f.open(QIODevice::WriteOnly))
                f.write("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Helvetica;}}\\f0 Beta in October.\\par}");
            QString x = QString::fromUtf8(readAll(db + QLatin1String("/index.xml")));
            x.replace(QLatin1String("</Snapshots>"),
                      QLatin1String("    <Snapshot>\n        <Title>From Scrivener</Title>\n        <Date>2025-10-27 02:20:40 -0700</Date>\n"
                                    "    </Snapshot>\n</Snapshots>"));
            QFile i(db + QLatin1String("/index.xml"));
            if (i.open(QIODevice::WriteOnly))
                i.write(x.toUtf8());
        }
        scrivener::OpenResult opened = scrivener::open(scriv, &err);
        std::unique_ptr<Project> wc = std::move(opened.project);
        check(wc && opened.reread, QStringLiteral("(the .scriv, changed outside, is read again)"), err);
        if (wc) {
            const QString wa = wc->item(a->id) ? a->id : QString();
            QList<Snapshot> la = snapshots::list(wc.get(), a->id), lb = snapshots::list(wc.get(), b->id);
            check(la.size() == 2 && lb.size() == 2, QStringLiteral("(the working copy has 2 and 2 snapshots)"),
                  QStringLiteral("%1 / %2").arg(la.size()).arg(lb.size()));
            // one snapshot deleted in the inspector's way
            if (!la.isEmpty())
                snapshots::remove(wc.get(), la.first());
            wc->save(&err);
            const QString idx = QString::fromUtf8(readAll(da + QLatin1String("/index.xml")));
            check(rtfCount(da) == 1 && idx.count(QLatin1String("<Snapshot>")) == 1,
                  QStringLiteral("Scrivener format: a snapshot deleted in the program leaves the .scriv (file and index.xml)"),
                  QStringLiteral("%1 files, %2 entries").arg(rtfCount(da)).arg(idx.count(QLatin1String("<Snapshot>"))));
            check(rtfCount(db) == 2, QStringLiteral("Scrivener format: the other document's snapshots stay"));
            check(stateFingerprint(wc.get()) == scrivener::fingerprint(scriv),
                  QStringLiteral("Scrivener format: the fingerprint is right after the deletion"));
            // all of them
            const int n = snapshots::removeAll(wc.get());
            wc->save(&err);
            check(n == 3 && !QDir(da).exists() && !QDir(db).exists(),
                  QStringLiteral("Scrivener format: Delete Old Snapshots empties the .scriv's snapshots (also the one from another time zone)"),
                  QStringLiteral("%1 deleted, A %2, B %3").arg(n).arg(QDir(da).exists()).arg(QDir(db).exists()));
            check(QFile::exists(scriv + QLatin1String("/Files/Data/") + ua + QLatin1String("/content.rtf"))
                      && QString::fromUtf8(readAll(scriv + QLatin1String("/Files/Data/") + ua + QLatin1String("/content.rtf"))).contains(QLatin1String("Alpha text")),
                  QStringLiteral("Scrivener format: the texts stay in the .scriv"));
            check(stateFingerprint(wc.get()) == scrivener::fingerprint(scriv),
                  QStringLiteral("Scrivener format: the fingerprint is right after Delete Old Snapshots"));
            Q_UNUSED(wa);
            wc.reset();
            // read from the .scriv alone: nothing comes back
            QDir(scrivener::workingCopyFor(scriv)).removeRecursively();
            scrivener::OpenResult fresh = scrivener::open(scriv, &err);
            bool flags = false;
            if (fresh.project)
                for (BinderItem *it : fresh.project->allItems())
                    flags = flags || it->hasSnapshots;
            check(fresh.project && fresh.reread && !flags, QStringLiteral("Scrivener format: read anew, the project has no snapshots"), err);
        }
    }

    w.close();
    settle(200);
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
