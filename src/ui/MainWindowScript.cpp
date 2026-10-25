// Format > Scriptwriting: script mode, the elements, the script format.
#include "ui/MainWindow.h"

#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "text/Script.h"
#include "ui/Commands.h"
#include "ui/EditorPane.h"
#include "ui/QuickRef.h"
#include "ui/ScriptDialogs.h"
#include "ui/TextEditor.h"
#include "ui/Windows.h"

#include <QActionGroup>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>

namespace qb {

void MainWindow::createScriptActions()
{
    auto act = [this](const QString &key, const QString &text, const QKeySequence &ks = QKeySequence()) {
        auto *ac = new QAction(text, this);
        ac->setObjectName(key);
        QString plain = text;
        plain.remove(QLatin1Char('&'));
        if (plain.endsWith(QChar(0x2026)))
            plain.chop(1);
        ac->setIconText(plain);
        if (!ks.isEmpty())
            ac->setShortcut(ks);
        a.insert(key, ac);
        addAction(ac);
        return ac;
    };
    QAction *mode = act(QStringLiteral("scriptMode"), tr("Script &Mode"), QKeySequence(Qt::CTRL | Qt::Key_8));
    mode->setCheckable(true);
    connect(mode, &QAction::triggered, this, [this](bool on) { setScriptMode(on); });
    connect(act(QStringLiteral("scriptElements"), tr("Show the &List of Elements"), QKeySequence(Qt::CTRL | Qt::Key_Backslash)),
            &QAction::triggered, this, [this] {
                TextEditor *e = editor();
                if (e && e->scriptActive())
                    e->showScriptElementsMenu();
            });
    connect(act(QStringLiteral("scriptSettings"), tr("Script &Settings…")), &QAction::triggered, this,
            [this] { showScriptSettings(); });
}

void MainWindow::createScriptMenu(QMenu *formatMenu)
{
    m_scriptMenu = formatMenu->addMenu(tr("S&criptwriting"));
    m_scriptMenu->setObjectName(QStringLiteral("scriptMenu"));
    m_scriptMenu->addAction(a[QStringLiteral("scriptMode")]);
    m_scriptMenu->addSeparator();
    m_scriptElementMenu = m_scriptMenu->addMenu(tr("Change &Element To"));
    m_scriptElementMenu->setObjectName(QStringLiteral("scriptElementMenu"));
    m_scriptMenu->addAction(a[QStringLiteral("scriptElements")]);
    m_scriptMenu->addSeparator();
    m_scriptFormatMenu = m_scriptMenu->addMenu(tr("Script &Format"));
    m_scriptFormatMenu->setObjectName(QStringLiteral("scriptFormatMenu"));
    connect(m_scriptFormatMenu, &QMenu::aboutToShow, this, &MainWindow::fillScriptFormatMenu);
    m_scriptMenu->addAction(a[QStringLiteral("scriptSettings")]);
    connect(m_scriptMenu, &QMenu::aboutToShow, this, &MainWindow::updateScriptActions);
}

void MainWindow::fillScriptFormatMenu()
{
    m_scriptFormatMenu->clear();
    if (!m_project)
        return;
    const script::Format &cur = script::format(m_project->formatting);
    bool listed = false;
    script::Kind last = script::Kind::BuiltIn;
    for (const script::Entry &e : script::available()) {
        if (e.kind != last)
            m_scriptFormatMenu->addSeparator();
        last = e.kind;
        const script::Format f = e.format;
        QAction *ac = m_scriptFormatMenu->addAction(QString(f.name).replace(QLatin1Char('&'), QLatin1String("&&")));
        ac->setCheckable(true);
        ac->setToolTip(f.description);
        // a format of this name that was changed in Script Settings is listed below as the project's own
        const bool isCurrent = f == cur;
        ac->setChecked(isCurrent);
        listed = listed || isCurrent;
        connect(ac, &QAction::triggered, this, [this, f] { chooseScriptFormat(f); });
    }
    if (!listed) {
        // a format of this project only
        m_scriptFormatMenu->addSeparator();
        QAction *ac = m_scriptFormatMenu->addAction(tr("%1 (this project)").arg(cur.name));
        ac->setCheckable(true);
        ac->setChecked(true);
    }
    m_scriptFormatMenu->setToolTipsVisible(true);
}

void MainWindow::rebuildScriptActions()
{
    for (QAction *old : m_scriptActions) {
        removeAction(old);
        delete old;
    }
    m_scriptActions.clear();
    commands::removeDynamic(QStringLiteral("script:"));
    if (m_scriptElementMenu)
        m_scriptElementMenu->clear();
    const script::Format &fmt = script::format(m_project ? m_project->formatting : Formatting());
    auto make = [this](const QString &name, const QString &text) {
        auto *ac = new QAction(QString(text).replace(QLatin1Char('&'), QLatin1String("&&")), this);
        ac->setObjectName(QStringLiteral("script:") + name);
        ac->setData(name);
        connect(ac, &QAction::triggered, this, [this, name] {
            TextEditor *e = editor();
            if (!e || !m_project || e->isNotes() || !e->item())
                return;
            // choosing an element makes the document a script
            if (!e->item()->scriptMode && !name.isEmpty())
                setScriptMode(true);
            e->setScriptElement(name);
            updateScriptActions();
        });
        addAction(ac);
        m_scriptActions << ac;
        commands::add(ac, tr("Format"), tr("Scriptwriting ▸ Change Element To"), true);
        if (m_scriptElementMenu)
            m_scriptElementMenu->addAction(ac);
    };
    for (const script::Element &e : fmt.elements)
        make(e.name, e.name);
    if (m_scriptElementMenu)
        m_scriptElementMenu->addSeparator();
    make(QString(), tr("General Text"));
    commands::apply();
    for (QuickRefWindow *qr : quickRefWindows())
        shareShortcuts(qr);
    if (m_composition)
        shareShortcuts(m_composition);
    updateScriptActions();
}

// The document the script commands act on: the one with the cursor.
static BinderItem *scriptItem(TextEditor *e)
{
    return e && !e->isNotes() && e->item() && e->item()->hasText() ? e->item() : nullptr;
}

void MainWindow::updateScriptActions()
{
    QAction *mode = a.value(QStringLiteral("scriptMode"));
    if (!mode)
        return;
    const script::Format &fmt = script::format(m_project ? m_project->formatting : Formatting());
    // "Script Mode - Screenplay", as the format is called
    mode->setText(tr("Script &Mode - %1").arg(QString(fmt.name).replace(QLatin1Char('&'), QLatin1String("&&"))));
    BinderItem *it = m_project ? scriptItem(editor()) : nullptr;
    if (!it && m_project && m_active)
        it = m_active->currentItem() && m_active->currentItem()->hasText() ? m_active->currentItem() : nullptr;
    mode->setEnabled(it != nullptr);
    mode->setChecked(it && it->scriptMode);
    a[QStringLiteral("scriptSettings")]->setEnabled(m_project != nullptr);
    TextEditor *e = editor();
    const bool inScript = e && e->scriptActive();
    a[QStringLiteral("scriptElements")]->setEnabled(inScript);
    const QString cur = inScript ? e->scriptElementAtCursor() : QStringLiteral("\x01");
    for (QAction *ac : m_scriptActions) {
        ac->setEnabled(m_project && scriptItem(e));
        ac->setCheckable(true);
        ac->setChecked(inScript && ac->data().toString().compare(cur, Qt::CaseInsensitive) == 0);
    }
}

void MainWindow::setScriptMode(bool on)
{
    if (!m_project)
        return;
    QList<BinderItem *> items;
    if (BinderItem *it = scriptItem(editor()))
        items << it;
    else
        for (BinderItem *it : actionItems())
            if (it->hasText())
                items << it;
    for (BinderItem *it : items)
        m_project->setScriptMode(it, on);
    icons::clearCache();
    for (EditorPane *p : m_panes)
        if (p) {
            p->updateFooter();
            p->refreshHeader();
        }
    updateScriptActions();
    if (TextEditor *e = editor())
        e->setFocus();
}

bool MainWindow::commitScriptFormat(const script::Format &format, const QHash<QString, QString> &renamedElements)
{
    if (!m_project)
        return false;
    QProgressDialog progress(tr("Updating the scripts of the project…"), QString(), 0, 0, this);
    progress.setObjectName(QStringLiteral("scriptProgress"));
    progress.setWindowTitle(tr("Script Format"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(600);
    progress.setAutoClose(false);
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = m_project->setScriptFormat(
        format, renamedElements,
        [&](int done, int total) {
            progress.setMaximum(qMax(1, total));
            progress.setValue(qMin(done, total));
            return true;
        },
        &err);
    QApplication::restoreOverrideCursor();
    progress.reset();
    if (!ok)
        QMessageBox::warning(this, tr("Script Format"), tr("Some documents could not be updated:\n%1").arg(err));
    refreshInspector();
    return ok;
}

void MainWindow::chooseScriptFormat(const script::Format &format)
{
    if (!m_project || script::format(m_project->formatting) == format)
        return;
    // elements the new format does not have
    const script::Format &cur = script::format(m_project->formatting);
    QStringList lost;
    const QHash<QString, QString> moved = script::renamedStyles(cur, format);
    for (const script::Element &e : cur.elements)
        if (!format.find(e.name) && !moved.contains(script::styleName(e.name)))
            lost << e.name;
    bool anyScript = false;
    for (BinderItem *it : m_project->allItems())
        anyScript = anyScript || it->scriptMode;
    if (anyScript) {
        QString text = tr("Use the script format \"%1\" in this project?\n\nThe scripts of the project get the new layout. "
                          "Elements keep their text; an element the new format does not have becomes the element of the "
                          "same kind.")
                           .arg(format.name);
        if (!lost.isEmpty())
            text += tr("\n\nNo element of the same kind exists for: %1. Their paragraphs become general text that keeps its look.")
                        .arg(lost.join(QLatin1String(", ")));
        QMessageBox box(QMessageBox::Question, tr("Script Format"), text, QMessageBox::Ok | QMessageBox::Cancel, this);
        box.setObjectName(QStringLiteral("scriptFormatQuestion"));
        if (box.exec() != QMessageBox::Ok)
            return;
    }
    commitScriptFormat(format);
}

void MainWindow::showScriptSettings()
{
    if (!m_project)
        return;
    ScriptSettingsDialog dlg(script::format(m_project->formatting), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.format() == script::format(m_project->formatting))
        return;
    commitScriptFormat(dlg.format(), dlg.renamed());
}

} // namespace qb
