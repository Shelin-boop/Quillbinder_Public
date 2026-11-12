// The system test, stage 2: views and editors (S-V01 to S-V11) and the inspector (S-I01 to S-I10), section 6.2.
// S-I11 is in stage 1 (cs_cases.cpp).
#include "cs.h"

#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/Snapshots.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/FilterBar.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapUi.h"
#include "ui/MindMapView.h"
#include "ui/Outliner.h"
#include "ui/TextEditor.h"
#include "ui/TextColumnView.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSlider>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QWheelEvent>

#include <random>

namespace cs {

using namespace qb;

namespace {

BinderItem *chapterOf(Project *p, int n)
{
    int k = 0;
    for (BinderItem *part : p->draft()->children)
        for (BinderItem *ch : part->children)
            if (!ch->children.isEmpty() && ++k == n)
                return ch;
    return p->draft()->children.value(0);
}

void typeAtEnd(TextEditor *e, const QString &s)
{
    QTextCursor c = e->textCursor();
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    e->setFocus(Qt::OtherFocusReason);
    typeInto(e, s);
}

void wheel(QWidget *target, bool zoomIn)
{
    const QPoint at = target->rect().center();
    QWheelEvent ev(at, target->mapToGlobal(at), QPoint(), QPoint(0, zoomIn ? 120 : -120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &ev);
    settle(40);
}

Inspector *inspectorOf(MainWindow *w) { return w->findChild<Inspector *>(); }

QWidget *visibleOf(MainWindow *w, const char *className)
{
    for (QWidget *x : w->findChildren<QWidget *>())
        if (x->inherits(className) && x->isVisible())
            return x;
    return nullptr;
}

QList<QWidget *> quickRefs()
{
    QList<QWidget *> out;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl->isVisible() && tl->objectName().startsWith(QLatin1String("quickRef")))
            out << tl;
    return out;
}

} // namespace

// ================================================================ S-V01 Combined Text
void caseSV01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *chapter = chapterOf(p, 3);
    selectItems(w, {chapter});
    chooseByMenu(w, QStringLiteral("viewText"));
    settle(500);
    chooseByMenu(w, QStringLiteral("scrivTitles"));
    chooseByMenu(w, QStringLiteral("scrivTitles"));
    TextEditor *e = visiblePane(w) ? visiblePane(w)->currentEditor() : nullptr;
    check(e != nullptr, QStringLiteral("Combined Text of a chapter is shown"));
    QMap<QString, QString> want;
    for (BinderItem *sc : chapter->children)
        want.insert(sc->id, docText(p, sc));
    // type at the ends and starts of the documents (each document is an editor of its own in Combined Text)
    QList<TextEditor *> editors;
    for (TextEditor *x : visiblePane(w)->findChildren<TextEditor *>())
        if (x->isVisible())
            editors << x;
    fact(QStringLiteral("S-V01 editors in Combined Text"), int(editors.size()));
    for (TextEditor *x : editors) {
        QTextCursor c(x->document());
        c.movePosition(QTextCursor::End);
        x->setTextCursor(c);
        x->setFocus(Qt::OtherFocusReason);
        typeInto(x, QStringLiteral(" End mark."));
        c.movePosition(QTextCursor::Start);
        x->setTextCursor(c);
        typeInto(x, QStringLiteral("Start mark. "));
    }
    for (BinderItem *sc : chapter->children)
        want[sc->id] = QStringLiteral("Start mark. ") + want.value(sc->id) + QStringLiteral(" End mark.");
    // Ctrl+1 / Ctrl+2 / Ctrl+3 while typing
    for (int i = 0; i < 3 && !editors.isEmpty(); ++i) {
        // to whatever has the focus now: Ctrl+1 replaces the editors (the stored one is gone: shakedown 2026-11-09)
        QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : static_cast<QWidget *>(w);
        QTest::keyClick(target, Qt::Key(Qt::Key_1 + i), Qt::ControlModifier);
        settle(300);
    }
    selectItems(w, {chapter});
    chooseByMenu(w, QStringLiteral("viewText"));
    settle(400);
    QStringList wrong;
    for (BinderItem *sc : chapter->children)
        if (docText(p, sc).replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n')) != QString(want.value(sc->id)).replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n')))
            wrong << sc->title;
    check(wrong.isEmpty(), QStringLiteral("each scene's text equals the model"), wrong.join(QLatin1String(", ")));
    editors.clear();
    for (TextEditor *x : visiblePane(w)->findChildren<TextEditor *>())
        if (x->isVisible())
            editors << x;
    if (!editors.isEmpty()) {
        QTextCursor c(editors.last()->document());
        c.movePosition(QTextCursor::End);
        editors.last()->setTextCursor(c);
        for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
            typingGate(editors.last(), profile, 60, QStringLiteral("S-V01"));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-V02 corkboard
void caseSV02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *chapter = chapterOf(p, 2);
    selectItems(w, {chapter});
    settle(300);
    // View > Corkboard toggles: a folder the project shows as a corkboard already would go back to the text
    if (QAction *vc = action(w, QStringLiteral("viewCork")); vc && !vc->isChecked())
        chooseByMenu(w, QStringLiteral("viewCork"));
    settle(500);
    auto *cork = qobject_cast<Corkboard *>(visibleOf(w, "qb::Corkboard"));
    check(cork != nullptr, QStringLiteral("the corkboard is shown"));
    if (!cork) {
        finishProject(w);
        delete w;
        return;
    }
    // titles and synopses of cards, through the inspector while the card is selected
    for (BinderItem *sc : chapter->children) {
        cork->selectItems({sc});
        settle(80);
        sc->synopsis += QStringLiteral(" (card edit)");
        p->notifyItemChanged(sc);
    }
    // Freeform, Commit Freeform Order, Arrange by Label, Status Stamps, label colors
    chooseByMenu(w, QStringLiteral("corkFreeform"));
    settle(200);
    const QList<BinderItem *> order = cork->shownItems();
    if (order.size() >= 2) {
        const QPoint from = cork->mapFromScene(cork->sceneRect().topLeft() + QPointF(60, 40));
        realDrag(cork->viewport(), from, cork->viewport(), from + QPoint(600, 260));
    }
    answerBoxLater(300, QMessageBox::Yes);
    chooseByMenu(w, QStringLiteral("corkCommit"));
    settle(300);
    chooseByMenu(w, QStringLiteral("corkFreeform"));
    for (const QString &a : {QStringLiteral("corkLabels"), QStringLiteral("corkLabels"), QStringLiteral("stamps"), QStringLiteral("stamps"), QStringLiteral("tintCards"),
                             QStringLiteral("tintCards")}) {
        chooseByMenu(w, a);
        settle(150);
    }
    QStringList binderOrder, shown;
    for (BinderItem *sc : chapter->children)
        binderOrder << sc->title;
    for (BinderItem *sc : cork->shownItems())
        shown << sc->title;
    check(binderOrder == shown, QStringLiteral("the binder order follows the committed order"), binderOrder.join(QLatin1String(",")) + QStringLiteral(" / ") + shown.join(QLatin1String(",")));
    // select several and delete
    cork->selectItems(chapter->children.mid(0, 2));
    QTest::keyClick(cork->viewport(), Qt::Key_Delete);
    settle(300);
    // the card size slider kept when the settings are read again
    auto *slider = w->findChild<QSlider *>(QStringLiteral("cardSizeSlider"));
    if (slider) {
        slider->setValue(slider->minimum() + (slider->maximum() - slider->minimum()) * 3 / 4);
        settle(200);
        const int v = slider->value();
        withModal<QDialog>(400, [](QDialog *d) { d->accept(); });
        chooseByMenu(w, QStringLiteral("options"));
        settle(500);
        check(slider->value() == v, QStringLiteral("the card size slider keeps its place after File > Options with OK"), QString::number(slider->value()));
    }
    checkCleanWindows(w);
    finishProject(w);
    delete w;
    // a corkboard of 300 cards
    MainWindow *w2 = openWindow(fixtureCopy(QStringLiteral("fx2-scriv")));
    if (Project *q = w2->project()) {
        BinderItem *big = nullptr;
        for (BinderItem *it : q->allItems())
            if (it->children.size() >= 50 && (!big || it->children.size() > big->children.size()))
                big = it;
        QList<BinderItem *> many = textItems(q).mid(0, 300);
        selectItems(w2, many);
        QElapsedTimer t;
        t.start();
        chooseByMenu(w2, QStringLiteral("viewCork"));
        settle(600);
        fact(QStringLiteral("S-V02 a corkboard of 300 cards (ms)"), int(t.elapsed()));
        checkCleanWindows(w2);
        w2->closeProject();
    }
    delete w2;
}

