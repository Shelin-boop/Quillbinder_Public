// Version 0.21.1: corrections asked for by the writer on 2026-10-22.
//  - The format bar, the Format menu and their shortcuts act on the text field with the
//    cursor (the notes, the bookmarked text in the inspector, the text of an object), also
//    while the keyboard is on the size field of the format bar.
//  - The bookmarked text in the inspector is an editable copy, kept in step with the
//    document both ways, at a zoom of its own; what is written or formatted there is
//    written into the document (and its file).
//  - Parts with a zoom each: synopsis / notes, bookmark lists / bookmarked text, snapshot
//    list / text, object list / sheet (inspector and Objects window); the tab row and the
//    bottom row with the inspector; widgets made later in a zoomed part; the default zoom
//    of new mind maps; every part in File > Options > Editing > Default text zoom.
//  - Mind map backgrounds: the shared palette, a color of the board's own per kind of
//    theme (light / dark), defaults for light and dark themes that boards follow, kept
//    for empty boards, files of older versions.
//  - Places kept with the project: the place in each document (also Combined text, the
//    copyholder), the inspector (tab, dividers, bookmark, object), each mind map's zoom
//    and visible area (inspector and window apart), the zoom of pictures, zoom and page of
//    PDF files.
//  - Bold, italic, underline, strikethrough, text color and highlight in the long text
//    fields of objects, kept in the object's file.
//  - The credits in Help > About.
// Saves screenshots into the folder given as first argument. Exit code = failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v0211_tests /tmp/shots
#include "AppInfo.h"
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/BookmarkList.h"
#include "ui/ColorPickButton.h"
#include "ui/Copyholder.h"
#include "ui/DocMirror.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/MediaView.h"
#include "ui/MindMapController.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapUi.h"
#include "ui/MindMapView.h"
#include "ui/ObjectsUi.h"
#include "ui/OptionsDialog.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/ViewMemory.h"
#include "ui/Zoom.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontComboBox>
#include <QLabel>
#include <QLineF>
#include <QRegularExpression>
#include <QSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTimer>
#include <QVersionNumber>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QWheelEvent>

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
static void section(const QString &s)
{
    std::printf("\n== %s\n", qPrintable(s));
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

static void setupApp(const QString &name)
{
    QCoreApplication::setOrganizationName(name);
    QCoreApplication::setApplicationName(name);
}
static void styleApp()
{
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
    as.backupOnOpen = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
}
static void setTheme(const QString &id)
{
    AppSettings &as = AppSettings::instance();
    as.theme = id;
    as.save();
    theme::apply();
    settle(150);
}

static std::unique_ptr<Project> novelProject(const QString &dir, const QString &title, QString *err)
{
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    return templates::createProject(novel, dir, title, err);
}

static QAction *byKey(QWidget *w, const QString &name)
{
    for (QAction *a : w->findChildren<QAction *>())
        if (a->objectName() == name)
            return a;
    return nullptr;
}

static EditorPane *firstPane(MainWindow &w)
{
    const auto panes = w.findChildren<EditorPane *>();
    for (EditorPane *p : panes)
        if (p->isVisible())
            return p;
    return panes.isEmpty() ? nullptr : panes.first();
}

static void setText(Project *p, BinderItem *it, const QString &text)
{
    QTextDocument *d = p->docs()->text(it->id);
    QTextCursor c(d);
    c.select(QTextCursor::Document);
    c.insertText(text);
}

static QString plain(Project *p, BinderItem *it) { return p->docs()->text(it->id)->toPlainText(); }

static void wheel(QWidget *target, int steps, Qt::KeyboardModifiers mods = Qt::ControlModifier)
{
    const QPoint pos = target->rect().center();
    QWheelEvent ev(QPointF(pos), QPointF(target->mapToGlobal(pos)), QPoint(), QPoint(0, 120 * steps), Qt::NoButton, mods,
                   Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &ev);
    settle(60);
}

static QString longText(int paragraphs, const QString &word)
{
    QStringList l;
    for (int i = 0; i < paragraphs; ++i)
        l << QStringLiteral("%1 paragraph %2: the tide came in over the stones and went out again, slowly, as it always did.").arg(word).arg(i + 1);
    return l.join(QLatin1Char('\n'));
}

static void selectWord(QTextEdit *e, const QString &word)
{
    const int at = e->document()->toPlainText().indexOf(word);
    QTextCursor c = e->textCursor();
    c.setPosition(at);
    c.setPosition(at + int(word.size()), QTextCursor::KeepAnchor);
    e->setTextCursor(c);
}

static QTextCharFormat formatAt(QTextDocument *d, int pos)
{
    QTextCursor c(d);
    c.setPosition(pos + 1);
    return c.charFormat();
}

template<typename T>
static T *findVisible(QWidget *root, const std::function<bool(T *)> &ok = nullptr)
{
    for (T *x : root->findChildren<T *>())
        if (x->isVisible() && (!ok || ok(x)))
            return x;
    return nullptr;
}

static TextEditor *notesEditor(Inspector *insp)
{
    return findVisible<TextEditor>(insp, [](TextEditor *t) { return t->isNotes(); });
}

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<MainWindow> w;
    Project *p = nullptr;
    BinderPanel *binder = nullptr;
    Inspector *insp = nullptr;
    BinderItem *a = nullptr, *b = nullptr, *folder = nullptr;
    bool ok = false;
    explicit Fixture(const QString &title)
    {
        QString err;
        auto proj = novelProject(dir.path(), title, &err);
        if (!proj) {
            check(false, QStringLiteral("create project"), err);
            return;
        }
        p = proj.get();
        folder = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter"));
        a = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Harbor"));
        b = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Lighthouse"));
        setText(p, a, longText(120, QStringLiteral("Harbor")));
        setText(p, b, QStringLiteral("The lighthouse keeper rowed out at dawn. Gulls followed the boat."));
        w = std::make_unique<MainWindow>();
        w->resize(1400, 860);
        w->show();
        w->activateWindow();
        w->adoptProject(std::move(proj));
        settle(300);
        if (QAction *si = byKey(w.get(), QStringLiteral("showInspector")); si && !si->isChecked())
            si->trigger();
        binder = w->findChild<BinderPanel *>();
        insp = w->findChild<Inspector *>();
        ok = binder && insp;
    }
    ~Fixture()
    {
        if (w)
            w->closeProject();
    }
    void show(BinderItem *it)
    {
        binder->select({it}, true);
        settle(250);
    }
    TextEditor *editor() { return firstPane(*w) ? firstPane(*w)->currentEditor() : nullptr; }
    // closes and opens the project again (the window state is written and read)
    bool reopen()
    {
        const QString path = p->folderPath();
        const QString ida = a->id, idb = b->id, idf = folder->id;
        a = b = folder = nullptr;
        w->closeProject();
        p = nullptr;
        settle(150);
        if (!w->openProject(path))
            return false;
        settle(400);
        p = w->project();
        a = p ? p->item(ida) : nullptr;
        b = p ? p->item(idb) : nullptr;
        folder = p ? p->item(idf) : nullptr;
        return p && a && b;
    }
};

// ------------------------------------------------------------------ the format bar's text

static void formatTargetChecks()
{
    section(QStringLiteral("The format bar acts on the text field with the cursor"));
    Fixture f(QStringLiteral("Format target"));
    if (!f.ok)
        return;
    f.show(f.b);
    TextEditor *main = f.editor();
    f.insp->showTab(Inspector::Notes);
    settle(200);
    TextEditor *notes = notesEditor(f.insp);
    check(main && notes, QStringLiteral("editor and notes are there"));
    if (!main || !notes)
        return;
    f.w->activateWindow();
    notes->setFocus();
    QTextCursor c = notes->textCursor();
    c.insertText(QStringLiteral("Notes about the keeper and his boat."));
    c.select(QTextCursor::Document);
    notes->setTextCursor(c);
    settle(100);
    const QString mainBefore = main->document()->toHtml();
    auto *spin = findVisible<QDoubleSpinBox>(f.w.get(), [](QDoubleSpinBox *s) { return s->toolTip().contains(QLatin1String("Font size")); });
    check(spin != nullptr, QStringLiteral("the size field of the format bar"));
    if (!spin)
        return;
    spin->setFocus(); // as a click in it does
    settle(100);
    check(f.w->formatTarget() == notes, QStringLiteral("the keyboard on the size field: the format bar still acts on the notes"),
          f.w->formatTarget() == main ? QStringLiteral("(the main editor)") : QString());
    spin->setValue(21);
    settle(150);
    check(qAbs(formatAt(notes->document(), 3).fontPointSize() - 21) < 0.01,
          QStringLiteral("a size typed into the size field changes the notes"), QString::number(formatAt(notes->document(), 3).fontPointSize()));
    check(main->document()->toHtml() == mainBefore, QStringLiteral("... and not the text in the main editor"));
    // the font list without taking the keyboard
    notes->setFocus();
    settle(50);
    auto *fonts = findVisible<QFontComboBox>(f.w.get());
    if (fonts) {
        fonts->setCurrentFont(QFont(QStringLiteral("DejaVu Serif")));
        settle(150);
    }
    check(fonts && formatAt(notes->document(), 3).fontFamilies().toStringList().value(0) == QLatin1String("DejaVu Serif")
              && main->document()->toHtml() == mainBefore,
          QStringLiteral("the font list changes the notes as well, the main editor stays"));
    // a click in the binder: the format bar acts on the editor again
    f.binder->tree()->setFocus();
    settle(100);
    check(f.w->formatTarget() == main, QStringLiteral("the keyboard elsewhere (the binder): the format bar acts on the main editor again"));
    shot(f.w.get(), QStringLiteral("01-notes-size-from-format-bar"));
}

