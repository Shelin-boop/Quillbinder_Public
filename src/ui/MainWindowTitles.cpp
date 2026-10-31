// Documents without a name take the first line of their text as title (0.40.3).
//
// A text document whose title is empty or "Untitled" gets the first line of its text that has
// words (without inline annotations, inline footnotes and pictures; at most 60 characters, cut
// at a word). The title then follows the text (BinderItem::autoTitle, kept in the project file)
// until it is changed by hand (BinderModel::setData); a text without words is "Untitled" again.
// The title is a real title: it is saved, shown everywhere and written to Scrivener projects.

#include "ui/MainWindow.h"

#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "text/TextUtil.h"

#include <QTimer>

#include <utility>

namespace qb {

void MainWindow::queueAutoTitle(const QString &id)
{
    if (!m_project)
        return;
    const BinderItem *it = m_project->item(id);
    if (!it || !it->hasText() || it->isRootFolder() || (!it->autoTitle && !text::isUntitled(it->title)))
        return;
    m_autoTitlePending.insert(id);
    if (!m_autoTitleTimer) {
        m_autoTitleTimer = new QTimer(this);
        m_autoTitleTimer->setObjectName(QStringLiteral("autoTitleTimer"));
        m_autoTitleTimer->setSingleShot(true);
        m_autoTitleTimer->setInterval(400); // not with every key
        connect(m_autoTitleTimer, &QTimer::timeout, this, &MainWindow::updateAutoTitles);
    }
    m_autoTitleTimer->start();
}

void MainWindow::queueUntitledDocuments()
{
    if (!m_project)
        return;
    m_project->root()->forEach([this](BinderItem *it) {
        // the counts kept in the project file tell which ones have text, without loading them all
        if (it->hasText() && !it->isRootFolder() && (it->wordCount > 0 || it->charCount > 0)
            && (it->autoTitle || text::isUntitled(it->title)))
            queueAutoTitle(it->id);
    });
}

void MainWindow::updateAutoTitles()
{
    if (!m_project)
        return;
    const QSet<QString> ids = std::exchange(m_autoTitlePending, {});
    for (const QString &id : ids) {
        BinderItem *it = m_project->item(id);
        if (!it || !it->hasText() || (!it->autoTitle && !text::isUntitled(it->title)))
            continue; // named by hand in the meantime
        const QTextDocument *doc = m_project->docs()->text(id);
        if (!doc)
            continue;
        const QString line = text::firstLineTitle(doc);
        const QString title = line.isEmpty() ? tr("Untitled") : line;
        const bool follows = !line.isEmpty();
        if (title == it->title && follows == it->autoTitle)
            continue;
        it->title = title;
        it->autoTitle = follows;
        m_project->notifyItemChanged(it, false);
    }
}

} // namespace qb
