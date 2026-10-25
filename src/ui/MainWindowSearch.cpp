// Search tools of the main window: project replace, find by formatting,
// highlighting the matches of a project search, the quick search list.
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Search.h"
#include "text/FormatFind.h"
#include "ui/BinderPanel.h"
#include "ui/MainWindow.h"
#include "ui/SearchTools.h"
#include "ui/TargetLineEdit.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"

#include <QApplication>
#include <QCompleter>
#include <QSet>
#include <QStandardItemModel>
#include <QTextCursor>

namespace qb {

void MainWindow::applySearchHighlights()
{
    QRegularExpression re(QStringLiteral("(?")); // invalid = no highlights
    QString findText;
    if (m_project && m_binder->showsSearchResults()) {
        const SearchQuery q = m_binder->searchQuery();
        if (!q.invert && !q.text.trimmed().isEmpty()) {
            re = search::pattern(q);
            if (!q.regex)
                findText = q.exactPhrase ? q.text.trimmed() : q.text.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
        }
    }
    for (EditorPane *pane : m_panes)
        pane->setSearchPattern(re, findText);
    updateActions(); // the selection the commands act on may be another list now
}

void MainWindow::projectReplace()
{
    ProjectReplaceDialog dlg(m_project.get(), this);
    dlg.setSelection(m_binder->selectedItems());
    connect(&dlg, &ProjectReplaceDialog::replaced, this, [this](const ReplaceResult &r) {
        if (r.total() == 0)
            return;
        for (EditorPane *pane : m_panes) {
            pane->refreshHeader();
            pane->updateFooter();
        }
        refreshInspector();
        updateWindowTitle();
        m_project->save(); // texts in files are already rewritten; bring the open ones and the titles along
        if (m_binder->showsSearchResults())
            m_binder->runSearch(m_binder->searchQuery());
    });
    dlg.exec();
}

void MainWindow::showFormatFind()
{
    if (!m_formatFind) {
        m_formatFind = new FormatFindWindow(m_project.get(), this);
        m_formatFind->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_formatFind, &FormatFindWindow::findRequested, this, [this](bool backward) { findFormatting(backward); });
    }
    m_formatFind->show();
    m_formatFind->raise();
    m_formatFind->activateWindow();
}