// ------------------------------------------------------------------ the bookmark preview

static void previewChecks()
{
    section(QStringLiteral("The bookmarked text in the inspector: an editable copy kept in step"));
    Fixture f(QStringLiteral("Preview"));
    if (!f.ok)
        return;
    Project *p = f.p;
    f.show(f.b);
    TextEditor *main = f.editor();
    p->projectBookmarks.append({f.b->title, QStringLiteral("qb://") + f.b->id});
    p->notifyBookmarksChanged();
    f.insp->showTab(Inspector::Bookmarks);
    settle(150);
    f.insp->projectBookmarkList()->setCurrentRow(0);
    settle(250);
    TextEditor *pv = f.insp->bookmarkPreview();
    QTextDocument *doc = p->docs()->text(f.b->id);
    check(pv && pv->isVisible() && !pv->isReadOnly() && pv->document() != doc && f.insp->previewMirror(),
          QStringLiteral("a document the editor shows too: the preview is an editable copy (before: read-only)"));
    if (!pv || !main)
        return;
    check(main->document() == doc, QStringLiteral("... and the editor keeps the document itself"));
    // typing in the preview
    f.w->activateWindow();
    pv->setFocus();
    QTextCursor c = pv->textCursor();
    c.movePosition(QTextCursor::End);
    pv->setTextCursor(c);
    QTest::keyClicks(pv, QStringLiteral(" The bell rang."));
    settle(100);
    check(plain(p, f.b).endsWith(QLatin1String("The bell rang.")) && main->document()->toPlainText().endsWith(QLatin1String("The bell rang.")),
          QStringLiteral("typed in the preview: in the document and in the editor at once"), plain(p, f.b));
    // a font from the format bar on a word selected in the preview
    selectWord(pv, QStringLiteral("keeper"));
    auto *fonts = findVisible<QFontComboBox>(f.w.get());
    if (fonts) {
        fonts->setCurrentFont(QFont(QStringLiteral("DejaVu Serif")));
        settle(150);
    }
    const int at = plain(p, f.b).indexOf(QLatin1String("keeper"));
    check(formatAt(doc, at).fontFamilies().toStringList().value(0) == QLatin1String("DejaVu Serif"),
          QStringLiteral("a font chosen with the cursor in the preview: in the document (the request of the writer)"),
          formatAt(doc, at).fontFamilies().toStringList().join(QLatin1String(",")));
    // the size field (it takes the keyboard)
    selectWord(pv, QStringLiteral("dawn"));
    auto *spin = findVisible<QDoubleSpinBox>(f.w.get(), [](QDoubleSpinBox *s) { return s->toolTip().contains(QLatin1String("Font size")); });
    if (spin) {
        spin->setFocus();
        settle(50);
        spin->setValue(18);
        settle(150);
    }
    const int dawn = plain(p, f.b).indexOf(QLatin1String("dawn"));
    check(qAbs(formatAt(doc, dawn).fontPointSize() - 18) < 0.01, QStringLiteral("... a size from the size field as well"),
          QString::number(formatAt(doc, dawn).fontPointSize()));
    // bold with the Format menu command (Ctrl+B)
    pv->setFocus();
    selectWord(pv, QStringLiteral("Gulls"));
    byKey(f.w.get(), QStringLiteral("bold"))->trigger();
    settle(100);
    const int gulls = plain(p, f.b).indexOf(QLatin1String("Gulls"));
    check(formatAt(doc, gulls).fontWeight() >= QFont::DemiBold, QStringLiteral("... and Bold"));
    shot(f.w.get(), QStringLiteral("02-preview-font-and-bold"));
    // typing in the editor reaches the preview
    main->setFocus();
    c = main->textCursor();
    c.movePosition(QTextCursor::End);
    main->setTextCursor(c);
    QTest::keyClicks(main, QStringLiteral(" Night fell."));
    settle(100);
    check(pv->document()->toPlainText().endsWith(QLatin1String("Night fell.")), QStringLiteral("typed in the editor: in the preview at once"));
    check(sameContents(doc, pv->document()), QStringLiteral("... the copy and the document have the same text and formats"));
    // undo in the preview takes its change back in the document
    pv->setFocus();
    c = pv->textCursor();
    c.movePosition(QTextCursor::End);
    pv->setTextCursor(c);
    QTest::keyClicks(pv, QStringLiteral("XYZ"));
    settle(50);
    const bool typed = plain(p, f.b).endsWith(QLatin1String("XYZ"));
    while (pv->document()->toPlainText().endsWith(QLatin1String("Z")) || pv->document()->toPlainText().endsWith(QLatin1String("Y"))
           || pv->document()->toPlainText().endsWith(QLatin1String("X")))
        if (pv->document()->isUndoAvailable())
            pv->undo();
        else
            break;
    settle(50);
    check(typed && !plain(p, f.b).contains(QLatin1String("XYZ")), QStringLiteral("Edit > Undo in the preview takes the change back in the document"));
    // its own zoom
    const double editorZoom = p->docs()->zoom();
    const double before = pv->document()->defaultFont().pointSizeF();
    const double listFont = f.insp->projectBookmarkList()->font().pointSizeF();
    wheel(pv->viewport(), 2);
    check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspPreview)), 1.2) && pv->document()->defaultFont().pointSizeF() > before * 1.15
              && qFuzzyCompare(p->docs()->zoom(), editorZoom) && qFuzzyCompare(f.insp->projectBookmarkList()->font().pointSizeF(), listFont),
          QStringLiteral("Ctrl + wheel over the bookmarked text: its text grows (before: only the lists around it); the editor and the lists stay"),
          QStringLiteral("%1 -> %2 pt").arg(before).arg(pv->document()->defaultFont().pointSizeF()));
    auto *title = f.insp->findChild<QLabel *>(QStringLiteral("inspectorZoomTitle"));
    auto *box = f.insp->findChild<QComboBox *>(QStringLiteral("inspectorZoom"));
    pv->setFocus();
    settle(50);
    check(title && title->text() == QLatin1String("Zoom: Text") && title->toolTip().contains(QLatin1String("Bookmarked Text")) && box
              && box->currentText().startsWith(QLatin1String("120")),
          QStringLiteral("... the zoom list at the bottom names the part and shows its zoom"), title ? title->text() + QLatin1Char(' ') + (box ? box->currentText() : QString()) : QString());
    shot(f.w.get(), QStringLiteral("03-preview-zoom-120"));
    // the change is in the file
    QString err;
    check(p->save(&err), QStringLiteral("save"), err);
    const QString kept = plain(p, f.b);
    check(f.reopen(), QStringLiteral("reopen the project"));
    if (!f.p)
        return;
    QTextDocument *d2 = f.p->docs()->text(f.b->id);
    const int at2 = d2->toPlainText().indexOf(QLatin1String("keeper"));
    check(d2->toPlainText() == kept && formatAt(d2, at2).fontFamilies().toStringList().value(0) == QLatin1String("DejaVu Serif"),
          QStringLiteral("after reopening: the text and the font chosen in the preview are in the file"),
          QStringLiteral("%1 / %2").arg(d2->toPlainText(), formatAt(d2, at2).fontFamilies().toStringList().join(QLatin1Char(','))));
    // the preview of the reopened project, and a run of edits in both
    f.insp->showTab(Inspector::Bookmarks);
    f.insp->projectBookmarkList()->setCurrentRow(-1);
    f.insp->projectBookmarkList()->setCurrentRow(0);
    settle(250);
    pv = f.insp->bookmarkPreview();
    // a stress run of edits both ways
    {
        QTextDocument *copy = pv->document();
        QTextDocument *doc = d2;
        DocMirror *m = f.insp->previewMirror();
        QSignalSpy rebuilt(m, &DocMirror::copyReplaced);
        // fixed seeds, so that a failure can be repeated
        const quint32 seed = qEnvironmentVariableIsSet("QB_MIRROR_SEED") ? quint32(qEnvironmentVariableIntValue("QB_MIRROR_SEED")) : 20261022u;
        QRandomGenerator gen(seed);
        auto *rng = &gen;
        int bad = 0;
        QString firstBad;
        for (int i = 0; i < 400; ++i) {
            QTextDocument *d = (i % 2) ? doc : pv->document();
            const int len = d->characterCount() - 1;
            QTextCursor e(d);
            const int pos = int(rng->bounded(qMax(1, len)));
            const int kind = int(rng->bounded(7));
            e.setPosition(pos);
            switch (kind) {
            case 0: e.insertText(QStringLiteral("word%1 ").arg(i)); break;
            case 1:
                e.setPosition(qMin(len, pos + int(rng->bounded(12))), QTextCursor::KeepAnchor);
                e.removeSelectedText();
                break;
            case 2: e.insertBlock(); break;
            case 3: {
                e.setPosition(qMin(len, pos + 8), QTextCursor::KeepAnchor);
                QTextCharFormat cf;
                cf.setFontItalic(true);
                e.mergeCharFormat(cf);
                break;
            }
            case 4: {
                QTextBlockFormat bf = e.blockFormat();
                bf.setAlignment(Qt::AlignHCenter);
                e.setBlockFormat(bf);
                break;
            }
            case 5: {
                QTextListFormat lf;
                lf.setStyle(QTextListFormat::ListDisc);
                e.createList(lf);
                break;
            }
            default: {
                e.setPosition(qMin(len, pos + 5), QTextCursor::KeepAnchor);
                QTextCharFormat cf;
                cf.setForeground(QColor(200, 30, 30));
                cf.setFontPointSize(14);
                e.mergeCharFormat(cf);
                break;
            }
            }
            QCoreApplication::processEvents();
            if (qEnvironmentVariableIsSet("QB_MIRROR_DEBUG"))
                info(QStringLiteral("step %1 kind %2 in %3 at %4: same=%5 lens %6/%7").arg(i).arg(kind).arg(i % 2 ? "document" : "copy").arg(pos)
                         .arg(sameContents(doc, pv->document())).arg(doc->characterCount()).arg(pv->document()->characterCount()));
            if (!sameContents(doc, pv->document())) {
                ++bad;
                if (firstBad.isEmpty())
                    firstBad = QStringLiteral("step %1 (kind %2, %3)").arg(i).arg(kind).arg(i % 2 ? QStringLiteral("document") : QStringLiteral("copy"));
            }
        }
        Q_UNUSED(copy)
        check(bad == 0 && rebuilt.count() == 0, QStringLiteral("400 random edits in both (text, deletions, paragraphs, italics, centering, lists, color and size): "
                                       "the two stay the same after each"),
              QStringLiteral("%1 differ; first: %2; copy made anew %3 times").arg(bad).arg(firstBad).arg(rebuilt.count()));
        info(QStringLiteral("copied to the document %1 times, to the copy %2 times, copy made anew %3 times")
                 .arg(m->toSource()).arg(m->toCopy()).arg(rebuilt.count()));
    }
    zoom::reset(QLatin1String(zoom::kInspPreview));
}