// ================================================================ S-V03 outliner
void caseSV03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *chapter = chapterOf(p, 4);
    selectItems(w, {chapter});
    chooseByMenu(w, QStringLiteral("viewOutline"));
    chooseByMenu(w, QStringLiteral("outlinerSynopsis"));
    settle(400);
    auto *out = qobject_cast<Outliner *>(visibleOf(w, "qb::Outliner"));
    check(out != nullptr, QStringLiteral("the outliner is shown"));
    if (!out) {
        finishProject(w);
        delete w;
        return;
    }
    // every column on
    if (auto *m = w->findChild<QMenu *>(QStringLiteral("outlinerOptionsMenu"))) {
        emit m->aboutToShow();
        for (QAction *a : m->actions())
            if (a->isCheckable() && !a->isChecked())
                a->trigger();
    }
    // edits in cells: through the outliner's model (title column), then label and status
    QAbstractItemModel *m = out->model();
    QStringList titles;
    for (int r = 0; r < m->rowCount(out->rootIndex()) && r < 3; ++r) {
        const QModelIndex ix = m->index(r, 0, out->rootIndex());
        const QString t = QStringLiteral("Outlined %1").arg(r + 1);
        m->setData(ix, t, Qt::EditRole);
        titles << t;
    }
    for (int c = 1; c < m->columnCount(out->rootIndex()); ++c) {
        out->sortByColumn(c, Qt::AscendingOrder);
        settle(40);
    }
    out->sortByColumn(0, Qt::AscendingOrder);
    out->expandAll();
    out->collapseAll();
    w = reopenWindow(w);
    p = w->project();
    int kept = 0;
    for (const QString &t : titles)
        if (p && itemByTitle(p, t))
            ++kept;
    check(kept == titles.size() && kept > 0, QStringLiteral("edits in the outliner kept after reopening (%1 of %2)").arg(kept).arg(titles.size()));
    finishProject(w);
    delete w;
    MainWindow *w2 = openWindow(fixtureCopy(QStringLiteral("fx2-scriv")));
    if (Project *q = w2->project()) {
        selectItems(w2, {q->draft()});
        QElapsedTimer t;
        t.start();
        chooseByMenu(w2, QStringLiteral("viewOutline"));
        settle(600);
        fact(QStringLiteral("S-V03 the outliner of FX-2's Draft (ms)"), int(t.elapsed()));
        w2->closeProject();
    }
    delete w2;
}

