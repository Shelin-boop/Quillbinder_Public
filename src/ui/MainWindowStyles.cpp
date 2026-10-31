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
#include "ui/StyleList.h"
#include "ui/TextEditor.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QLocale>
#include <QTextBlock>

#include <functional>

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
    QAction *custom = menu->addAction(tr("Customize Style &List…"), this, &MainWindow::customizeStyleList);
    custom->setObjectName(QStringLiteral("styleListCustomize"));
    custom->setEnabled(m_project != nullptr);
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
    // all entries: the chosen ones in their order, then those the list leaves out, so that the
    // combo box still names the style of the text (its popup shows only the chosen ones, 0.40.3)
    QList<stylelist::Entry> entries = stylelist::shown(m_project.get());
    for (const stylelist::Entry &e : stylelist::all(m_project.get()))
        if (!e.shown)
            entries << e;
    for (const stylelist::Entry &e : entries)
        m_styleCombo->addItem(e.text, e.name);
    Q_UNUSED(f);
    m_updatingUi = was;
}

void MainWindow::fillStyleList(QMenu *menu, TextEditor *target)
{
    menu->clear();
    const Formatting f = m_project ? m_project->formatting : Formatting();
    TextEditor *shown = target ? target : editor();
    QStringList current;
    if (shown) {
        const QString para = shown->paragraphStyleAtCursor(), chars = shown->characterStyleAtCursor();
        if (para.isEmpty() && chars.isEmpty())
            current << QString();
        if (!para.isEmpty())
            current << para;
        if (!chars.isEmpty())
            current << chars;
    }
    stylelist::addTo(menu, stylelist::shown(m_project.get()), f, current, [this, target](const QString &name) {
        if (TextEditor *e = target ? target : editor()) {
            e->applyStyle(name);
            updateFormatBar();
            e->setFocus();
        }
    });
    if (!shown)
        for (QAction *ac : menu->actions())
            ac->setEnabled(false);
    menu->addSeparator();
    menu->addAction(a[QStringLiteral("styleNew")]);
    menu->addAction(a[QStringLiteral("styleRedefine")]);
    menu->addAction(a[QStringLiteral("stylesPanel")]);
    menu->addSeparator();
    QAction *custom = menu->addAction(tr("Customize Style List…"), this, &MainWindow::customizeStyleList);
    custom->setObjectName(QStringLiteral("styleListCustomize"));
    custom->setEnabled(m_project != nullptr);
}