// ------------------------------------------------------------------ zoom per part

static void partsChecks()
{
    section(QStringLiteral("A zoom per part of the inspector and the Objects window"));
    {
        // a zoom of the whole inspector before 0.21.1 is where each part starts
        QSettings s;
        s.setValue(QStringLiteral("zoom/inspector"), 1.4);
        s.remove(QStringLiteral("zoom/inspector.notes"));
        s.remove(QStringLiteral("zoomPartsMigrated"));
        s.sync();
        zoom::migrateParts();
        check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspNotes)), 1.4) && qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspPreview)), 1.4),
              QStringLiteral("the parts start from the inspector's zoom of 0.21.0 (nothing jumps after the update)"));
        for (const QString &k : zoom::areas())
            zoom::setFactor(k, 1.0);
    }
    Fixture f(QStringLiteral("Parts"));
    if (!f.ok)
        return;
    Project *p = f.p;
    f.show(f.b);
    f.insp->showTab(Inspector::Notes);
    settle(200);
    auto *syn = findVisible<QPlainTextEdit>(f.insp, [](QPlainTextEdit *e) { return e->placeholderText().startsWith(QLatin1String("Synopsis")); });
    TextEditor *notes = notesEditor(f.insp);
    check(syn && notes, QStringLiteral("synopsis card and notes"));
    if (!syn || !notes)
        return;
    const double synBefore = syn->font().pointSizeF();
    const double notesBefore = p->docs()->notesZoom();
    wheel(syn->viewport(), 2);
    check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspSynopsis)), 1.2) && syn->font().pointSizeF() > synBefore * 1.15
              && qFuzzyCompare(p->docs()->notesZoom(), notesBefore),
          QStringLiteral("over the synopsis: the card grows, the notes stay"),
          QStringLiteral("%1 -> %2 pt, notes %3").arg(synBefore).arg(syn->font().pointSizeF()).arg(p->docs()->notesZoom()));
    const double synNow = syn->font().pointSizeF();
    wheel(notes->viewport(), 3);
    check(qFuzzyCompare(p->docs()->notesZoom(), 1.3) && qFuzzyCompare(syn->font().pointSizeF(), synNow),
          QStringLiteral("over the notes: the notes grow, the card stays"));
    auto *title = f.insp->findChild<QLabel *>(QStringLiteral("inspectorZoomTitle"));
    check(title && title->text() == QLatin1String("Zoom: Notes"), QStringLiteral("... the zoom list names the part zoomed last"), title ? title->text() : QString());
    shot(f.w.get(), QStringLiteral("04-notes-tab-two-zooms"));

    // the tab row and the bottom row go with the inspector
    QToolButton *tabButton = f.insp->findChild<QToolButton *>(QStringLiteral("inspectorMindMapButton"));
    auto *labelBox = findVisible<QComboBox>(f.insp, [](QComboBox *b) { return b->objectName() != QLatin1String("inspectorZoom") && b->itemText(0).startsWith(QLatin1String("No ")); });
    const double tabFont = tabButton ? tabButton->font().pointSizeF() : 0, labelFont = labelBox ? labelBox->font().pointSizeF() : 0;
    if (tabButton)
        wheel(tabButton, 2);
    check(tabButton && labelBox && qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspector)), 1.2) && tabButton->font().pointSizeF() > tabFont * 1.15
              && labelBox->font().pointSizeF() > labelFont * 1.15,
          QStringLiteral("over the tab buttons: they and the label / status row grow with the inspector (before: they stayed)"),
          QStringLiteral("%1 -> %2, %3 -> %4").arg(tabFont).arg(tabButton ? tabButton->font().pointSizeF() : 0).arg(labelFont).arg(labelBox ? labelBox->font().pointSizeF() : 0));
    // after the dots on the tabs changed (underline): still not grown twice
    info(QStringLiteral("tab font after zoom: %1").arg(tabButton->font().pointSizeF()));
    f.show(f.a);
    info(QStringLiteral("... after showing another document: %1").arg(tabButton->font().pointSizeF()));
    f.show(f.b);
    info(QStringLiteral("... and back: %1").arg(tabButton->font().pointSizeF()));
    wheel(tabButton, 1);
    info(QStringLiteral("... one notch more: %1").arg(tabButton->font().pointSizeF()));
    wheel(tabButton, -1);
    check(qAbs(tabButton->font().pointSizeF() - tabFont * 1.2) < 0.3, QStringLiteral("... and keep that size when the marks on the tabs change"),
          QStringLiteral("%1 (expected %2)").arg(tabButton->font().pointSizeF()).arg(tabFont * 1.2));

    // bookmarks: the lists and the text below them
    p->projectBookmarks.append({f.a->title, QStringLiteral("qb://") + f.a->id});
    p->notifyBookmarksChanged();
    f.insp->showTab(Inspector::Bookmarks);
    settle(150);
    f.insp->projectBookmarkList()->setCurrentRow(0);
    settle(200);
    const double listBefore = f.insp->projectBookmarkList()->font().pointSizeF();
    const double textBefore = f.insp->bookmarkPreview()->document()->defaultFont().pointSizeF();
    wheel(f.insp->projectBookmarkList()->viewport(), 2);
    check(f.insp->projectBookmarkList()->font().pointSizeF() > listBefore * 1.15
              && qFuzzyCompare(f.insp->bookmarkPreview()->document()->defaultFont().pointSizeF(), textBefore),
          QStringLiteral("over the bookmark lists: the lists grow, the bookmarked text stays"));

    // snapshots: the list and the text
    setText(p, f.b, QStringLiteral("A snapshot of the lighthouse text."));
    {
        QTextCursor c(p->docs()->text(f.b->id));
        c.setPosition(2);
        c.setPosition(10, QTextCursor::KeepAnchor);
        QTextCharFormat cf;
        cf.setFontPointSize(20);
        c.mergeCharFormat(cf);
    }
    snapshots::take(p, f.b->id, QStringLiteral("First"));
    f.insp->showTab(Inspector::Snapshots);
    f.insp->setItem(nullptr);
    f.insp->setItem(f.b);
    settle(150);
    auto *snapList = findVisible<QListWidget>(f.insp, [](QListWidget *l) { return l->count() > 0 && l->item(0)->text().startsWith(QLatin1String("First")); });
    QTextBrowser *sv = f.insp->snapshotView();
    if (snapList) {
        snapList->setCurrentRow(0);
        settle(150);
    }
    const double svBefore = sv->document()->defaultFont().pointSizeF();
    const double snapListBefore = snapList ? snapList->font().pointSizeF() : 0;
    double bigBefore = 0;
    for (auto it = sv->document()->begin().begin(); !it.atEnd(); ++it)
        bigBefore = qMax(bigBefore, it.fragment().charFormat().fontPointSize());
    wheel(sv->viewport(), 2);
    double bigAfter = 0;
    for (auto it = sv->document()->begin().begin(); !it.atEnd(); ++it)
        bigAfter = qMax(bigAfter, it.fragment().charFormat().fontPointSize());
    check(snapList && sv->document()->toPlainText().contains(QLatin1String("snapshot of the lighthouse"))
              && sv->document()->defaultFont().pointSizeF() > svBefore * 1.15 && bigAfter > bigBefore * 1.15
              && qFuzzyCompare(snapList->font().pointSizeF(), snapListBefore),
          QStringLiteral("over the text of a snapshot: it grows, also text with a size of its own (before: no zoom at all); the list stays"),
          QStringLiteral("%1 -> %2 pt; 20 pt text: %3 -> %4").arg(svBefore).arg(sv->document()->defaultFont().pointSizeF()).arg(bigBefore).arg(bigAfter));
    shot(f.w.get(), QStringLiteral("05-snapshot-text-zoom"));

    // objects: the upper part with the list, and the sheet
    auto *st = p->objects();
    const QString mira = st->create(QStringLiteral("characters"), QString(), QStringLiteral("Mira"));
    const QString fens = st->create(QStringLiteral("locations"), QString(), QStringLiteral("Misty Fens"));
    f.insp->showTab(Inspector::Objects);
    settle(150);
    ObjectsPanel *op = f.insp->objectsPanel();
    op->showObject(mira);
    settle(200);
    const double treeBefore = op->tree()->font().pointSizeF();
    const double nameBefore = op->form()->nameField()->font().pointSizeF();
    wheel(op->tree()->viewport(), 2);
    check(op->tree()->font().pointSizeF() > treeBefore * 1.15 && qFuzzyCompare(op->form()->nameField()->font().pointSizeF(), nameBefore),
          QStringLiteral("Objects tab, over the list: the categories and the list grow, the sheet stays (the writer's request)"));
    const double treeNow = op->tree()->font().pointSizeF();
    wheel(op->form()->nameField(), 3);
    check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspObjectSheet)), 1.3) && op->form()->nameField()->font().pointSizeF() > nameBefore * 1.25
              && qFuzzyCompare(op->tree()->font().pointSizeF(), treeNow),
          QStringLiteral("... over the sheet: the sheet grows, the list stays"));
    // a sheet of another category: its fields are made anew, in the sheet's zoom
    op->showObject(fens);
    settle(200);
    QList<double> ratios;
    for (QLabel *l : op->form()->findChildren<QLabel *>())
        if (l->isVisible() && !l->text().isEmpty() && l->pixmap().isNull())
            ratios << l->font().pointSizeF() / QApplication::font().pointSizeF();
    bool allZoomed = !ratios.isEmpty();
    for (double r : ratios)
        allZoomed = allZoomed && r > 1.25 && r < 1.75;
    QStringList rs;
    for (double r : ratios)
        rs << QString::number(r, 'f', 2);
    check(allZoomed, QStringLiteral("... the fields of a sheet made later have the sheet's zoom (none at 100 %, none grown twice)"), rs.join(QLatin1String(" ")));
    shot(f.w.get(), QStringLiteral("06-objects-tab-two-zooms"));

    // the Objects window: the same two parts
    if (ObjectsWindow *ow = ObjectsWindow::open(p, mira, f.w.get())) {
        ow->resize(1100, 700);
        settle(300);
        const double wt = ow->panel()->tree()->font().pointSizeF(), wn = ow->panel()->form()->nameField()->font().pointSizeF();
        wheel(ow->panel()->form()->nameField(), 2);
        check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kObjectsSheet)), 1.2) && ow->panel()->form()->nameField()->font().pointSizeF() > wn * 1.15
                  && qFuzzyCompare(ow->panel()->tree()->font().pointSizeF(), wt),
              QStringLiteral("Objects window: the sheet with a zoom of its own (before: one zoom for everything)"));
        wheel(ow->panel()->tree()->viewport(), 1);
        check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kObjects)), 1.1) && qFuzzyCompare(zoom::factor(QLatin1String(zoom::kObjectsSheet)), 1.2),
              QStringLiteral("... and the list with its own"));
        shot(ow, QStringLiteral("07-objects-window-two-zooms"));
        ow->close();
        settle(100);
    }

    // File > Options > Editing > Default text zoom lists every part
    {
        OptionsDialog od(f.w.get(), 0);
        od.showSection(OptionsDialog::SEditing, QStringLiteral("Options"));
        auto *area = od.findChild<QComboBox *>(QStringLiteral("optZoomArea"));
        QStringList missing;
        for (const char *k : {zoom::kInspSynopsis, zoom::kInspNotes, zoom::kInspBookmarks, zoom::kInspPreview, zoom::kInspSnapList,
                              zoom::kInspSnapText, zoom::kInspObjectList, zoom::kInspObjectSheet, zoom::kObjects, zoom::kObjectsSheet, zoom::kMindMap})
            if (!area || area->findData(QLatin1String(k)) < 0)
                missing << QLatin1String(k);
        check(missing.isEmpty(), QStringLiteral("Options: the default zoom of every part can be set"), missing.join(QLatin1String(", ")));
        if (area) {
            area->setCurrentIndex(area->findData(QLatin1String(zoom::kMindMap)));
            od.findChild<QSpinBox *>(QStringLiteral("optZoomPercent"))->setValue(150);
            od.showSection(OptionsDialog::SEditing, QStringLiteral("Options"));
            shot(&od, QStringLiteral("08-options-default-zoom-parts"));
        }
        od.accept();
    }
    // the default zoom of mind maps (did nothing in 0.21.0)
    f.insp->showTab(Inspector::MindMap);
    f.insp->setItem(nullptr);
    f.insp->setItem(f.a);
    settle(300);
    MindMapView *mv = f.insp->mindMapPanel()->view();
    check(mv && qAbs(mv->zoom() - 1.5) < 0.01, QStringLiteral("a board never zoomed opens at the default zoom of mind maps (did nothing in 0.21.0)"),
          mv ? QString::number(mv->zoom()) : QStringLiteral("no view"));
    auto *box = f.insp->findChild<QComboBox *>(QStringLiteral("inspectorZoom"));
    check(box && box->currentText().startsWith(QLatin1String("150")) && title && title->text() == QLatin1String("Zoom: Map"),
          QStringLiteral("... the zoom list shows the board's zoom on the Mind Map tab"), box ? box->currentText() : QString());
    zoom::setDefaultFactor(QLatin1String(zoom::kMindMap), 1.0);
    for (const QString &k : zoom::areas())
        zoom::setFactor(k, 1.0);
}