// ================================================================ S-V04 filtering
void caseSV04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    for (const QString &view : {QStringLiteral("viewCork"), QStringLiteral("viewOutline")}) {
        selectItems(w, {p->draft()});
        chooseByMenu(w, view);
        settle(300);
        EditorPane *pane = visiblePane(w);
        if (!pane)
            continue;
        pane->showFilterBar();
        settle(200);
        auto *field = w->findChild<QLineEdit *>(QStringLiteral("filterField"));
        auto *count = w->findChild<QLabel *>(QStringLiteral("filterCount"));
        for (const QString &t : {QStringLiteral("Scene"), QStringLiteral("zzzz-no-such-text"), QString()}) {
            if (field)
                field->setText(t);
            settle(200);
            fact(QStringLiteral("S-V04 %1 filter \"%2\"").arg(view, t), count ? count->text() : QString());
        }
        for (const QString &combo : {QStringLiteral("filterLabel"), QStringLiteral("filterStatus"), QStringLiteral("filterKeyword"), QStringLiteral("filterCompile")}) {
            if (auto *cb = w->findChild<QComboBox *>(combo))
                for (int i = 0; i < cb->count(); ++i) {
                    cb->setCurrentIndex(i);
                    settle(60);
                }
        }
        if (field)
            field->setText(QStringLiteral("Scene"));
        if (BinderItem *it = textItems(p).value(2)) {
            it->synopsis += QStringLiteral(" edited while filtered");
            p->notifyItemChanged(it);
        }
        if (auto *done = w->findChild<QAbstractButton *>(QStringLiteral("filterDone")))
            done->click();
        settle(200);
        checkCleanWindows(w);
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-V05 split editors
void caseSV05()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    for (const QString &a : {QStringLiteral("splitH"), QStringLiteral("noSplit"), QStringLiteral("splitV")}) {
        chooseByMenu(w, a);
        settle(200);
    }
    QList<EditorPane *> panes = visiblePanes(w);
    check(panes.size() == 2, QStringLiteral("two editors after Split Vertically"));
    BinderItem *it = textItems(p).value(12);
    if (panes.size() == 2) {
        panes.at(0)->showItems({it}, false, int(ViewMode::Single));
        panes.at(1)->showItems({it}, false, int(ViewMode::Single));
        settle(300);
        TextEditor *a = panes.at(0)->currentEditor(), *b = panes.at(1)->currentEditor();
        std::mt19937 r(ctx().seed);
        for (int i = 0; i < 300 && a && b; ++i) {
            TextEditor *e = i % 2 ? b : a;
            QTextCursor c(e->document());
            c.setPosition(int(r() % quint32(qMax(1, e->document()->characterCount() - 1))));
            e->setTextCursor(c);
            e->setFocus(Qt::OtherFocusReason);
            QTest::keyClicks(e, i % 2 ? QStringLiteral("b") : QStringLiteral("a"));
        }
        check(a && b && rawText(a->document()) == rawText(b->document()), QStringLiteral("300 edits alternating: both editors show the same text"));
        for (const QString &x : {QStringLiteral("lock"), QStringLiteral("switchEditor"), QStringLiteral("switchEditor"), QStringLiteral("lock"), QStringLiteral("selectionAffectsOther"),
                                 QStringLiteral("selectionAffectsOther")}) {
            chooseByMenu(w, x);
            settle(80);
        }
        selectItems(w, {textItems(p).value(14)});
        chooseByMenu(w, QStringLiteral("openOther"));
        settle(200);
        BinderItem *chapter = chapterOf(p, 5);
        panes.at(0)->showItems({chapter}, false, int(ViewMode::Combined));
        panes.at(1)->showItems({chapter}, false, int(ViewMode::Corkboard));
        settle(300);
        shot(w, QStringLiteral("combined-and-cork"));
        // close the split while the cursor is in the second editor
        panes.at(1)->showItems({it}, false, int(ViewMode::Single));
        if (TextEditor *b2 = panes.at(1)->currentEditor()) {
            b2->setFocus(Qt::OtherFocusReason);
            chooseByMenu(w, QStringLiteral("noSplit"));
            settle(300);
        }
    }
    TextEditor *e = showDoc(w, it);
    if (e) {
        chooseByMenu(w, QStringLiteral("splitV"));
        settle(200);
        e = visiblePanes(w).value(0) ? visiblePanes(w).value(0)->currentEditor() : e;
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
        for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
            typingGate(e, profile, 60, QStringLiteral("S-V05"));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-V06 copyholder
void caseSV06()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    // a picture and a PDF in Research
    QStringList warn;
    QList<BinderItem *> media;
    for (const QString &f : {QStringLiteral("tower.png")}) {
        const QList<BinderItem *> got = w->importDropped(p->research(), -1, {QUrl::fromLocalFile(ctx().sourceDir + QStringLiteral("/tests/import/") + f)}, false);
        media += got;
    }
    BinderItem *text = textItems(p).value(16);
    for (BinderItem *it : QList<BinderItem *>{text} + media) {
        selectItems(w, {it});
        check(chooseByMenu(w, QStringLiteral("openCopyholder")), QStringLiteral("Navigate > Open > in Copyholder: %1").arg(it->title));
        settle(300);
    }
    selectItems(w, {text});
    chooseByMenu(w, QStringLiteral("openCopyholder"));
    settle(300);
    // the copyholder's editor: a TextEditor that is not the pane's current one
    TextEditor *holder = nullptr;
    EditorPane *pane = visiblePane(w);
    for (TextEditor *x : pane ? pane->findChildren<TextEditor *>() : QList<TextEditor *>())
        if (x->isVisible() && x != pane->currentEditor())
            holder = x;
    if (holder) {
        QTextCursor c(holder->document());
        c.movePosition(QTextCursor::End);
        holder->setTextCursor(c);
        holder->setFocus(Qt::OtherFocusReason);
        typeInto(holder, QStringLiteral(" Typed in the copyholder."));
    } else {
        note(QStringLiteral("no copyholder editor found"));
    }
    chooseByMenu(w, QStringLiteral("selectionAffectsCopyholder"));
    selectItems(w, {textItems(p).value(17)});
    chooseByMenu(w, QStringLiteral("selectionAffectsCopyholder"));
    selectItems(w, {text});
    chooseByMenu(w, QStringLiteral("openCopyholder"));
    p->model()->moveToTrash({text});
    settle(300);
    chooseByMenu(w, QStringLiteral("closeCopyholder"));
    p->model()->moveItems({text}, p->draft(), -1);
    check(docText(p, text).contains(QLatin1String("Typed in the copyholder.")) || !holder, QStringLiteral("the edit in the copyholder is kept"));
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-V07 Quick Reference panels
void caseSV07()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderPanel *bp = binderOf(w);
    const QList<BinderItem *> docs = textItems(p);
    for (int i = 0; i < 6; ++i) {
        selectItems(w, {docs.value(20 + i)});
        if (i % 3 == 0 && bp)
            QTest::keyClick(bp->tree(), Qt::Key_Space);
        else if (i % 3 == 1)
            chooseByMenu(w, QStringLiteral("quickRef"));
        else   // 0.64.2: the menu path of the same document (it was always Chapter 1's scenes)
            menuPath(w, QStringLiteral("Navigate > Open Quick Reference > ") + [](BinderItem *it) {
                QStringList t;
                for (; it && it->parent; it = it->parent)
                    t.prepend(it->title);
                return t.join(QLatin1String(" > "));
            }(docs.value(20 + i)));
        settle(300);
    }
    QList<QWidget *> panels;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl != w && tl->isVisible() && tl->findChild<TextEditor *>() && !tl->inherits("QDialog"))
            panels << tl;
    fact(QStringLiteral("S-V07 panels open"), int(panels.size()));
    int edited = 0;
    // 0.64.2: the panel's text, not its notes below (findChild found the notes editor first)
    auto panelText = [](QWidget *pn) -> TextEditor * {
        if (auto *v = pn->findChild<TextColumnView *>())
            if (!v->editors().isEmpty())
                return v->editors().first();
        for (TextEditor *e : pn->findChildren<TextEditor *>())
            if (e->objectName() != QLatin1String("quickRefNotes"))
                return e;
        return nullptr;
    };
    for (QWidget *pn : panels)
        if (TextEditor *e = panelText(pn)) {
            QTextCursor c(e->document());
            c.movePosition(QTextCursor::End);
            e->setTextCursor(c);
            pn->activateWindow();
            e->setFocus(Qt::OtherFocusReason);
            typeInto(e, QStringLiteral(" Typed in a panel."));
            ++edited;
        }
    chooseByMenu(w, QStringLiteral("floatQuickRef"));
    chooseByMenu(w, QStringLiteral("floatQuickRef"));
    int kept = 0;
    for (BinderItem *it : docs.mid(20, 6))
        if (docText(p, it).contains(QLatin1String("Typed in a panel.")))
            ++kept;
    check(kept >= edited && edited > 0, QStringLiteral("edits in the panels are kept (%1 of %2)").arg(kept).arg(edited));
    chooseByMenu(w, QStringLiteral("closeQuickRefs"));
    settle(300);
    selectItems(w, {docs.value(30)});
    chooseByMenu(w, QStringLiteral("quickRef"));
    finishProject(w);
    int left = 0;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl != w && tl->isVisible() && tl->findChild<TextEditor *>() && !tl->inherits("QDialog"))
            ++left;
    check(left == 0, QStringLiteral("no panel left after closing the project"), QString::number(left));
    delete w;
}