void MainWindow::customizeStyleList()
{
    if (!m_project)
        return;
    StyleListDialog dlg(m_project.get(), styleUiParent());
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_project->styleListOrder = dlg.order();
    m_project->styleListHidden = dlg.hidden();
    m_project->notifySettingsChanged(); // kept in the project file
    refreshStyleUi();
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
    QProgressDialog progress(tr("Updating the text of the project…"), QString(), 0, 0, styleUiParent());
    progress.setObjectName(QStringLiteral("styleProgress"));
    progress.setWindowTitle(tr("Styles"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(600);
    progress.setAutoClose(false);
    QString err;
    const Formatting oldF = m_project->formatting;
    // a renamed style keeps its place in the chosen style list (0.40.3)
    for (auto it = renamed.cbegin(); it != renamed.cend(); ++it)
        for (QStringList *l : {&m_project->styleListOrder, &m_project->styleListHidden})
            for (QString &n : *l)
                if (n == it.key())
                    n = it.value();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = m_project->setStyleSheet(
        sheet, renamed,
        [&](int done, int total) {
            progress.setMaximum(qMax(1, total));
            progress.setValue(qMin(done, total));
            return true;
        },
        &err);
    // A changed style reaches all text of the style (0.40.2): what the change touches (font,
    // size, weight, color, letter spacing, capitals, alignment, indents, spacing, line
    // spacing, tabs) is taken over also where the text had its own value, set by hand or
    // brought along by an import (imported text names its font and spacing everywhere).
    // Bold, italic, underline and strikethrough follow the style already (Project::setStyleSheet).
    for (const Style &s : styles::sheet(m_project->formatting).styles) {
        const QString oldName = renamed.key(s.name, s.name);
        const Style *was = styles::sheet(oldF).find(oldName);
        if (!was || was->name != oldName || was->character != s.character)
            continue;
        const direct::CharSpec before = formatchange::styleFont(oldF, oldName);
        const direct::CharSpec after = formatchange::styleFont(m_project->formatting, s.name);
        int members = direct::charMembersBetween(was->chars, s.chars) & ~(direct::Family | direct::Size);
        if (before.family.compare(after.family, Qt::CaseInsensitive) != 0)
            members |= direct::Family;
        if (qAbs(before.size - after.size) >= 0.05)
            members |= direct::Size;
        const int bits = s.character ? 0 : direct::paraBitsBetween(was->para, s.para);
        if ((members || bits) && formatchange::takeStyle(m_project.get(), s.name, s.character, members, bits, &err) < 0)
            ok = false;
    }
    QApplication::restoreOverrideCursor();
    progress.reset();
    if (!ok)
        QMessageBox::warning(styleUiParent(), tr("Styles"), tr("Some documents could not be updated:\n%1").arg(err));
    refreshInspector();
    return ok;
}

void MainWindow::newStyleFromSelection()
{
    TextEditor *e = editor();
    if (!e || !m_project) {
        QMessageBox::information(styleUiParent(), tr("New Style From Selection"),
                                 tr("Click into text that has the formatting the style should get first."));
        return;
    }
    QTextBlockFormat bf;
    QTextCharFormat cf;
    formatsAt(e, &bf, &cf);
    StyleFromSelectionDialog dlg(StyleFromSelectionDialog::New, m_project->formatting, bf, cf, QString(), styleUiParent());
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
        QMessageBox::information(styleUiParent(), tr("Redefine Style From Selection"),
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
    StyleFromSelectionDialog dlg(StyleFromSelectionDialog::Redefine, m_project->formatting, bf, cf, preferred, styleUiParent());
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
    StyleEditDialog dlg(sheet.styles[idx], m_project->formatting, styleUiParent());
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
    if (QMessageBox::question(styleUiParent(), tr("Delete Style"),
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
        styleUiParent(), tr("Import Styles From a Project"), QFileInfo(m_project->folderPath()).absolutePath(),
        tr("%1 projects (*%2)").arg(QLatin1String(QB_APP_NAME), QLatin1String(QB_PROJECT_FILE_SUFFIX)));
    if (file.isEmpty())
        return;
    StyleSheet in;
    QString err;
    if (!styles::readFromProjectFile(file, &in, &err)) {
        QMessageBox::warning(styleUiParent(), tr("Import Styles"), err);
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
    QMessageBox::information(styleUiParent(), tr("Import Styles"),
                             tr("%1 new, %2 replaced, %3 left as they are.").arg(added).arg(replaced).arg(same + differing - replaced));
}

void MainWindow::saveStylesAsDefault()
{
    if (!m_project)
        return;
    AppSettings &s = AppSettings::instance();
    s.newStylesXml = styles::toXmlString(styles::sheet(m_project->formatting));
    s.save();
    QMessageBox::information(styleUiParent(), tr("Styles"),
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
        // what the panel starts opens above the panel (0.40.3: the dialogs belonged to the main
        // window, and the window manager kept the panel, a tool window, in front of them)
        auto fromPanel = [this, p](const std::function<void()> &f) {
            const QPointer<QWidget> was = m_styleUiParent;
            m_styleUiParent = p;
            f();
            m_styleUiParent = was;
        };
        connect(p, &StylesPanel::newRequested, this, [this, fromPanel] { fromPanel([this] { newStyleFromSelection(); }); });
        connect(p, &StylesPanel::redefineRequested, this,
                [this, fromPanel](const QString &n) { fromPanel([this, n] { redefineStyleFromSelection(n); }); });
        connect(p, &StylesPanel::redefineNoStyleRequested, this,
                [this, fromPanel] { fromPanel([this] { redefineNoStyleFromSelection(); }); });
        connect(p, &StylesPanel::editRequested, this, [this, fromPanel](const QString &n) { fromPanel([this, n] { editStyle(n); }); });
        connect(p, &StylesPanel::deleteRequested, this,
                [this, fromPanel](const QString &n) { fromPanel([this, n] { deleteStyle(n); }); });
        connect(p, &StylesPanel::resetRequested, this, [this, fromPanel](const QString &n) { fromPanel([this, n] { resetStyle(n); }); });
        connect(p, &StylesPanel::moveRequested, this,
                [this, fromPanel](const QString &n, int d) { fromPanel([this, n, d] { moveStyle(n, d); }); });
        connect(p, &StylesPanel::importRequested, this, [this, fromPanel] { fromPanel([this] { importStyles(); }); });
        connect(p, &StylesPanel::saveAsDefaultRequested, this, [this, fromPanel] { fromPanel([this] { saveStylesAsDefault(); }); });
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

bool MainWindow::formattingAtCursor(TextEditor *e, Formatting *f) const
{
    if (!e || !m_project)
        return false;
    const QTextBlockFormat bf = e->textCursor().blockFormat();
    *f = m_project->formatting;
    const direct::CharSpec sp = e->charSpecAtCursor();
    if (!sp.family.isEmpty())
        f->fontFamily = sp.family;
    if (sp.size > 0)
        f->fontSize = sp.size;
    if (bf.lineHeightType() == QTextBlockFormat::ProportionalHeight && bf.lineHeight() > 0)
        f->lineSpacing = bf.lineHeight() / 100.0;
    else if (bf.lineHeightType() == QTextBlockFormat::SingleHeight)
        f->lineSpacing = 1.0;
    if (!(bf.alignment() & (Qt::AlignHCenter | Qt::AlignRight)) && !e->textCursor().currentList())
        f->firstLineIndent = qMax(0.0, bf.textIndent());
    f->paragraphSpacing = bf.bottomMargin();
    f->align = bodyAlignment(bf.alignment());
    return true;
}

void MainWindow::redefineNoStyleFromSelection()
{
    TextEditor *e = editor();
    Formatting nf;
    if (!formattingAtCursor(e, &nf)) {
        QMessageBox::information(styleUiParent(), tr("Redefine No Style From Selection"),
                                 tr("Click into text that has the formatting the main text should get first."));
        return;
    }
    const Formatting oldF = m_project->formatting;
    const QString align = nf.align == Qt::AlignHCenter ? tr("centered")
                          : nf.align == Qt::AlignRight  ? tr("right")
                          : nf.align == Qt::AlignJustify ? tr("justified")
                                                         : tr("left");
    const QString summary =
        tr("Font: %1, %2 pt\nLine spacing: %3\nFirst line indent: %4\nSpace after paragraphs: %5 pt\nAlignment: %6")
            .arg(nf.fontFamily.isEmpty() ? tr("automatic serif font") : nf.fontFamily)
            .arg(QLocale().toString(nf.fontSize, 'f', 1))
            .arg(QLocale().toString(nf.lineSpacing, 'f', 2))
            .arg(QLocale().toString(units::fromPx(nf.firstLineIndent), 'f', 2) + units::suffix())
            .arg(QLocale().toString(direct::pxToPt(nf.paragraphSpacing), 'f', 1))
            .arg(align);
    const bool same = nf.fontFamily == oldF.fontFamily && qAbs(nf.fontSize - oldF.fontSize) < 0.05
                      && qAbs(nf.lineSpacing - oldF.lineSpacing) < 0.005 && qAbs(nf.firstLineIndent - oldF.firstLineIndent) < 0.05
                      && qAbs(nf.paragraphSpacing - oldF.paragraphSpacing) < 0.05 && nf.align == oldF.align;
    if (same) {
        QMessageBox::information(styleUiParent(), tr("Redefine No Style From Selection"),
                                 tr("The main text style of the project already has this formatting:\n\n%1").arg(summary));
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Redefine No Style From Selection"),
                    tr("Make the formatting at the cursor the main text style of the project (\"No Style\")?\n\n%1\n\n"
                       "All text without a style in the whole project takes it over, also text with a font, size or "
                       "spacing of its own. Styles keep what they set themselves; the rest of their look follows the "
                       "main text style. Project > Project Settings > Formatting shows the same settings.")
                        .arg(summary),
                    QMessageBox::Ok | QMessageBox::Cancel, styleUiParent());
    box.setObjectName(QStringLiteral("redefineNoStyle"));
    box.setDefaultButton(QMessageBox::Ok);
    if (box.exec() != QMessageBox::Ok)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_project->formatting = nf; // styles and script format come along unchanged
    m_project->docs()->applyFormatting();
    QString err;
    const int n = formatchange::followProject(m_project.get(), oldF, true, &err);
    m_project->notifySettingsChanged();
    QApplication::restoreOverrideCursor();
    if (n < 0)
        QMessageBox::warning(styleUiParent(), tr("Redefine No Style From Selection"),
                             tr("Some documents could not be changed:\n%1").arg(err));
    refreshInspector();
    updateFormatBar();
    for (EditorPane *p : m_panes)
        if (p && p->ruler())
            p->ruler()->update();
    if (e)
        e->setFocus();
}

} // namespace qb