// ------------------------------------------------------------------ mind map background

static void backgroundChecks()
{
    section(QStringLiteral("Mind map background: palette, light and dark themes, defaults"));
    Fixture f(QStringLiteral("Background"));
    if (!f.ok)
        return;
    Project *p = f.p;
    MindMapPrefs &mp = MindMapPrefs::instance();
    const QColor light0(253, 251, 240), dark0(46, 46, 43);
    mp.boardBackground = light0;
    mp.boardBackgroundDark = dark0;
    mp.save();
    setTheme(QStringLiteral("light"));
    MindMapDoc *da = mindmaps::docFor(p, f.a->id);
    MindMapDoc *db = mindmaps::docFor(p, f.b->id);
    check(da->board.settings.background.rgb() == light0.rgb(), QStringLiteral("a new board in a light theme: the light default"));
    setTheme(QStringLiteral("dark"));
    check(theme::isDark() && da->board.settings.background.rgb() == dark0.rgb(),
          QStringLiteral("... in a dark theme: the dark default (before: the light paper in every theme)"), da->board.settings.background.name());
    // the board's own color, for the kind of theme in use only
    const QColor red(214, 39, 40), blue(31, 100, 200);
    da->setOwnBackground(red);
    check(da->board.settings.background.rgb() == red.rgb() && da->board.settings.ownDark.rgb() == red.rgb() && !da->board.settings.ownLight.isValid(),
          QStringLiteral("a color chosen in a dark theme: the board's own for dark themes"));
    f.insp->showTab(Inspector::MindMap);
    f.insp->setItem(nullptr);
    f.insp->setItem(f.a);
    settle(300);
    shot(f.w.get(), QStringLiteral("09-board-own-color-dark-theme"));
    setTheme(QStringLiteral("light"));
    check(da->board.settings.background.rgb() == light0.rgb(), QStringLiteral("... in a light theme the board shows the light default again"));
    da->setOwnBackground(blue);
    setTheme(QStringLiteral("dark"));
    check(da->board.settings.background.rgb() == red.rgb(), QStringLiteral("... a light color of its own does not change the dark one"));
    setTheme(QStringLiteral("light"));
    check(da->board.settings.background.rgb() == blue.rgb(), QStringLiteral("... and back in a light theme the light one"));
    da->undoStack()->undo();
    check(da->board.settings.background.rgb() == light0.rgb() && !da->board.settings.ownLight.isValid(), QStringLiteral("Undo takes a background color back"));
    da->undoStack()->redo();
    // defaults: boards without a color of their own follow
    {
        OptionsDialog od(f.w.get(), 0);
        od.showSection(OptionsDialog::SExtras, QStringLiteral("Mind Map"));
        auto *lightBtn = od.findChild<QPushButton *>(QStringLiteral("optMmBackgroundLight"));
        auto *darkBtn = od.findChild<QPushButton *>(QStringLiteral("optMmBackgroundDark"));
        check(lightBtn && darkBtn, QStringLiteral("Options > Extras > Mind Map: a background for light and one for dark themes"));
        shot(&od, QStringLiteral("10-options-mind-map-backgrounds"));
        bool chosen = false;
        QTimer::singleShot(150, qApp, [&chosen] {
            auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!m)
                return;
            for (QAction *ac : m->actions())
                if (ac->text().contains(QLatin1String("Green"))) {
                    ac->trigger();
                    chosen = true;
                    break;
                }
            m->close();
        });
        if (lightBtn)
            lightBtn->click();
        settle(300);
        check(chosen, QStringLiteral("... the button shows the shared palette"));
        od.accept();
    }
    settle(100);
    const QColor chosen = mp.boardBackground;
    check(chosen.rgb() != light0.rgb() && mp.boardBackgroundDark.rgb() == dark0.rgb() && db->board.settings.background.rgb() == chosen.rgb()
              && da->board.settings.background.rgb() == blue.rgb(),
          QStringLiteral("a new light default: boards without a color of their own follow at once, a board with its own keeps it"),
          db->board.settings.background.name() + QLatin1Char(' ') + chosen.name());
    // Set As Defaults in the board's window: the default of the kind of theme in use only
    setTheme(QStringLiteral("dark"));
    if (MindMapWindow *mw = MindMapWindow::open(da, f.w.get())) {
        settle(300);
        QPushButton *setDef = nullptr;
        for (QPushButton *b : mw->findChildren<QPushButton *>())
            if (b->text().contains(QLatin1String("Set As Defaults")))
                setDef = b;
        auto *bgLabel = mw->findChild<QLabel *>(QStringLiteral("mmBackgroundLabel"));
        check(bgLabel && bgLabel->text().contains(QLatin1String("dark")), QStringLiteral("Document Settings names the kind of theme of the color"),
              bgLabel ? bgLabel->text() : QString());
        // the color well shows the palette
        bool palette = false;
        QTimer::singleShot(150, qApp, [&palette] {
            auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            palette = m && m->objectName() == QLatin1String("colorWellPalette") && m->actions().size() > 6;
            if (m)
                m->close();
        });
        if (auto *well = mw->findChild<QToolButton *>(QStringLiteral("mmBackground")))
            well->click();
        settle(300);
        check(palette, QStringLiteral("... its color button shows the shared palette (before: the system's color window)"));
        if (setDef)
            setDef->click();
        settle(100);
        check(setDef && mp.boardBackgroundDark.rgb() == red.rgb() && mp.boardBackground.rgb() == chosen.rgb(),
              QStringLiteral("Set As Defaults in a dark theme: the dark default only"));
        shot(mw, QStringLiteral("11-board-window-dark"));
        mw->close();
        settle(100);
    }
    mp.boardBackgroundDark = dark0;
    mp.save();
    mindmaps::refreshAll();
    // the right-click menu of the empty board
    f.insp->showTab(Inspector::MindMap);
    f.insp->setItem(nullptr);
    f.insp->setItem(f.b);
    settle(300);
    if (MindMapController *ctl = f.insp->mindMapPanel()->controller()) {
        QMenu menu;
        ctl->fillContextMenu(&menu);
        QMenu *bg = menu.findChild<QMenu *>(QStringLiteral("mm_menuBackground"));
        QStringList names;
        QAction *blueAction = nullptr;
        if (bg)
            for (QAction *ac : bg->actions()) {
                names << ac->text();
                if (ac->text().contains(QLatin1String("Blue")))
                    blueAction = ac;
            }
        check(bg && names.size() >= 8 && names.join(QLatin1Char(' ')).contains(QLatin1String("Default for Dark Themes")),
              QStringLiteral("right-click on the empty board: Background Color with the palette and \"Default\""), names.join(QLatin1String(" | ")));
        if (blueAction)
            blueAction->trigger();
        settle(100);
        check(db->board.settings.background.rgb() == blue.rgb() && db->board.settings.ownDark.rgb() == blue.rgb(),
              QStringLiteral("... a color chosen there is the board's own"));
    }
    // an empty board with a color of its own is kept
    da->save();
    db->save();
    const QString pathA = da->path();
    QFile fa(pathA);
    const bool fileThere = fa.open(QIODevice::ReadOnly);
    const QString xml = QString::fromUtf8(fa.readAll());
    check(fileThere && xml.contains(QLatin1String("QuillbinderBackground")) && xml.contains(QLatin1String("BackgroundColor")),
          QStringLiteral("an empty board with a color of its own is written (before: lost, no file for an empty board)"));
    check(!mindmaps::hasMap(p, f.a->id) || !da->board.isEmpty(), QStringLiteral("... the Mind Map tab is not marked for a board without notes"));
    QString err;
    p->save(&err);
    check(f.reopen(), QStringLiteral("reopen"));
    if (!f.p)
        return;
    MindMapDoc *da2 = mindmaps::docFor(f.p, f.a->id);
    check(!mindmaps::hasMap(f.p, f.a->id) || da2->board.isEmpty() == false, QStringLiteral("... not marked after reopening either"));
    check(da2->board.settings.ownLight.rgb() == blue.rgb() && da2->board.settings.ownDark.rgb() == red.rgb()
              && da2->board.settings.background.rgb() == (theme::isDark() ? red : blue).rgb(),
          QStringLiteral("after reopening: both colors of the board are there"),
          QStringLiteral("light %1, dark %2, shown %3").arg(da2->board.settings.ownLight.name(), da2->board.settings.ownDark.name(),
                                                          da2->board.settings.background.name()));
    // a file of an older version: one color; the board's own unless it was the default
    {
        BinderItem *c = f.p->model()->createItem(f.p->draft(), -1, ItemType::Text, QStringLiteral("Old Board"));
        BinderItem *d = f.p->model()->createItem(f.p->draft(), -1, ItemType::Text, QStringLiteral("Old Default Board"));
        auto writeOld = [&](BinderItem *it, const QString &color) {
            mindmap::Board old;
            mindmap::Note n;
            n.id = 1;
            n.text = QStringLiteral("Note");
            old.notes << n;
            QString x = QString::fromUtf8(old.toXml());
            x.remove(QRegularExpression(QStringLiteral("<QuillbinderBackground[^>]*/>")));
            x.replace(QRegularExpression(QStringLiteral("<BackgroundColor>[^<]*</BackgroundColor>")),
                      QStringLiteral("<BackgroundColor>%1</BackgroundColor>").arg(color));
            QDir().mkpath(f.p->contentDir(it->id));
            QFile out(mindmap::pathFor(f.p->contentDir(it->id)));
            out.open(QIODevice::WriteOnly);
            out.write(x.toUtf8());
        };
        writeOld(c, QStringLiteral("0.5 0.25 0.75"));
        const QColor def = MindMapPrefs::instance().boardBackground;
        writeOld(d, QStringLiteral("%1 %2 %3").arg(def.redF(), 0, 'f', 6).arg(def.greenF(), 0, 'f', 6).arg(def.blueF(), 0, 'f', 6));
        MindMapDoc *dc = mindmaps::docFor(f.p, c->id), *dd = mindmaps::docFor(f.p, d->id);
        check(dc->board.settings.ownLight.isValid() && !dd->board.settings.ownLight.isValid(),
              QStringLiteral("a board of 0.21.0 or older: its color is its own, unless it was the default of that time"));
    }
    setTheme(QStringLiteral("light"));
}