// ================================================================ S-V08 navigation
void caseSV08()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QList<BinderItem *> docs = textItems(p);
    for (int i = 0; i < 25; ++i)
        showDoc(w, docs.value(i));
    for (int i = 0; i < 50; ++i)
        chooseByMenu(w, i < 25 ? QStringLiteral("back") : QStringLiteral("forward"));
    showDoc(w, docs.value(0));
    for (int i = 0; i < 45; ++i)
        chooseByMenu(w, QStringLiteral("nextDoc"));
    for (int i = 0; i < 45; ++i)
        chooseByMenu(w, QStringLiteral("prevDoc"));
    for (const QString &a : {QStringLiteral("enclosingGroup"), QStringLiteral("reveal"), QStringLiteral("focusBinder"), QStringLiteral("focusEditor")})
        chooseByMenu(w, a);
    menuPath(w, QStringLiteral("Navigate > Go To > Draft > Part 2 > Chapter 6 > Scene 19"));
    for (int i = 0; i < 7; ++i)
        chooseByMenu(w, QStringLiteral("inspect%1").arg(i));
    for (const QString &a : {QStringLiteral("binderAffects_current"), QStringLiteral("binderAffects_first"), QStringLiteral("binderAffects_second"), QStringLiteral("binderAffects_other"),
                             QStringLiteral("binderAffects_current")}) {
        chooseByMenu(w, a);
        selectItems(w, {docs.value(5)});
    }
    // double-click beside the text
    if (EditorPane *pane = visiblePane(w))
        for (QWidget *x : pane->findChildren<QWidget *>())
            if (x->inherits("qb::TextColumnView") && x->isVisible()) {
                QTest::mouseDClick(x, Qt::LeftButton, Qt::NoModifier, QPoint(8, x->height() / 2));
                settle(400);
                break;
            }
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-V09 panels and layouts
void caseSV09()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QStringList panels = {QStringLiteral("showBinder"), QStringLiteral("showInspector"), QStringLiteral("showCollections"), QStringLiteral("showToolbar"),
                                QStringLiteral("showFormatBar"), QStringLiteral("showIconBar"), QStringLiteral("showRuler")};
    std::mt19937 r(ctx().seed);
    for (int k = 0; k < 20; ++k) {
        chooseByMenu(w, panels.at(int(r() % quint32(panels.size()))));
        settle(60);
    }
    for (const QString &a : panels)
        if (QAction *x = action(w, a); x && x->isCheckable() && !x->isChecked() && a != QLatin1String("showCollections") && a != QLatin1String("showRuler"))
            x->trigger();
    // the built-in layouts
    for (const QString &id : {QStringLiteral("binder-editor"), QStringLiteral("editor-only"), QStringLiteral("outliner-editor"), QStringLiteral("corkboard-editor"),
                              QStringLiteral("two-editors"), QStringLiteral("planning"), QStringLiteral("default")}) {
        check(chooseByMenu(w, QStringLiteral("layout:") + id), QStringLiteral("Window > Layouts > %1").arg(id));
        settle(300);
        shot(w, QStringLiteral("layout-") + id);
    }
    // three own layouts
    for (int i = 1; i <= 3; ++i) {
        if (i == 2)
            chooseByMenu(w, QStringLiteral("splitV"));
        withModal<QDialog>(300, [i](QDialog *d) {
            if (auto *le = d->findChild<QLineEdit *>())
                le->setText(QStringLiteral("Own layout %1").arg(i));
            d->accept();
        });
        chooseByMenu(w, QStringLiteral("layoutSave"));
        settle(300);
    }
    for (int i = 1; i <= 3; ++i)
        menuPath(w, QStringLiteral("Window > Layouts > Own layout %1").arg(i));
    withModal<QDialog>(400, [](QDialog *d) {
        shot(d, QStringLiteral("manage-layouts"));
        if (auto *lw = d->findChild<QListWidget *>()) {
            for (int r2 = 0; r2 < lw->count(); ++r2)
                if (lw->item(r2)->text().contains(QLatin1String("Own layout 3"))) {
                    lw->setCurrentRow(r2);
                    lw->item(r2)->setText(QStringLiteral("Renamed layout"));
                }
        }
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("layoutManage"));
    settle(400);
    finishProject(w);
    delete w;
    // after a "restart" (a new window reads the settings again)
    auto *w2 = new MainWindow;
    w2->show();
    settle(300);
    int kept = 0;
    for (int i = 1; i <= 2; ++i)
        if (menuPath(w2, QStringLiteral("Window > Layouts > Own layout %1").arg(i)))
            ++kept;
    check(kept == 2, QStringLiteral("the own layouts are kept after a restart"), QString::number(kept));
    delete w2;
}