bool MainWindow::findFormatting(bool backward)
{
    if (!m_project)
        return false;
    if (!m_formatFind) {
        showFormatFind(); // nothing asked for yet
        return false;
    }
    const fmtfind::Query q = m_formatFind->query();
    const bool everywhere = m_formatFind->allDocuments();
    const Formatting f = m_project->formatting;
    QPointer<FormatFindWindow> win = m_formatFind;
    auto status = [win](const QString &text) {
        if (win)
            win->setStatus(text);
    };
    // The text the last hit was in: the walk goes on from there, also while
    // the keyboard is still in the Find by Formatting window (unless the
    // writer has clicked into another text since).
    static QPointer<TextEditor> lastHit, activeThen;
    auto select = [this](TextEditor *e, const fmtfind::Range &r) {
        QTextCursor c(e->document());
        c.setPosition(r.start);
        c.setPosition(r.end, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        m_active->textView()->ensureCursorVisible(e);
        if (!m_formatFind || !m_formatFind->isActiveWindow())
            e->setFocus();
        lastHit = e;
        activeThen = m_active->currentEditor();
    };
    auto endOf = [](TextEditor *e) { return e->document()->characterCount(); };

    // 1. the texts the active editor shows, from the cursor on
    const QList<TextEditor *> eds = m_active->textView()->editors();
    TextEditor *cur = m_active->currentEditor();
    if (lastHit && eds.contains(lastHit) && cur == activeThen)
        cur = lastHit;
    if (!eds.isEmpty() && !eds.contains(cur))
        cur = backward ? eds.last() : eds.first();
    if (cur && eds.contains(cur)) {
        const int idx = int(eds.indexOf(cur));
        for (int i = idx; i >= 0 && i < eds.size(); i += backward ? -1 : 1) {
            TextEditor *e = eds[i];
            const int from = i == idx ? (backward ? e->textCursor().selectionStart() : e->textCursor().selectionEnd())
                                      : (backward ? endOf(e) : 0);
            const fmtfind::Range r = fmtfind::next(e->document(), q, f, from, backward);
            if (r.isValid()) {
                select(e, r);
                status(QString());
                return true;
            }
        }
    }
    auto wrapShown = [&]() {
        // back to the start (the end) of what is shown
        for (int n = 0; n < eds.size(); ++n) {
            TextEditor *e = eds[backward ? eds.size() - 1 - n : n];
            const fmtfind::Range r = fmtfind::next(e->document(), q, f, backward ? endOf(e) : 0, backward);
            if (r.isValid()) {
                select(e, r);
                return true;
            }
        }
        return false;
    };
    if (!everywhere) {
        if (wrapShown()) {
            status(backward ? tr("Continued from the end.") : tr("Continued from the beginning."));
            return true;
        }
        status(tr("Not found in this editor."));
        QApplication::beep();
        return false;
    }

    // 2. the other documents of the project, in binder order
    QList<BinderItem *> order;
    for (BinderItem *it : m_project->allItems())
        if (it->hasText() && !m_project->isInTrash(it) && it->type != ItemType::Trash)
            order << it;
    QSet<BinderItem *> shown;
    for (TextEditor *e : eds)
        if (e->item())
            shown.insert(e->item());
    int start = backward ? int(order.size()) : -1;
    for (int i = 0; i < order.size(); ++i)
        if (shown.contains(order[i])) {
            start = i;
            if (backward)
                break; // before the first one shown
        }
    const int count = int(order.size());
    for (int n = 1; n <= count; ++n) {
        const int i = (((start + (backward ? -n : n)) % count) + count) % count;
        BinderItem *it = order[i];
        if (shown.contains(it))
            continue;
        const int hits = m_project->docs()->editStored(
            it->id, false, [&](QTextDocument *d) { return int(fmtfind::all(d, q, f).size()); });
        if (hits <= 0)
            continue;
        loadInPane(m_active, {it});
        m_updatingUi = true;
        m_binder->select({it}, false);
        m_updatingUi = false;
        const QList<TextEditor *> now = m_active->textView()->editors();
        if (now.isEmpty())
            continue;
        TextEditor *e = now.first();
        const fmtfind::Range r = fmtfind::next(e->document(), q, f, backward ? endOf(e) : 0, backward);
        if (!r.isValid())
            continue;
        select(e, r);
        status(tr("In \"%1\".").arg(it->title.isEmpty() ? tr("Untitled") : it->title));
        return true;
    }
    // 3. nothing elsewhere: around again in what is shown
    if (wrapShown()) {
        status(tr("No other document has it; continued from the %1.").arg(backward ? tr("end") : tr("beginning")));
        return true;
    }
    status(tr("Not found."));
    QApplication::beep();
    return false;
}

void MainWindow::fillQuickSearch()
{
    m_completerModel->clear();
    if (!m_project || m_quickSearch->text().trimmed().size() < 2)
        return;
    const QList<QuickHit> hits = quickSearchGrouped(m_project.get(), m_quickSearch->text(), 10);
    int lastKind = -1;
    for (const QuickHit &h : hits) {
        if (int(h.kind) != lastKind) {
            lastKind = int(h.kind);
            auto *head = new QStandardItem(h.kind == QuickHit::Title ? tr("Titles")
                                           : h.kind == QuickHit::Synopsis ? tr("Synopses") : tr("Text"));
            head->setFlags(Qt::NoItemFlags);
            QFont hf = m_quickSearch->font();
            hf.setBold(true);
            hf.setPointSizeF(hf.pointSizeF() * 0.9);
            head->setFont(hf);
            head->setData(QStringLiteral("header"), Qt::UserRole + 2);
            m_completerModel->appendRow(head);
        }
        BinderItem *it = h.item;
        auto *row = new QStandardItem(icons::forItem(m_project.get(), it), it->title.isEmpty() ? tr("Untitled") : it->title);
        row->setData(it->id, Qt::UserRole + 1);
        QString path;
        for (BinderItem *p = it->parent; p && p->parent; p = p->parent)
            path.prepend(p->title + QStringLiteral(" › "));
        row->setToolTip(h.excerpt.isEmpty() ? path + it->title : path + it->title + QLatin1Char('\n') + h.excerpt);
        m_completerModel->appendRow(row);
    }
    m_completer->complete();
}

} // namespace qb