// ------------------------------------------------------------------ places

static void placesChecks()
{
    section(QStringLiteral("Places kept: documents, inspector, mind maps, pictures and PDF files"));
    Fixture f(QStringLiteral("Places"));
    if (!f.ok)
        return;
    Project *p = f.p;
    f.show(f.a);
    EditorPane *pane = firstPane(*f.w);
    TextEditor *e = f.editor();
    TextColumnView *col = pane->textView();
    // somewhere in the middle of the long text
    const int target = plain(p, f.a).indexOf(QLatin1String("paragraph 80:"));
    QTextCursor c = e->textCursor();
    c.setPosition(target);
    e->setTextCursor(c);
    e->setFocus();
    col->ensureCursorVisible(e);
    settle(200);
    const int scrollA = col->scrollValue();
    check(scrollA > 500, QStringLiteral("the long text is scrolled to its middle"), QString::number(scrollA));
    f.show(f.b);
    f.show(f.a);
    settle(200);
    e = f.editor();
    check(e && e->textCursor().position() == target && qAbs(col->scrollValue() - scrollA) < 60,
          QStringLiteral("back to a document: the cursor and the view where they were (before: the top)"),
          QStringLiteral("cursor %1 (expected %2), scroll %3 (was %4)").arg(e ? e->textCursor().position() : -1).arg(target).arg(col->scrollValue()).arg(scrollA));
    shot(f.w.get(), QStringLiteral("12-document-place-kept"));
    // something else that sets the cursor wins (a search result, a link)
    f.show(f.b);
    pane->showItems({f.a}, true);
    e = f.editor();
    QTextCursor other = e->textCursor();
    other.setPosition(15);
    e->setTextCursor(other);
    settle(250);
    check(e->textCursor().position() == 15, QStringLiteral("... unless something else put the cursor somewhere (search results, links)"));
    // put it back where it was for the next checks
    other.setPosition(target);
    e->setTextCursor(other);
    col->ensureCursorVisible(e);
    settle(100);
    // Combined text: the group remembers which document and where
    BinderItem *c1 = p->model()->createItem(f.folder, -1, ItemType::Text, QStringLiteral("Evening"));
    setText(p, c1, longText(40, QStringLiteral("Evening")));
    pane->showItems({f.folder}, true, int(ViewMode::Combined));
    settle(300);
    TextEditor *ed1 = nullptr;
    for (TextEditor *t : col->editors())
        if (t->item() == c1)
            ed1 = t;
    const int tc1 = plain(p, c1).indexOf(QLatin1String("paragraph 20:"));
    if (ed1) {
        QTextCursor cc = ed1->textCursor();
        cc.setPosition(tc1);
        ed1->setTextCursor(cc);
        ed1->setFocus();
        col->ensureCursorVisible(ed1);
        settle(150);
    }
    const int scrollS = col->scrollValue();
    f.show(f.b);
    pane->showItems({f.folder}, true, int(ViewMode::Combined));
    settle(350);
    TextEditor *act = col->activeEditor();
    check(ed1 && act && act->item() == c1 && act->textCursor().position() == tc1 && qAbs(col->scrollValue() - scrollS) < 80,
          QStringLiteral("Combined text: the document with the cursor and the view kept for the group"),
          QStringLiteral("%1 at %2, scroll %3 (was %4)").arg(act && act->item() ? act->item()->title : QString()).arg(act ? act->textCursor().position() : -1).arg(col->scrollValue()).arg(scrollS));
    // the copyholder shows a document where the writer was in it
    f.show(f.b);
    pane->openCopyholder(f.a);
    settle(350);
    TextEditor *held = nullptr;
    if (pane->copyholder())
        for (TextEditor *t : pane->copyholder()->findChildren<TextEditor *>())
            if (t->item() == f.a)
                held = t;
    check(held && held->textCursor().position() == target, QStringLiteral("the copyholder opens a document at its place as well"),
          held ? QString::number(held->textCursor().position()) : QStringLiteral("no editor"));
    // the notes in the inspector, per document
    f.show(f.a);
    f.insp->showTab(Inspector::Notes);
    settle(150);
    if (TextEditor *notes = notesEditor(f.insp)) {
        QTextCursor nc(notes->document());
        nc.insertText(longText(30, QStringLiteral("Note")));
        nc.setPosition(notes->document()->toPlainText().indexOf(QLatin1String("paragraph 12:")));
        notes->setTextCursor(nc);
        const int notesPos = nc.position();
        f.show(f.b);
        f.show(f.a);
        settle(150);
        check(notesEditor(f.insp) && notesEditor(f.insp)->textCursor().position() == notesPos,
              QStringLiteral("the notes in the inspector: back at their place after another document"),
              QString::number(notesEditor(f.insp) ? notesEditor(f.insp)->textCursor().position() : -1));
    }
    // the inspector: tab, dividers, bookmark, object
    p->projectBookmarks.append({f.b->title, QStringLiteral("qb://") + f.b->id});
    p->notifyBookmarksChanged();
    const QString mira = p->objects()->create(QStringLiteral("characters"), QString(), QStringLiteral("Mira"));
    f.insp->showTab(Inspector::Objects);
    settle(150);
    f.insp->objectsPanel()->showObject(mira);
    f.insp->showTab(Inspector::Bookmarks);
    settle(150);
    f.insp->projectBookmarkList()->setCurrentRow(0);
    settle(150);
    auto *bsplit = f.insp->findChild<QSplitter *>(QStringLiteral("inspectorBookmarkSplit"));
    QList<int> sizes = bsplit ? bsplit->sizes() : QList<int>();
    if (bsplit && sizes.size() == 3) {
        const int total = sizes[0] + sizes[1] + sizes[2];
        bsplit->setSizes({total / 6, total / 6, total - 2 * (total / 6)});
        settle(50);
        sizes = bsplit->sizes();
    }
    // a mind map: zoom and place in the inspector and in the window, apart
    MindMapDoc *doc = mindmaps::docFor(p, f.a->id);
    doc->change(QStringLiteral("notes"), [doc] {
        for (int i = 0; i < 6; ++i) {
            mindmap::Note n;
            n.id = i + 1;
            n.text = QStringLiteral("Note %1").arg(i + 1);
            n.pos = QPointF(i * 260, i * 140);
            n.width = 160;
            doc->board.notes << n;
        }
    });
    f.show(f.a);
    f.insp->showTab(Inspector::MindMap);
    settle(300);
    MindMapView *mv = f.insp->mindMapPanel()->view();
    if (mv) {
        mv->setZoom(1.7);
        mv->centerOn(QPointF(900, 520));
        settle(600);
    }
    const QPointF centerIns = mv ? mv->visibleCenter() : QPointF();
    f.show(f.b);
    f.show(f.a);
    settle(300);
    mv = f.insp->mindMapPanel()->view();
    check(mv && qAbs(mv->zoom() - 1.7) < 0.01 && QLineF(mv->visibleCenter(), centerIns).length() < 30,
          QStringLiteral("a mind map in the inspector: back to its zoom and place after another document"),
          mv ? QStringLiteral("%1 at %2,%3 (was %4,%5)").arg(mv->zoom()).arg(mv->visibleCenter().x()).arg(mv->visibleCenter().y()).arg(centerIns.x()).arg(centerIns.y()) : QString());
    if (MindMapWindow *mw = MindMapWindow::open(doc, f.w.get())) {
        mw->resize(1000, 700);
        settle(400);
        const bool fresh = qAbs(mw->view()->zoom() - 1.7) > 0.01;
        mw->view()->setZoom(0.6);
        settle(600);
        mw->close();
        settle(200);
        MindMapWindow *mw2 = MindMapWindow::open(doc, f.w.get());
        settle(400);
        check(fresh && mw2 && qAbs(mw2->view()->zoom() - 0.6) < 0.01 && qAbs(f.insp->mindMapPanel()->view()->zoom() - 1.7) < 0.01,
              QStringLiteral("... the large window keeps a zoom of its own for the board"),
              mw2 ? QString::number(mw2->view()->zoom()) : QString());
        if (mw2)
            mw2->close();
        settle(100);
    }
    // a picture and a PDF file
    BinderItem *pic = p->model()->createItem(p->research(), -1, ItemType::Image, QStringLiteral("Lighthouse Picture"));
    {
        QImage img(1600, 1100, QImage::Format_RGB32);
        img.fill(QColor(40, 90, 160));
        pic->fileExt = QStringLiteral("png");
        QDir().mkpath(p->contentDir(pic->id));
        img.save(p->mediaFilePath(pic));
    }
    BinderItem *pdf = p->model()->createItem(p->research(), -1, ItemType::Pdf, QStringLiteral("Tide Tables"));
    {
        pdf->fileExt = QStringLiteral("pdf");
        QDir().mkpath(p->contentDir(pdf->id));
        QPdfWriter pw(p->mediaFilePath(pdf));
        pw.setPageSize(QPageSize(QPageSize::A5));
        QPainter pp(&pw);
        for (int i = 0; i < 4; ++i) {
            if (i)
                pw.newPage();
            pp.drawText(QPointF(400, 800), QStringLiteral("Tide table page %1").arg(i + 1));
        }
        pp.end();
    }
    f.show(pic);
    pane->media()->zoomBy(2.0);
    settle(100);
    f.show(f.b);
    f.show(pic);
    settle(200);
    const viewmem::MediaPlace mpic = viewmem::mediaPlace(p, pic->id);
    check(mpic.zoom > 0 && pane->media()->footerInfo().contains(QLatin1String("1600")),
          QStringLiteral("a picture keeps its zoom"), QString::number(mpic.zoom));
    f.show(pdf);
    settle(300);
    pane->media()->zoomBy(1.5);
    pane->media()->pdfPage(2);
    settle(300);
    f.show(f.b);
    f.show(pdf);
    settle(500);
    check(pane->media()->footerInfo().startsWith(QLatin1String("Page 3")) && viewmem::mediaPlace(p, pdf->id).zoom > 0,
          QStringLiteral("a PDF file keeps its zoom and page"), pane->media()->footerInfo());
    // after reopening the project
    f.show(f.a);
    f.insp->showTab(Inspector::Bookmarks);
    f.insp->projectBookmarkList()->setCurrentRow(0);
    settle(150);
    if (bsplit)
        bsplit->setSizes(sizes);
    sizes = bsplit ? bsplit->sizes() : QList<int>();
    settle(100);
    QString err;
    p->save(&err);
    check(f.reopen(), QStringLiteral("reopen the project"));
    if (!f.p)
        return;
    settle(400);
    pane = firstPane(*f.w);
    e = f.editor();
    check(e && e->item() == f.a && e->textCursor().position() == target,
          QStringLiteral("after reopening: the document at its place"), e ? QString::number(e->textCursor().position()) : QString());
    f.show(f.b);
    f.show(f.a);
    e = f.editor();
    check(e && e->textCursor().position() == target, QStringLiteral("... and again after another document"));
    auto *bsplit2 = f.insp->findChild<QSplitter *>(QStringLiteral("inspectorBookmarkSplit"));
    const QList<int> sizes2 = bsplit2 ? bsplit2->sizes() : QList<int>();
    bool sizesOk = sizes2.size() == sizes.size() && !sizes.isEmpty();
    for (int i = 0; sizesOk && i < sizes.size(); ++i)
        sizesOk = qAbs(sizes2[i] - sizes[i]) <= 4;
    check(f.insp->currentTab() == Inspector::Bookmarks && f.insp->projectBookmarkList()->currentRow() == 0 && f.insp->bookmarkPreview()->isVisible()
              && sizesOk,
          QStringLiteral("... the inspector: its tab, its dividers and the bookmark shown"),
          QStringLiteral("tab %1, row %2, sizes %3 / %4").arg(f.insp->currentTab()).arg(f.insp->projectBookmarkList()->currentRow())
              .arg([&] { QStringList l; for (int v : sizes) l << QString::number(v); return l.join(QLatin1Char(',')); }())
              .arg([&] { QStringList l; for (int v : sizes2) l << QString::number(v); return l.join(QLatin1Char(',')); }()));
    check(f.insp->objectsPanel()->currentObject() == mira, QStringLiteral("... the object shown in the Objects tab"));
    f.insp->showTab(Inspector::MindMap);
    settle(400);
    mv = f.insp->mindMapPanel()->view();
    check(mv && qAbs(mv->zoom() - 1.7) < 0.01, QStringLiteral("... the mind map's zoom and place"), mv ? QString::number(mv->zoom()) : QString());
    shot(f.w.get(), QStringLiteral("13-after-reopening"));
}