// ================================================================ S-V10 zoom everywhere
void caseSV10()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    TextEditor *e = showDoc(w, textItems(p).value(3));
    QList<QWidget *> targets;
    if (e)
        targets << e->viewport();
    if (Inspector *in = inspectorOf(w))
        targets << in;
    for (QWidget *t : targets)
        for (int i = 0; i < 6; ++i)
            wheel(t, i < 3);
    selectItems(w, {chapterOf(p, 1)});
    chooseByMenu(w, QStringLiteral("viewCork"));
    if (QWidget *c = visibleOf(w, "qb::Corkboard"))
        for (int i = 0; i < 4; ++i)
            wheel(qobject_cast<QAbstractScrollArea *>(c) ? qobject_cast<QAbstractScrollArea *>(c)->viewport() : c, i % 2);
    chooseByMenu(w, QStringLiteral("viewOutline"));
    if (QWidget *o = visibleOf(w, "qb::Outliner"))
        for (int i = 0; i < 4; ++i)
            wheel(qobject_cast<QAbstractScrollArea *>(o)->viewport(), i % 2);
    chooseByMenu(w, QStringLiteral("inspect5"));
    if (QWidget *m = visibleOf(w, "qb::MindMapView"))
        for (int i = 0; i < 4; ++i)
            wheel(qobject_cast<QAbstractScrollArea *>(m)->viewport(), i % 2);
    chooseByMenu(w, QStringLiteral("zoomInspectorPerTab"));
    e = showDoc(w, textItems(p).value(3));
    for (int i = 0; i < 15; ++i)
        chooseByMenu(w, QStringLiteral("zoomOut"));
    if (e)
        typeAtEnd(e, QStringLiteral(" At the smallest zoom."));
    for (int i = 0; i < 30; ++i)
        chooseByMenu(w, QStringLiteral("zoomIn"));
    if (e)
        typeAtEnd(e, QStringLiteral(" At the largest zoom."));
    shot(w, QStringLiteral("largest-zoom"));
    chooseByMenu(w, QStringLiteral("zoomReset"));
    chooseByMenu(w, QStringLiteral("zoomInspectorPerTab"));
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-V11 page view
void caseSV11()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *it = textItems(p).value(6);
    TextEditor *e = showDoc(w, it);
    chooseByMenu(w, QStringLiteral("pageView"));
    struct Page {
        QString name;
        double wmm, hmm, margin;
    };
    for (const Page &pg : {Page{QStringLiteral("A4"), 210, 297, 25}, Page{QStringLiteral("US Letter"), 215.9, 279.4, 25}, Page{QStringLiteral("A5"), 148, 210, 15},
                           Page{QStringLiteral("100 x 150 mm"), 100, 150, 0}, Page{QStringLiteral("margins 50 mm"), 210, 297, 50}}) {
        withModal<QDialog>(300, [pg](QDialog *d) {
            const QList<QDoubleSpinBox *> spins = d->findChildren<QDoubleSpinBox *>();
            if (spins.size() >= 2) {
                spins.at(0)->setValue(pg.wmm);
                spins.at(1)->setValue(pg.hmm);
                for (int i = 2; i < spins.size(); ++i)
                    spins.at(i)->setValue(pg.margin);
            }
            d->accept();
        });
        chooseByMenu(w, QStringLiteral("pageSetup"));
        settle(400);
        if (e)
            typeAtEnd(e, QLatin1Char('\n') + atParagraphs(3500, 300).join(QLatin1Char('\n')));
        shot(w, QStringLiteral("page-") + QString(pg.name).replace(QLatin1Char(' '), QLatin1Char('-')));
    }
    if (e)
        insertPicture(p, it, e->document(), 0, testPicture(3000, 4000));
    selectItems(w, {chapterOf(p, 2)});
    chooseByMenu(w, QStringLiteral("viewText"));
    settle(500);
    checkCleanWindows(w);
    chooseByMenu(w, QStringLiteral("pageView"));
    finishProject(w);
    delete w;
}

