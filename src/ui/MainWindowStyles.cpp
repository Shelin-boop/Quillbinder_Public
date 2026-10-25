// Format > Style: the main window's commands for paragraph and character
// styles (menus, format bar list, Styles Panel, creating and changing styles).

#include "ui/MainWindow.h"

#include "AppInfo.h"
#include "core/DocumentStore.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/FormattingChange.h"
#include "ui/Ruler.h"
#include "ui/StyleDialogs.h"
#include "ui/TextEditor.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTextBlock>

namespace qb {

namespace {

// Formatting at the selection: the paragraph that holds its start and the
// text right after it.
void formatsAt(TextEditor *e, QTextBlockFormat *bf, QTextCharFormat *cf)
{
    const QTextCursor c = e->textCursor();
    *cf = e->currentCharFormat();
    if (c.hasSelection()) {
        QTextCursor p(e->document());
        p.setPosition(c.selectionStart() + 1);
        *cf = p.charFormat();
    } else if (!c.atBlockEnd() || c.block().length() > 1) {
        QTextCursor p(e->document());
        p.setPosition(qMin(c.position() + 1, c.block().position() + c.block().length() - 1));
        if (p.position() > c.block().position())
            *cf = p.charFormat();
    }
    *bf = e->document()->findBlock(c.selectionStart()).blockFormat();
}

} // namespace

void MainWindow::fillStyleMenu(QMenu *menu, TextEditor *target, bool full)
{
    menu->clear();
    const Formatting f = m_project ? m_project->formatting : Formatting();
    TextEditor *shown = target ? target : editor();
    const QString curPara = shown ? shown->paragraphStyleAtCursor() : QString();
    const QString curChar = shown ? shown->characterStyleAtCursor() : QString();
    auto apply = [this, target](const QString &name) {
        if (TextEditor *e = target ? target : editor()) {
            e->applyStyle(name);
            updateFormatBar();
        }
    };
    auto entry = [&](QMenu *m, const QString &text, const QString &name, bool checked) {
        // The main menu shows the commands themselves (they carry the keyboard
        // shortcuts); a context menu acts on its own editor.
        QAction *ac = nullptr;
        if (!target)
            for (QAction *command : m_styleActions)
                if (command->data().toString() == name)
                    ac = command;
        if (ac) {
            m->addAction(ac);
        } else {
            ac = m->addAction(text, this, [apply, name] { apply(name); });
            ac->setEnabled(shown != nullptr);
        }
        // only the style at the cursor carries a mark
        ac->setCheckable(checked);
        ac->setChecked(checked);
        return ac;
    };
    entry(menu, tr("No Style"), QString(), shown && curPara.isEmpty() && curChar.isEmpty());
    menu->addSeparator();
    const StyleSheet &sheet = styles::sheet(f);
    bool anyChar = false;
    for (const Style &s : sheet.styles) {
        if (s.character) {
            anyChar = true;
            continue;
        }
        entry(menu, styles::marker(s) + s.name, s.name, s.name == curPara);
    }
    if (anyChar) {
        menu->addSeparator();
        for (const Style &s : sheet.styles)
            if (s.character)
                entry(menu, styles::marker(s) + s.name, s.name, s.name == curChar);
    }
    if (!full)
        return;
    menu->addSeparator();
    menu->addAction(a[QStringLiteral("styleNew")]);
    menu->addAction(a[QStringLiteral("styleRedefine")]);
    QMenu *edit = menu->addMenu(tr("&Edit Style"));
    QMenu *del = menu->addMenu(tr("&Delete Style"));
    edit->setEnabled(m_project != nullptr);
    for (const Style &s : sheet.styles) {
        const QString n = s.name;
        edit->addAction(styles::marker(s) + n + QStringLiteral("…"), this, [this, n] { editStyle(n); });
        if (!styles::isFixedName(n))
            del->addAction(styles::marker(s) + n, this, [this, n] { deleteStyle(n); });
    }
    del->setEnabled(m_project != nullptr && !del->isEmpty());
    menu->addSeparator();
    menu->addAction(a[QStringLiteral("stylesPanel")]);
    menu->addAction(a[QStringLiteral("styleImport")]);
    menu->addAction(a[QStringLiteral("styleSaveDefault")]);
    for (const char *k : {"styleImport", "styleSaveDefault"})
        a[QLatin1String(k)]->setEnabled(m_project != nullptr);
}

void MainWindow::rebuildStyleCombo()
{
    if (!m_styleCombo)
        return;
    const Formatting f = m_project ? m_project->formatting : Formatting();
    const bool was = m_updatingUi;
    m_updatingUi = true;
    m_styleCombo->clear();
    m_styleCombo->addItem(tr("No Style"), QString());
    bool anyChar = false;
    for (const Style &s : styles::sheet(f).styles) {
        if (s.character) {
            anyChar = true;
            continue;
        }
        m_styleCombo->addItem(styles::marker(s) + s.name, s.name);
    }
    if (anyChar) {
        m_styleCombo->insertSeparator(m_styleCombo->count());
        for (const Style &s : styles::sheet(f).styles)
            if (s.character)
                m_styleCombo->addItem(styles::marker(s) + s.name, s.name);
    }
    m_updatingUi = was;
}

void MainWindow::refreshStyleUi()
{
    rebuildStyleCombo();
    rebuildStyleActions();
    if (m_stylesPanel)
        m_stylesPanel->setStyles(m_project ? m_project->formatting : Formatting(), m_project != nullptr);
    if (m_styleMenu)
        fillStyleMenu(m_styleMenu, nullptr, true);
    updateFormatBar();
    for (EditorPane *p : m_panes)
        if (p && p->ruler())
            p->ruler()->update();
}

bool MainWindow::commitStyles(const StyleSheet &sheet, const QHash<QString, QString> &renamed)
{
    if (!m_project)
        return false;
    QProgressDialog progress(tr("Updating the text of the project…"), QString(), 0, 0, this);
    progress.setObjectName(QStringLiteral("styleProgress"));
    progress.setWindowTitle(tr("Styles"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(600);
    progress.setAutoClose(false);
    QString err;
    const Formatting oldF = m_project->formatting;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_project->setStyleSheet(
        sheet, renamed,
        [&](int done, int total) {
            progress.setMaximum(qMax(1, total));
            progress.setValue(qMin(done, total));
            return true;
        },
        &err);
    // A style with a new font or size: text of the style that has the old one set by
    // hand (imported text names its font everywhere) shows the style as well.
    for (const Style &s : styles::sheet(m_project->formatting).styles) {
        const QString oldName = renamed.key(s.name, s.name);
        const Style *was = styles::sheet(oldF).find(oldName);
        if (!was || was->name != oldName || was->character != s.character)
            continue;
        const direct::CharSpec before = formatchange::styleFont(oldF, oldName);
        const direct::CharSpec after = formatchange::styleFont(m_project->formatting, s.name);
        int members = 0;
        if (!before.family.isEmpty() && before.family.compare(after.family, Qt::CaseInsensitive) != 0)
            members |= direct::Family;
        if (before.size > 0 && qAbs(before.size - after.size) >= 0.05)
            members |= direct::Size;
        if (members && formatchange::followStyle(m_project.get(), s.name, s.character, members, before, after, &err) < 0)
            ok = false;
    }
    QApplication::restoreOverrideCursor();
    progress.reset();
    if (!ok)
        QMessageBox::warning(this, tr("Styles"), tr("Some documents could not be updated:\n%1").arg(err));
    refreshInspector();
    return ok;
}

void MainWindow::newStyleFromSelection()
{
    TextEditor *e = editor();
    if (!e || !m_project) {
        QMessageBox::information(this, tr("New Style From Selection"),
                                 tr("Click into text that has the formatting the style should get first."));
        return;
    }
    QTextBlockFormat bf;
    QTextCharFormat cf;
    formatsAt(e, &bf, &cf);
    StyleFromSelectionDialog dlg(StyleFromSelectionDialog::New, m_project->formatting, bf, cf, QString(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Style s = dlg.style();
    StyleSheet sheet = styles::sheet(m_project->formatting);
    // paragraph styles stay together in front of the character styles
    int at = sheet.styles.size();
    if (!s.character)
        for (int i = 0; i < sheet.styles.size(); ++i)
            if (sheet.styles[i].character) {
                at = i;
                break;
            }
    sheet.styles.insert(at, s);
    commitStyles(sheet);
    // the text the style was made from now uses it
    e->applyStyle(s.name);
    updateFormatBar();
    e->setFocus();
}

void MainWindow::redefineStyleFromSelection(const QString &name)
{
    TextEditor *e = editor();
    if (!e || !m_project) {
        QMessageBox::information(this, tr("Redefine Style From Selection"),
                                 tr("Click into text that has the formatting the style should get first."));
        return;
    }
    QString preferred = name;
    if (preferred.isEmpty())
        preferred = e->characterStyleAtCursor();
    if (preferred.isEmpty())
        preferred = e->paragraphStyleAtCursor();
    QTextBlockFormat bf;
    QTextCharFormat cf;
    formatsAt(e, &bf, &cf);
    StyleFromSelectionDialog dlg(StyleFromSelectionDialog::Redefine, m_project->formatting, bf, cf, preferred, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Style s = dlg.style();
    StyleSheet sheet = styles::sheet(m_project->formatting);
    const int idx = sheet.indexOf(s.name);
    if (idx < 0)
        return;
    sheet.styles[idx] = s;
    commitStyles(sheet);
    // what was set by hand at the selection is part of the style now
    if (s.character ? e->characterStyleAtCursor() == s.name : e->paragraphStyleAtCursor() == s.name)
        e->applyStyle(s.name);
    updateFormatBar();
    e->setFocus();
}

void MainWindow::editStyle(const QString &name)
{
    if (!m_project)
        return;
    StyleSheet sheet = styles::sheet(m_project->formatting);
    const int idx = sheet.indexOf(name);
    if (idx < 0)
        return;
    StyleEditDialog dlg(sheet.styles[idx], m_project->formatting, this);
    // Apply and OK: the style changes in the whole project
    auto commit = [this, &dlg] {
        StyleSheet current = styles::sheet(m_project->formatting);
        const int at = current.indexOf(dlg.originalName());
        const Style s = dlg.style();
        if (at < 0 || s == current.styles[at])
            return;
        QHash<QString, QString> renamed;
        if (s.name != current.styles[at].name) {
            renamed.insert(current.styles[at].name, s.name);
            // styles that name it as the following style follow the renaming
            for (Style &o : current.styles)
                if (o.next == current.styles[at].name)
                    o.next = s.name;
        }
        current.styles[at] = s;
        commitStyles(current, renamed);
        dlg.applied(s, m_project->formatting);
        updateFormatBar();
    };
    connect(&dlg, &StyleEditDialog::applyRequested, this, commit);
    if (dlg.exec() != QDialog::Accepted)
        return;
    commit();
}

void MainWindow::deleteStyle(const QString &name)
{
    if (!m_project || styles::isFixedName(name))
        return;
    StyleSheet sheet = styles::sheet(m_project->formatting);
    const int idx = sheet.indexOf(name);
    if (idx < 0)
        return;
    const QString shown = sheet.styles[idx].name;
    if (QMessageBox::question(this, tr("Delete Style"),
                              tr("Delete the style “%1”?\n\nText that uses it keeps its look as formatting set by hand, "
                                 "but is no longer connected to a style.").arg(shown),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes)
        != QMessageBox::Yes)
        return;
    sheet.styles.removeAt(idx);
    for (Style &o : sheet.styles)
        if (o.next == shown)
            o.next.clear();
    commitStyles(sheet);
}

void MainWindow::resetStyle(const QString &name)
{
    if (!m_project)
        return;
    const Style *def = styles::builtinDefault(name);
    StyleSheet sheet = styles::sheet(m_project->formatting);
    const int idx = sheet.indexOf(name);
    if (!def || idx < 0 || sheet.styles[idx].character != def->character)
        return;
    Style s = *def;
    s.name = sheet.styles[idx].name;
    sheet.styles[idx] = s;
    commitStyles(sheet);
}

void MainWindow::moveStyle(const QString &name, int delta)
{
    if (!m_project)
        return;
    StyleSheet sheet = styles::sheet(m_project->formatting);
    const int idx = sheet.indexOf(name);
    if (idx < 0)
        return;
    // the neighbour of the same kind
    int other = idx + delta;
    while (other >= 0 && other < sheet.styles.size() && sheet.styles[other].character != sheet.styles[idx].character)
        other += delta;
    if (other < 0 || other >= sheet.styles.size())
        return;
    sheet.styles.swapItemsAt(idx, other);
    commitStyles(sheet);
}

void MainWindow::importStyles()
{
    if (!m_project)
        return;
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Import Styles From a Project"), QFileInfo(m_project->folderPath()).absolutePath(),
        tr("%1 projects (*%2)").arg(QLatin1String(QB_APP_NAME), QLatin1String(QB_PROJECT_FILE_SUFFIX)));
    if (file.isEmpty())
        return;
    StyleSheet in;
    QString err;
    if (!styles::readFromProjectFile(file, &in, &err)) {
        QMessageBox::warning(this, tr("Import Styles"), err);
        return;
    }
    StyleSheet sheet = styles::sheet(m_project->formatting);
    int same = 0, differing = 0;
    for (const Style &s : in.styles) {
        const int idx = sheet.indexOf(s.name);
        if (idx < 0)
            continue;
        if (sheet.styles[idx].character == s.character && !sheet.styles[idx].sameFormatting(s))
            ++differing;
        else
            ++same;
    }
    bool replace = false;
    if (differing > 0) {
        const auto answer = QMessageBox::question(
            this, tr("Import Styles"),
            (differing == 1 ? tr("1 style of this project has the same name as an imported one but looks different.\n\n"
                                 "Replace it with the imported definition? Text that uses it is updated.")
                            : tr("%1 styles of this project have the same name as imported ones but look different.\n\n"
                                 "Replace them with the imported definitions? Text that uses them is updated.")
                                  .arg(differing)),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
        if (answer == QMessageBox::Cancel)
            return;
        replace = answer == QMessageBox::Yes;
    }
    int added = 0, replaced = 0;
    for (const Style &s : in.styles) {
        const int idx = sheet.indexOf(s.name);
        if (idx < 0) {
            int at = sheet.styles.size();
            if (!s.character)
                for (int i = 0; i < sheet.styles.size(); ++i)
                    if (sheet.styles[i].character) {
                        at = i;
                        break;
                    }
            sheet.styles.insert(at, s);
            ++added;
        } else if (replace && sheet.styles[idx].character == s.character && !sheet.styles[idx].sameFormatting(s)) {
            Style n = s;
            n.name = sheet.styles[idx].name;
            n.headingLevel = sheet.styles[idx].headingLevel;
            sheet.styles[idx] = n;
            ++replaced;
        }
    }
    // a following style that was not imported
    for (Style &s : sheet.styles)
        if (!s.next.isEmpty() && s.next != styles::noStyle() && sheet.indexOf(s.next) < 0)
            s.next.clear();
    if (added || replaced)
        commitStyles(sheet);
    QMessageBox::information(this, tr("Import Styles"),
                             tr("%1 new, %2 replaced, %3 left as they are.").arg(added).arg(replaced).arg(same + differing - replaced));
}

void MainWindow::saveStylesAsDefault()
{
    if (!m_project)
        return;
    AppSettings &s = AppSettings::instance();
    s.newStylesXml = styles::toXmlString(styles::sheet(m_project->formatting));
    s.save();
    QMessageBox::information(this, tr("Styles"),
                             tr("New projects start with the styles of this project from now on.\n\n"
                                "File ▸ Options ▸ Editing goes back to the built-in styles."));
}

void MainWindow::showStylesPanel()
{
    if (!m_stylesPanel) {
        m_stylesPanel = new StylesPanel(this);
        StylesPanel *p = m_stylesPanel;
        connect(p, &StylesPanel::applyRequested, this, [this](const QString &name) {
            if (TextEditor *e = editor()) {
                e->applyStyle(name);
                updateFormatBar();
                e->window()->activateWindow();
                e->setFocus();
            }
        });
        connect(p, &StylesPanel::newRequested, this, [this] { newStyleFromSelection(); });
        connect(p, &StylesPanel::redefineRequested, this, [this](const QString &n) { redefineStyleFromSelection(n); });
        connect(p, &StylesPanel::editRequested, this, [this](const QString &n) { editStyle(n); });
        connect(p, &StylesPanel::deleteRequested, this, [this](const QString &n) { deleteStyle(n); });
        connect(p, &StylesPanel::resetRequested, this, [this](const QString &n) { resetStyle(n); });
        connect(p, &StylesPanel::moveRequested, this, [this](const QString &n, int d) { moveStyle(n, d); });
        connect(p, &StylesPanel::importRequested, this, [this] { importStyles(); });
        connect(p, &StylesPanel::saveAsDefaultRequested, this, [this] { saveStylesAsDefault(); });
        // next to the right edge of the main window
        const QRect g = geometry();
        p->move(g.right() - p->width() - 40, g.top() + 120);
    }
    m_stylesPanel->setStyles(m_project ? m_project->formatting : Formatting(), m_project != nullptr);
    if (TextEditor *e = editor())
        m_stylesPanel->setCurrent(e->paragraphStyleAtCursor(), e->characterStyleAtCursor());
    m_stylesPanel->show();
    m_stylesPanel->raise();
    m_stylesPanel->activateWindow();
}

} // namespace qb