// ------------------------------------------------------------------ objects: formatted text

struct RunInfo {
    QString text;
    bool bold, italic, underline, strike;
    QRgb fg, bg;
    bool operator==(const RunInfo &o) const
    {
        return text == o.text && bold == o.bold && italic == o.italic && underline == o.underline && strike == o.strike && fg == o.fg && bg == o.bg;
    }
};
static QList<RunInfo> runs(QTextDocument *d)
{
    QList<RunInfo> l;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (cf.isAnchor())
                continue;
            RunInfo r{it.fragment().text(), cf.fontWeight() >= QFont::DemiBold, cf.fontItalic(), cf.fontUnderline(), cf.fontStrikeOut(),
                      cf.hasProperty(QTextFormat::ForegroundBrush) ? cf.foreground().color().rgb() : 0u,
                      cf.hasProperty(QTextFormat::BackgroundBrush) ? cf.background().color().rgb() : 0u};
            if (!l.isEmpty() && l.last().bold == r.bold && l.last().italic == r.italic && l.last().underline == r.underline
                && l.last().strike == r.strike && l.last().fg == r.fg && l.last().bg == r.bg)
                l.last().text += r.text;
            else
                l << r;
        }
        if (!l.isEmpty())
            l.last().text += QLatin1Char('\n');
    }
    return l;
}
static QString describe(const QList<RunInfo> &l)
{
    QStringList s;
    for (const RunInfo &r : l)
        s << QStringLiteral("[%1%2%3%4 %5 %6]%7").arg(r.bold ? "b" : "").arg(r.italic ? "i" : "").arg(r.underline ? "u" : "").arg(r.strike ? "s" : "")
                 .arg(QColor(r.fg).name(), QColor(r.bg).name(), r.text.simplified());
    return s.join(QLatin1Char(' '));
}