// ================================================================ S-I01 synopsis and notes
void caseSI01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QList<BinderItem *> docs = textItems(p);
    for (int i = 0; i < 20; ++i) {
        showDoc(w, docs.value(i));
        for (QPlainTextEdit *s : w->findChildren<QPlainTextEdit *>())
            if (s->isVisible()) {
                s->setFocus(Qt::OtherFocusReason);
                s->moveCursor(QTextCursor::End);
                QTest::keyClicks(s, QStringLiteral(" Synopsis %1.").arg(i));
                break;
            }
    }
    TextEditor *notes = nullptr;
    showDoc(w, docs.value(2));
    chooseByMenu(w, QStringLiteral("inspect0"));
    if (Inspector *in = inspectorOf(w))
        for (TextEditor *t : in->findChildren<TextEditor *>())
            if (t->isVisible())
                notes = t;
    check(notes != nullptr, QStringLiteral("the notes editor of the inspector"));
    if (notes) {
        notes->setFocus(Qt::OtherFocusReason);
        QTextCursor c(notes->document());
        c.movePosition(QTextCursor::End);
        notes->setTextCursor(c);
        typeInto(notes, QStringLiteral("A note in bold:"));
        insertPicture(p, docs.value(2), notes->document(), notes->textCursor().position(), testPicture(60, 40));
        QString big;
        for (quint32 s = 0; big.size() < 120000; ++s)
            big += atParagraphs(3600 + s, 2000).join(QLatin1Char('\n')) + QLatin1Char('\n');
        QTextCursor b(notes->document());
        b.movePosition(QTextCursor::End);
        b.insertText(big);
        // switch documents while typing in the notes
        for (int k = 0; k < 4; ++k) {
            typeInto(notes, QStringLiteral(" typing %1").arg(k));
            selectItems(w, {docs.value(3 + k)});
            settle(150);
            check(QApplication::focusWidget() != nullptr, QStringLiteral("(focus after switching %1)").arg(k));
        }
        showDoc(w, docs.value(2));
        chooseByMenu(w, QStringLiteral("inspect0"));
        for (TextEditor *t : inspectorOf(w)->findChildren<TextEditor *>())
            if (t->isVisible())
                notes = t;
        notes->setFocus(Qt::OtherFocusReason);
        QTextCursor endN(notes->document());
        endN.movePosition(QTextCursor::End);
        notes->setTextCursor(endN);
        for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
            typingGate(notes, profile, 60, QStringLiteral("S-I01"));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-I02 metadata and keywords
void caseSI02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QList<CustomFieldType> kinds = {CustomFieldType::Text, CustomFieldType::Checkbox, CustomFieldType::List, CustomFieldType::Date};
    for (int i = 0; i < kinds.size(); ++i) {
        CustomField f;
        f.id = QStringLiteral("csfield%1").arg(i);
        f.title = QStringLiteral("Test field %1").arg(i);
        f.type = kinds.at(i);
        if (f.type == CustomFieldType::List)
            f.options = {QStringLiteral("one"), QStringLiteral("two")};
        p->customFields << f;
    }
    p->markIndexDirty();
    BinderItem *it = textItems(p).value(9);
    selectItems(w, {it});
    menuPath(w, QStringLiteral("Documents > Label > Purple"));
    menuPath(w, QStringLiteral("Documents > Status > Revised Draft"));
    chooseByMenu(w, QStringLiteral("includeCompile"));
    it->custom.insert(QStringLiteral("csfield0"), QStringLiteral("text value"));
    it->custom.insert(QStringLiteral("csfield1"), QStringLiteral("Yes"));
    it->custom.insert(QStringLiteral("csfield2"), QStringLiteral("two"));
    it->custom.insert(QStringLiteral("csfield3"), QStringLiteral("2026-11-09"));
    Keyword k;
    k.id = QStringLiteral("cskw");
    k.name = QStringLiteral("Test keyword");
    k.color = QColor(0x33, 0x99, 0x33);
    p->keywords << k;
    it->keywords << k.id;
    textItems(p).value(10)->keywords << k.id;
    p->notifyItemChanged(it);
    withModal<QDialog>(400, [](QDialog *d) {
        shot(d, QStringLiteral("project-keywords"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("keywords"));
    settle(400);
    const QString title = it->title;
    w = reopenWindow(w);
    p = w->project();
    BinderItem *again = p ? itemByTitle(p, title) : nullptr;
    check(again && again->custom.value(QStringLiteral("csfield2")) == QLatin1String("two") && again->keywords.contains(QStringLiteral("cskw")) && !again->includeInCompile,
          QStringLiteral("label, status, compile, custom metadata and keywords kept after reopening"));
    finishProject(w);
    delete w;
}

// ================================================================ S-I03 bookmarks and the editable preview
void caseSI03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QList<BinderItem *> docs = textItems(p);
    BinderItem *host = docs.value(4);
    BinderItem *target = docs.value(5);
    host->bookmarks << Bookmark{target->title, QStringLiteral("qb://") + target->id} << Bookmark{QStringLiteral("Web"), QStringLiteral("https://example.org")};
    std::swap(host->bookmarks[0], host->bookmarks[1]);
    host->bookmarks.removeAt(0);
    p->notifyItemChanged(host);
    selectItems(w, {target});
    chooseByMenu(w, QStringLiteral("bookmark"));
    showDoc(w, host);
    chooseByMenu(w, QStringLiteral("inspect1"));
    settle(300);
    if (auto *list = w->findChild<QListWidget *>(QStringLiteral("inspectorBookmarks")))
        if (list->count())
            list->setCurrentRow(0);
    settle(400);
    TextEditor *preview = w->findChild<TextEditor *>(QStringLiteral("bookmarkPreview"));
    if (!preview)
        for (TextEditor *t : inspectorOf(w)->findChildren<TextEditor *>())
            if (t->isVisible())
                preview = t;
    check(preview != nullptr, QStringLiteral("the editable bookmark preview"));
    if (preview) {
        std::mt19937 r(ctx().seed);
        for (int i = 0; i < 400; ++i) {
            QTextCursor c(preview->document());
            c.setPosition(int(r() % quint32(qMax(1, preview->document()->characterCount() - 1))));
            preview->setTextCursor(c);
            preview->setFocus(Qt::OtherFocusReason);
            if (i % 40 == 39)
                chooseByMenu(w, QStringLiteral("annotation"));
            else
                QTest::keyClicks(preview, i % 5 ? QStringLiteral("p") : QStringLiteral(" "));
        }
        check(rawText(preview->document()) == docText(p, target), QStringLiteral("400 edits in the preview: the document equals the preview"));
        QTextCursor c(preview->document());
        c.movePosition(QTextCursor::End);
        preview->setTextCursor(c);
        for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
            typingGate(preview, profile, 60, QStringLiteral("S-I03"));
    }
    p->model()->moveToTrash({target});
    settle(400);
    fact(QStringLiteral("S-I03 the preview after the document went to the Trash"), preview && preview->isVisible() ? preview->toPlainText().left(60) : QStringLiteral("hidden"));
    finishProject(w);
    delete w;
}

// ================================================================ S-I04 snapshots
void caseSI04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QList<BinderItem *> docs = textItems(p);
    for (int i = 0; i < 5; ++i) {
        TextEditor *e = showDoc(w, docs.value(i));
        if (e)
            QTest::keyClick(e, Qt::Key_5, Qt::ControlModifier);
        settle(150);
    }
    BinderItem *it = docs.value(6);
    TextEditor *e = showDoc(w, it);
    withModal<QInputDialog>(300, [](QInputDialog *d) {
        d->setTextValue(QStringLiteral("Titled snapshot"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("snapshotTitle"));
    settle(300);
    const QString snapText = docText(p, it);
    if (e) {
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" Changed after the snapshot."));
    }
    chooseByMenu(w, QStringLiteral("splitV"));
    if (visiblePanes(w).size() > 1)
        visiblePanes(w).at(1)->showItems({it}, false, int(ViewMode::Single));
    const QList<Snapshot> list = snapshots::list(p, it->id);
    check(!list.isEmpty(), QStringLiteral("Take Snapshot with Title"));
    if (!list.isEmpty()) {
        snapshots::rollBack(p, list.first());
        settle(300);
        int equal = 0;
        for (EditorPane *pane : visiblePanes(w))
            if (TextEditor *x = pane->currentEditor(); x && rawText(x->document()) == snapText)
                ++equal;
        check(equal == visiblePanes(w).size(), QStringLiteral("the rolled-back text equals the snapshot in both editors (%1)").arg(equal));
        snapshots::remove(p, list.last());
    }
    chooseByMenu(w, QStringLiteral("showSnapshots"));
    settle(300);
    shot(w, QStringLiteral("snapshots"));
    finishProject(w);
    delete w;
}

// ================================================================ S-I05 comments and footnotes tab
void caseSI05()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *withNotes = nullptr;
    for (BinderItem *it : textItems(p))
        if (!withNotes && docText(p, it).contains(QChar(0xFFFC)) == false && p->docs()->text(it->id)->toPlainText().size() > 100)
            withNotes = it;
    TextEditor *e = showDoc(w, withNotes);
    if (!e)
        return;
    for (int i = 0; i < 5; ++i) {
        QTextCursor c(e->document());
        c.setPosition(20 + i * 60);
        c.setPosition(26 + i * 60, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        chooseByMenu(w, i % 2 ? QStringLiteral("footnote") : QStringLiteral("comment"));
        e->setFocus(Qt::OtherFocusReason);
    }
    chooseByMenu(w, QStringLiteral("inspect4"));
    settle(300);
    Inspector *in = inspectorOf(w);
    QListWidget *list = nullptr;
    for (QListWidget *l : in ? in->findChildren<QListWidget *>() : QList<QListWidget *>())
        if (l->isVisible())
            list = l;
    if (list) {
        for (int r = 0; r < list->count(); ++r) {
            list->setCurrentRow(r);
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(r)).center());
            settle(80);
        }
    }
    // delete the anchor text while the tab shows the comment
    QTextCursor c(e->document());
    c.setPosition(18);
    c.setPosition(30, QTextCursor::KeepAnchor);
    e->setTextCursor(c);
    QTest::keyClick(e, Qt::Key_Delete);
    settle(300);
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-I06 mind maps
void caseSI06()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *it = textItems(p).value(1);
    showDoc(w, it);
    chooseByMenu(w, QStringLiteral("inspect5"));
    settle(400);
    auto *view = qobject_cast<MindMapView *>(visibleOf(w, "qb::MindMapView"));
    check(view != nullptr, QStringLiteral("the mind map tab"));
    if (!view) {
        finishProject(w);
        delete w;
        return;
    }
    MindMapDoc *doc = view->boardScene() ? view->boardScene()->doc() : nullptr;
    if (doc) {
        doc->change(QStringLiteral("system test nodes"), [doc] {
            int prev = 0;
            for (int i = 0; i < 60; ++i) {
                mindmap::Note n;
                n.id = doc->board.nextId();
                n.pos = QPointF(40 + (i % 10) * 180, 40 + (i / 10) * 90);
                n.text = QStringLiteral("Node %1").arg(i);
                n.look.fill = QColor::fromHsv(i * 6, 80, 240);
                doc->board.notes << n;
                if (prev)
                    doc->board.connect(prev, n.id, i % 2 == 0);
                prev = n.id;
            }
        });
        // keyboard and mouse: a double-click on the board, typing
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(30, view->viewport()->height() - 30));
        settle(200);
        QTest::keyClicks(QApplication::focusWidget() ? QApplication::focusWidget() : view, QStringLiteral("Typed node"));
        QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : view, Qt::Key_Escape);
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        MindMapWindow *big = MindMapWindow::open(doc, w);
        settle(400);
        shot(big, QStringLiteral("mind-map-window"));
    }
    chooseByMenu(w, QStringLiteral("mindMapHelp"));
    settle(400);
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl != w && tl->isVisible() && tl->windowTitle().contains(QLatin1String("Help")))
            tl->close();
    const QString path = projectPathOf(p);
    for (int round = 0; round < 10; ++round) {
        step(QStringLiteral("close with the mind map open in both places, round %1").arg(round + 1));
        w->closeProject();
        settle(300);
        checkCleanWindows(w);
        if (!w->openProject(path))
            break;
        settle(500);
        if (BinderItem *x = textItems(w->project()).value(1))
            showDoc(w, x);
        chooseByMenu(w, QStringLiteral("inspect5"));
        settle(300);
        if (auto *v2 = qobject_cast<MindMapView *>(visibleOf(w, "qb::MindMapView")); v2 && v2->boardScene())
            MindMapWindow::open(v2->boardScene()->doc(), w);
        settle(300);
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-I07 objects
void caseSI07()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    chooseByMenu(w, QStringLiteral("inspect6"));
    chooseByMenu(w, QStringLiteral("objectsWindow"));
    settle(400);
    withModal<QDialog>(400, [](QDialog *d) {
        shot(d, QStringLiteral("object-templates"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("objectTemplates"));
    settle(400);
    objects::Store *st = p->objects();
    QStringList ids;
    const QString cat = st->schema().categories.value(0).id;
    for (int i = 0; i < 12; ++i) {
        const QString id = st->create(cat, QString(), QStringLiteral("Person %1").arg(i));
        ids << id;
        if (i % 4 == 0)
            st->importPicture(id, st->category(cat) && !st->category(cat)->fields.isEmpty() ? st->category(cat)->fields.first().id : QString(),
                              ctx().sourceDir + QStringLiteral("/tests/import/tower.png"));
    }
    BinderItem *it = textItems(p).value(3);
    TextEditor *e = showDoc(w, it);
    if (e) {
        QTextCursor c(e->document());
        c.setPosition(10);
        c.setPosition(16, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        withModal<QDialog>(300, [](QDialog *d) { d->accept(); });
        chooseByMenu(w, QStringLiteral("objectLink"));
        settle(300);
        QTextCursor l(e->document());
        l.setPosition(12);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, Qt::ControlModifier, e->cursorRect(l).center());
        settle(300);
    }
    objects::Object o = *st->object(ids.first());
    o.name = QStringLiteral("Renamed person");
    st->update(o);
    st->remove(ids.value(1));
    st->addFolder(cat, QString(), QStringLiteral("Object folder"));
    QString err;
    st->save(&err);
    if (e) {
        QTextCursor l(e->document());
        l.setPosition(12);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, Qt::ControlModifier, e->cursorRect(l).center());
        settle(300);
        check(true, QStringLiteral("a click on a link to a deleted object does not crash"));
    }
    const int count = st->count();
    w = reopenWindow(w);
    check(w->project() && w->project()->objects()->count() == count, QStringLiteral("objects kept after reopening (%1)").arg(count));
    finishProject(w);
    delete w;
}

// ================================================================ S-I08 the look of the parts
void caseSI08()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    TextEditor *e = showDoc(w, textItems(p).value(2));
    QStringList seen;
    if (e) {
        QStringList entries;
        rightClickChoose(e, 5, QStringLiteral("Appearance"), &entries);
        seen << QStringLiteral("editor: ") + entries.filter(QStringLiteral("Appearance")).join(QLatin1Char(','));
        closeDialogsLater(100);
    }
    const QString lookFile = p->settingsDir() + QStringLiteral("/look.json");
    fact(QStringLiteral("S-I08 the right-click Appearance menus found"), seen.join(QLatin1String("; ")));
    const QByteArray before = readText(lookFile).toUtf8();
    w = reopenWindow(w);
    check(w->project() && readText(w->project()->settingsDir() + QStringLiteral("/look.json")).toUtf8() == before, QStringLiteral("the look is kept with the project"));
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-I09 notes on four levels
void caseSI09()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        if (!p)
            continue;
        BinderItem *scene = textItems(p).value(7);
        BinderItem *chapter = scene->parent;
        BinderItem *part = chapter ? chapter->parent : nullptr;
        TextEditor *e = showDoc(w, scene);
        if (e)
            for (const QString &k : {QStringLiteral("comment"), QStringLiteral("annotation"), QStringLiteral("footnote"), QStringLiteral("inlineFootnote")}) {
                QTextCursor c(e->document());
                c.setPosition(5);
                c.setPosition(12, QTextCursor::KeepAnchor);
                e->setTextCursor(c);
                chooseByMenu(w, k);
                e->setFocus(Qt::OtherFocusReason);
            }
        for (BinderItem *x : {scene, chapter, part}) {
            if (!x)
                continue;
            x->synopsis = QStringLiteral("Synopsis of %1").arg(x->title);
            QTextCursor n(p->docs()->notes(x->id));
            n.movePosition(QTextCursor::End);
            n.insertText(QStringLiteral(" Notes of %1.").arg(x->title));
            p->notifyItemChanged(x);
        }
        if (chapter)
            fillText(p, chapter, {QStringLiteral("The chapter folder's own text.")});
        const QJsonObject want = modelOf(p, false);
        const int at = chapter ? int(chapter->children.indexOf(scene)) : 0;   // back to its place (0.64.2: the model expects it there)
        // moves and conversions, then the format switch
        p->model()->moveItems({scene}, p->draft()->children.value(1), 0);
        selectItems(w, {scene});
        chooseByMenu(w, QStringLiteral("toFolder"));
        selectItems(w, {scene});
        chooseByMenu(w, QStringLiteral("toText"));
        p->model()->moveToTrash({scene});
        p->model()->moveItems({scene}, chapter, at);
        w = reopenWindow(w);
        check(w->project() && compareModels(want, modelOf(w->project(), false), fx + QStringLiteral(" notes on four levels")) <= 2,
              QStringLiteral("%1: each note stays at its level and on its item").arg(fx));
        finishProject(w);
        delete w;
    }
}

