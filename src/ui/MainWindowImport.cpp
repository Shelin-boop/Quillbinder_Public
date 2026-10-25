// File > Import > Web Page and things dropped from other programs (files, web links).
#include "ui/MainWindow.h"

#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/Project.h"
#include "import/FileImporter.h"
#include "ui/BinderPanel.h"

#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

namespace qb {

static bool isWebAddress(const QUrl &u)
{
    return u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https"));
}

void MainWindow::explainWebImportAddon()
{
    QMessageBox box(QMessageBox::Information, tr("Import Web Page"),
                    tr("Web pages are imported by the add-on “Web Page Import”, which is not installed or is switched off."),
                    QMessageBox::Close, this);
    box.setInformativeText(tr("Install it with File > Install Update or Add-on; Help > Add-on Manager switches add-ons on and off."));
    QPushButton *manager = box.addButton(tr("Add-on Manager…"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == manager)
        showAddonManager();
}

BinderItem *MainWindow::importWebPage(const QString &address, bool interactive, BinderItem *parent, int row, bool select)
{
    if (!m_project)
        return nullptr;
    importers::ImporterInterface *imp = compile::PluginManager::instance().webImporter();
    if (!imp) {
        explainWebImportAddon();
        return nullptr;
    }
    if (!parent) {
        // the selected folder, beneath the selected document, else Research
        if (actionItems().isEmpty()) {
            parent = m_project->research();
            row = -1;
        } else {
            parent = insertionPoint(&row);
        }
    }
    if (!parent || m_project->isInTrash(parent)) {
        parent = m_project->research();
        row = -1;
    }
    QTemporaryDir work;
    if (!work.isValid()) {
        QMessageBox::warning(this, tr("Import Web Page"), tr("Cannot create a temporary folder."));
        return nullptr;
    }
    importers::WebPage page;
    QString err;
    if (!imp->importWebPage(this, address, QString(), interactive, parent->title, work.path(), &page, &err)) {
        if (!err.isEmpty())
            QMessageBox::warning(this, tr("Import Web Page"), err);
        return nullptr;
    }
    if (!m_project)
        return nullptr; // closed meanwhile
    QStringList warnings = page.content.warnings;
    BinderItem *it = importer::addWebPage(m_project.get(), page, parent, row, &warnings);
    if (it && select)
        m_binder->select({it}, true);
    if (!warnings.isEmpty())
        QMessageBox::information(this, tr("Import Web Page"), warnings.join(QLatin1Char('\n')));
    return it;
}

QList<BinderItem *> MainWindow::importDropped(BinderItem *parent, int row, const QList<QUrl> &urls, bool select)
{
    QList<BinderItem *> out;
    if (!m_project)
        return out;
    QStringList paths;
    QList<QUrl> web;
    for (const QUrl &u : urls) {
        if (u.isLocalFile())
            paths << u.toLocalFile();
        else if (isWebAddress(u))
            web << u;
    }
    QStringList warnings;
    if (!paths.isEmpty()) {
        out = importer::importFiles(m_project.get(), paths, parent, row, &warnings);
        if (row >= 0)
            for (BinderItem *it : out)
                if (it->parent == parent)
                    row = qMax(row, it->row() + 1);
    }
    if (!warnings.isEmpty())
        QMessageBox::information(this, tr("Import"), warnings.join(QLatin1Char('\n')));
    if (!out.isEmpty() && select)
        m_binder->select(out, true);
    if (!web.isEmpty()) {
        // after the drop has finished: fetching runs an event loop, and the browser waits for the end of the drop
        const QString id = parent ? parent->id : QString();
        QTimer::singleShot(0, this, [this, id, row, web, select] {
            if (!m_project)
                return;
            if (!compile::PluginManager::instance().webImporter()) {
                explainWebImportAddon();
                return;
            }
            BinderItem *target = m_project->item(id);
            if (!target)
                target = m_project->research();
            int at = row;
            QList<BinderItem *> pages;
            for (const QUrl &u : web.mid(0, 20)) {
                BinderItem *it = importWebPage(u.toString(), false, target, at, false);
                if (!it)
                    break; // cancelled or failed (already told)
                pages << it;
                if (at >= 0)
                    at = it->row() + 1;
            }
            if (!pages.isEmpty() && select)
                m_binder->select(pages, true);
        });
    }
    return out;
}

} // namespace qb