static void objectTextChecks()
{
    section(QStringLiteral("Formatting in the text fields of objects"));
    Fixture f(QStringLiteral("Object text"));
    if (!f.ok)
        return;
    Project *p = f.p;
    f.show(f.b);
    TextEditor *main = f.editor();
    const QString mainBefore = main ? main->document()->toHtml() : QString();
    const QString mira = p->objects()->create(QStringLiteral("characters"), QString(), QStringLiteral("Mira"));
    f.insp->showTab(Inspector::Objects);
    settle(150);
    ObjectsPanel *op = f.insp->objectsPanel();
    op->showObject(mira);
    settle(200);
    auto *field = qobject_cast<ObjectTextEdit *>(op->form()->fieldWidget(QStringLiteral("background")));
    check(field != nullptr, QStringLiteral("the field \"Background story\""));
    if (!field)
        return;
    f.w->activateWindow();
    field->setFocus();
    QTest::keyClicks(field, QStringLiteral("Born in the north, she keeps a red notebook and a highlighted map."));
    settle(100);
    check(f.w->formatObjectTarget() == field, QStringLiteral("the format bar acts on the field with the cursor"));
    selectWord(field, QStringLiteral("north"));
    byKey(f.w.get(), QStringLiteral("bold"))->trigger();
    settle(50);
    selectWord(field, QStringLiteral("Born"));
    byKey(f.w.get(), QStringLiteral("italic"))->trigger();
    selectWord(field, QStringLiteral("keeps"));
    byKey(f.w.get(), QStringLiteral("underline"))->trigger();
    selectWord(field, QStringLiteral("she"));
    byKey(f.w.get(), QStringLiteral("strike"))->trigger();
    settle(50);
    // the color buttons of the format bar
    ColorPickButton *textBtn = nullptr, *highBtn = nullptr;
    for (ColorPickButton *b : f.w->findChildren<ColorPickButton *>())
        (b->kind() == ColorPickButton::TextColor ? textBtn : highBtn) = b;
    selectWord(field, QStringLiteral("red notebook"));
    if (textBtn)
        emit textBtn->applyColor(QColor(214, 39, 40));
    selectWord(field, QStringLiteral("highlighted map"));
    if (highBtn)
        emit highBtn->applyColor(QColor(255, 221, 0));
    selectWord(field, QStringLiteral("notebook"));
    byKey(f.w.get(), QStringLiteral("bold"))->trigger(); // bold and red together
    settle(100);
    QTextDocument *d = field->document();
    const QString t = d->toPlainText();
    check(formatAt(d, t.indexOf(QLatin1String("north"))).fontWeight() >= QFont::DemiBold && formatAt(d, t.indexOf(QLatin1String("Born"))).fontItalic()
              && formatAt(d, t.indexOf(QLatin1String("keeps"))).fontUnderline() && formatAt(d, t.indexOf(QLatin1String("she"))).fontStrikeOut()
              && formatAt(d, t.indexOf(QLatin1String("red"))).foreground().color().rgb() == QColor(214, 39, 40).rgb()
              && formatAt(d, t.indexOf(QLatin1String("map"))).background().color().rgb() == QColor(255, 221, 0).rgb(),
          QStringLiteral("bold, italic, underline, strikethrough, text color and highlight from the format bar"));
    check(main && main->document()->toHtml() == mainBefore, QStringLiteral("... the text in the main editor stays as it was (before: Ctrl+B there)"));
    check(!findVisible<QFontComboBox>(f.w.get())->isEnabled() && byKey(f.w.get(), QStringLiteral("bold"))->isEnabled()
              && !byKey(f.w.get(), QStringLiteral("alignCenter"))->isEnabled(),
          QStringLiteral("... the format bar offers what the field can keep (no fonts, no paragraphs)"));
    for (QWidget *x = field->parentWidget(); x; x = x->parentWidget())
        if (auto *sa = qobject_cast<QScrollArea *>(x)) {
            sa->ensureWidgetVisible(field, 20, 40);
            break;
        }
    shot(f.w.get(), QStringLiteral("14-object-text-formatting"));
    // kept as Markdown with spans; read back the same
    const QString md = field->markdownText();
    ObjectTextEdit back;
    back.setMarkdownText(md);
    const QList<RunInfo> r1 = runs(d), r2 = runs(back.document());
    check(r1 == r2, QStringLiteral("written as Markdown and read back: the same text and formats"), describe(r1) + QStringLiteral("\n   vs ") + describe(r2));
    check(md.contains(QLatin1String("color:#d62728")) && md.contains(QLatin1String("**north**")), QStringLiteral("... colors as HTML spans, bold as Markdown"), md);
    // characters Markdown or HTML would read as markup
    {
        ObjectTextEdit e2;
        e2.setPlainText(QStringLiteral("5*3 & <tag> _x_ [y] `z` ~w~ \\ # done"));
        QTextCursor cc(e2.document());
        cc.select(QTextCursor::Document);
        e2.setTextCursor(cc);
        e2.applyTextColor(QColor(31, 100, 200));
        ObjectTextEdit e3;
        e3.setMarkdownText(e2.markdownText());
        check(e3.document()->toPlainText() == e2.document()->toPlainText() && runs(e3.document()) == runs(e2.document()),
              QStringLiteral("... also colored text with * & < _ [ ` ~ \\ #"), e3.document()->toPlainText() + QStringLiteral(" / ") + e2.markdownText());
    }
    // in the object's file, and after reopening
    field->clearFocus();
    f.binder->tree()->setFocus();
    settle(200);
    QString err;
    p->save(&err);
    check(f.reopen(), QStringLiteral("reopen"));
    if (!f.p)
        return;
    f.insp->showTab(Inspector::Objects);
    settle(150);
    f.insp->objectsPanel()->showObject(mira);
    settle(200);
    auto *field2 = qobject_cast<ObjectTextEdit *>(f.insp->objectsPanel()->form()->fieldWidget(QStringLiteral("background")));
    check(field2 && runs(field2->document()) == r1, QStringLiteral("after reopening: the field with its formats"),
          field2 ? describe(runs(field2->document())) : QString());
    // the Objects window (Window > Objects): the shortcuts reach its fields
    byKey(f.w.get(), QStringLiteral("objectsWindow"))->trigger();
    settle(300);
    if (ObjectsWindow *ow = ObjectsWindow::existing(f.p)) {
        settle(300);
        ow->activateWindow();
        auto *wf = qobject_cast<ObjectTextEdit *>(ow->panel()->form()->fieldWidget(QStringLiteral("goals")));
        if (wf) {
            wf->setFocus();
            QTest::keyClicks(wf, QStringLiteral("Find her brother."));
            selectWord(wf, QStringLiteral("brother"));
            settle(50);
            QTest::keySequence(ow->windowHandle(), QKeySequence::Bold);
            settle(100);
        }
        check(wf && formatAt(wf->document(), wf->document()->toPlainText().indexOf(QLatin1String("brother"))).fontWeight() >= QFont::DemiBold,
              QStringLiteral("in the Objects window, Ctrl+B in a field makes bold"));
        ow->close();
        settle(100);
    }
}