// ================================================================ S-I10 links and mind maps: do links hold?
void caseSI10()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        if (!p)
            continue;
        const QList<BinderItem *> docs = textItems(p);
        BinderItem *from = docs.value(2), *to = docs.value(9), *gone = docs.value(10);
        TextEditor *e = showDoc(w, from);
        if (!e)
            continue;
        QTextCursor c(e->document());
        c.setPosition(4);
        c.setPosition(10, QTextCursor::KeepAnchor);
        QTextCharFormat link;
        link.setAnchor(true);
        link.setAnchorHref(QStringLiteral("qb://") + to->id);
        c.mergeCharFormat(link);
        QTextCursor c2(e->document());
        c2.setPosition(14);
        c2.setPosition(20, QTextCursor::KeepAnchor);
        QTextCharFormat link2 = link;
        link2.setAnchorHref(QStringLiteral("qb://") + gone->id);
        c2.mergeCharFormat(link2);
        p->projectBookmarks << Bookmark{to->title, QStringLiteral("qb://") + to->id};
        to->title = QStringLiteral("Renamed target");
        p->notifyItemChanged(to);
        p->model()->moveItems({to}, p->research(), 0);
        selectItems(w, {to});
        chooseByMenu(w, QStringLiteral("toFolder"));
        p->model()->moveToTrash({gone});
        answerBoxLater(300, QMessageBox::Yes);
        chooseByMenu(w, QStringLiteral("emptyTrash"));
        settle(400);
        // the editor may have been replaced by Convert and Empty Trash (shakedown 2026-11-09): shown again
        e = showDoc(w, from);
        if (!e)
            return;
        // click both links
        for (int pos : {6, 16}) {
            QTextCursor l(e->document());
            l.setPosition(pos);
            BoxText msg;
            answerBoxLater(400, QMessageBox::Ok, &msg);
            QTest::mouseClick(e->viewport(), Qt::LeftButton, Qt::ControlModifier, e->cursorRect(l).center());
            settle(600);
            fact(QStringLiteral("S-I10 %1 a click on the link at %2").arg(fx).arg(pos),
                 msg.isEmpty() ? (visiblePane(w) && visiblePane(w)->currentItem() ? visiblePane(w)->currentItem()->title : QStringLiteral("nothing")) : msg.left(120));
            closeDialogsLater(50);
            settle(100);
            e = showDoc(w, from);
            if (!e)
                break;
        }
        const QString toId = to->id;
        w = reopenWindow(w);
        p = w->project();
        check(p && p->item(toId) && p->item(toId)->title == QLatin1String("Renamed target"), QStringLiteral("%1: the link's target is there after rename, move, convert and reopening").arg(fx));
        finishProject(w);
        delete w;
    }
}

} // namespace cs