// ------------------------------------------------------------------ credits

static void creditsChecks()
{
    section(QStringLiteral("Credits"));
    const QString about = MainWindow::aboutText();
    // (0.40: Claude is the only credit; the Project Manager line was taken out at the user's wish)
    check(!about.contains(QLatin1String("Project Manager")) && about.contains(QLatin1String("Main Developer: Claude.AI w. Opus 5.5 Extra")),
          QStringLiteral("Help > About names the Main Developer"));
    // (0.21.2: any later version)
    check(about.contains(QLatin1String(QB_APP_VERSION))
              && QVersionNumber::fromString(QLatin1String(QB_APP_VERSION)) >= QVersionNumber(0, 21, 1),
          QStringLiteral("... and the version (0.21.1 or later)"));
    QFile readme(QStringLiteral(QB_TEST_SOURCE_DIR "/../README.md"));
    if (readme.open(QIODevice::ReadOnly)) {
        const QString r = QString::fromUtf8(readme.readAll());
        check(!r.contains(QLatin1String("Project Manager")) && r.contains(QLatin1String("Main Developer: Claude.AI w. Opus 5.5 Extra")),
              QStringLiteral("the README has the credits"));
    }
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qunsetenv("QB_NO_DIALOGS");
    setupApp(QStringLiteral("QuillbinderV0211Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 && argv[1][0] != '-' ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    styleApp();
    zoom::install();
    // a window nobody answers would stop the run: it is named and closed after 6 s
    auto *stray = new QTimer(&app);
    stray->setInterval(1000);
    QObject::connect(stray, &QTimer::timeout, [] {
        static QPointer<QWidget> seen;
        static int ticks = 0;
        QWidget *m = QApplication::activeModalWidget();
        if (!m) {
            ticks = 0;
            return;
        }
        if (m != seen) {
            seen = m;
            ticks = 0;
        }
        if (++ticks < 6)
            return;
        const auto *box = qobject_cast<QMessageBox *>(m);
        check(false, QStringLiteral("(a window stayed open: %1 \"%2\")").arg(QString::fromLatin1(m->metaObject()->className()), box ? box->text() : m->windowTitle()));
        m->close();
        ticks = 0;
    });
    stray->start();

    partsChecks();
    formatTargetChecks();
    previewChecks();
    backgroundChecks();
    placesChecks();
    objectTextChecks();
    creditsChecks();

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
