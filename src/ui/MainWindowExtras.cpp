// Additions of 0.17 to the main window: Navigate > Fonts, Documents > Change
// Icon (the whole icon set, recent icons, Icon from Text, Manage Icons), the
// icon bar, the two color buttons of the format bar, Help > Developer Guide.
#include "ui/ObjectsUi.h"
#include "AppInfo.h"
#include "core/IconCatalog.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "ui/MindMapHelp.h"
#include "ui/CompareDialog.h"
#include "import/Compare.h"
#include "ui/AppSettings.h"
#include "ui/ColorPickButton.h"
#include "ui/EditorPane.h"
#include "ui/FontInstall.h"
#include "ui/IconDialogs.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace qb {

void MainWindow::createExtraActions()
{
    auto act = [this](const QString &key, const QString &text, const QString &icon = QString()) {
        auto *ac = new QAction(text, this);
        ac->setObjectName(key);
        QString plain = text;
        plain.remove(QLatin1Char('&'));
        if (plain.endsWith(QChar(0x2026)))
            plain.chop(1);
        ac->setIconText(plain);
        if (!icon.isEmpty())
            ac->setIcon(icons::action(icon));
        a.insert(key, ac);
        addAction(ac);
        return ac;
    };
    // ---- Navigate > Fonts
    connect(act(QStringLiteral("installFonts"), tr("&Install Font Files…"), QStringLiteral("font")), &QAction::triggered, this,
            [this] { installFonts(); });
    connect(act(QStringLiteral("fontFolder"), tr("Open &Font Folder"), QStringLiteral("folder-open")), &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fontinstall::userFontDir()));
    });
    connect(act(QStringLiteral("fontManager"), tr("Open Font &Manager of the Desktop"), QStringLiteral("preferences-desktop-font")),
            &QAction::triggered, this, [this] {
                QString which;
                if (!fontinstall::openFontManager(&which))
                    QMessageBox::information(this, tr("Font Manager"),
                                             tr("No font manager was found on this computer (KDE: System Settings > Text & Fonts > "
                                                "Font Management; or the package “font-manager”).\n\nFonts can be installed here with "
                                                "Navigate > Fonts > Install Font Files, or by copying them into:\n%1")
                                                 .arg(fontinstall::userFontDir()));
            });
    connect(act(QStringLiteral("reloadFonts"), tr("&Reload Font List"), QStringLiteral("view-refresh")), &QAction::triggered, this, [this] {
        const QStringList fresh = fontinstall::reload();
        QMessageBox::information(this, tr("Fonts"),
                                 fresh.isEmpty() ? tr("No new fonts were found in your font folders.\n(Fonts installed for all users need "
                                                      "a restart of the program.)")
                                                 : tr("New fonts in the font lists:\n%1").arg(fresh.join(QStringLiteral(", "))));
    });
    // ---- icons
    connect(act(QStringLiteral("resetIcon"), tr("&Reset Icon to Default")), &QAction::triggered, this, [this] { setIconFor(QString()); });
    connect(act(QStringLiteral("iconFromText"), tr("Icon from &Text…")), &QAction::triggered, this, [this] {
        if (!m_project)
            return;
        IconFromTextDialog dlg(m_project.get(), this);
        if (dlg.exec() == QDialog::Accepted)
            setIconFor(dlg.createdIcon());
    });
    connect(act(QStringLiteral("manageIcons"), tr("&Manage Icons…")), &QAction::triggered, this, [this] {
        if (!m_project)
            return;
        ManageIconsDialog dlg(m_project.get(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.chosen().isEmpty())
            setIconFor(dlg.chosen());
    });
    // ---- icon bar
    QAction *showBar = act(QStringLiteral("showIconBar"), tr("&Icon Bar"));
    showBar->setCheckable(true);
    connect(act(QStringLiteral("customizeIconBar"), tr("Choose Icons for the Icon &Bar…"), QStringLiteral("configure-toolbars")),
            &QAction::triggered, this, [this] { chooseIconBar(); });
    // ---- help
    connect(act(QStringLiteral("compareFile"), tr("Compare with &File…"), QStringLiteral("document-import")), &QAction::triggered, this,
            [this] { chooseCompareFile(); });
    a[QStringLiteral("compareFile")]->setToolTip(tr("Compare a Word or LibreOffice file with what Compile makes of the project, "
                                                   "and take the differences over to one side"));
    QAction *byChapter = act(QStringLiteral("compareByChapter"), tr("By &Chapter"));
    byChapter->setCheckable(true);
    byChapter->setChecked(AppSettings::instance().compareByChapter);
    connect(byChapter, &QAction::toggled, this, [](bool on) {
        AppSettings::instance().compareByChapter = on;
        AppSettings::instance().save();
    });
    QAction *stepByStep = act(QStringLiteral("compareStepByStep"), tr("&Step by Step"));
    stepByStep->setCheckable(true);
    stepByStep->setChecked(AppSettings::instance().compareStepByStep);
    connect(stepByStep, &QAction::toggled, this, [](bool on) {
        AppSettings::instance().compareStepByStep = on;
        AppSettings::instance().save();
    });
    connect(act(QStringLiteral("mindMapHelp"), tr("&Mind Map Help"), QStringLiteral("qb-mindmap")), &QAction::triggered, this,
            [this] { mindmaphelp::showHelp(this); });
    connect(act(QStringLiteral("developerGuide"), tr("&Developer Guide: Updates and Add-ons"), QStringLiteral("help-about")),
            &QAction::triggered, this, [this] { showDeveloperGuide(); });
}

// ------------------------------------------------------------------ fonts

void MainWindow::createFontMenu(QMenu *nav)
{
    QMenu *fonts = nav->addMenu(icons::action(QStringLiteral("font")), tr("&Fonts"));
    fonts->setObjectName(QStringLiteral("fontsMenu"));
    for (const char *k : {"installFonts", "fontFolder", "fontManager", "", "reloadFonts"}) {
        if (!*k)
            fonts->addSeparator();
        else
            fonts->addAction(a[QLatin1String(k)]);
    }
}

void MainWindow::installFonts()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Install Font Files"), QDir::homePath(),
                                                            tr("Fonts (*.ttf *.otf *.ttc *.otc *.pfb *.pfa *.woff *.woff2 *.zip);;All files (*)"));
    if (files.isEmpty())
        return;
    const fontinstall::Result r = fontinstall::install(files);
    QString msg;
    if (!r.families.isEmpty())
        msg = tr("Installed for you (in %1):\n%2\n\nThe fonts can be used at once; other programs see them after their next start.")
                  .arg(fontinstall::userFontDir(), r.families.join(QStringLiteral(", ")));
    else if (!r.installed.isEmpty())
        msg = tr("The files are in %1; no new font families came with them (they were installed already).").arg(fontinstall::userFontDir());
    if (!r.skipped.isEmpty())
        msg += (msg.isEmpty() ? QString() : QStringLiteral("\n\n")) + tr("Not installed:") + QStringLiteral("\n• ")
               + r.skipped.join(QStringLiteral("\n• "));
    QMessageBox::information(this, tr("Install Font Files"), msg);
}

// ------------------------------------------------------------------ icons

void MainWindow::rememberIcon(const QString &icon)
{
    if (icon.isEmpty())
        return;
    QStringList &rec = AppSettings::instance().recentIcons;
    rec.removeAll(icon);
    rec.prepend(icon);
    while (rec.size() > 5)
        rec.removeLast();
    AppSettings::instance().save();
}

void MainWindow::fillIconMenu(QMenu *menu)
{
    menu->clear();
    const Project *p = m_project.get();
    menu->addAction(a[QStringLiteral("resetIcon")]);
    menu->addAction(a[QStringLiteral("iconFromText")]);
    menu->addAction(a[QStringLiteral("manageIcons")]);
    auto entry = [this, p](QMenu *m, const QString &id, const QString &text) {
        QAction *ac = m->addAction(icons::iconFor(p, id), QString(text).replace(QLatin1Char('&'), QLatin1String("&&")), this,
                                   [this, id] { setIconFor(id); });
        ac->setData(id);
        return ac;
    };
    // the icons chosen last
    QStringList recent;
    for (const QString &id : AppSettings::instance().recentIcons)
        if (!icons::isCustom(id) || icons::customNames(p).contains(icons::customName(id)))
            recent << id;
    if (!recent.isEmpty()) {
        menu->addSeparator();
        menu->addSection(tr("Recent"));
        for (const QString &id : recent)
            entry(menu, id, icons::label(id));
    }
    menu->addSeparator();
    for (const QString &g : icons::groups()) {
        QMenu *sub = menu->addMenu(icons::get(icons::groupIcon(g)), QObject::tr(g.toUtf8().constData()));
        sub->setObjectName(QStringLiteral("iconGroup_") + g);
        for (const icons::CatalogEntry &e : icons::inGroup(g))
            entry(sub, e.id, e.label);
    }
    menu->addSeparator();
    for (const icons::CatalogEntry &e : icons::inGroup(QString()))
        entry(menu, e.id, e.label);
    const QStringList own = icons::customNames(p);
    menu->addSeparator();
    if (!own.isEmpty()) {
        QMenu *sub = menu->addMenu(icons::get(QStringLiteral("qb-proofread")), tr("&Project Icons"));
        sub->setObjectName(QStringLiteral("iconGroup_Project"));
        for (const QString &n : own)
            entry(sub, QStringLiteral("custom:") + n, n);
    }
    QMenu *more = menu->addMenu(tr("More Icons (Desktop)"));
    more->setObjectName(QStringLiteral("iconGroup_More"));
    for (const icons::Choice &c : icons::customChoices())
        entry(more, c.name, c.label);
}

// ------------------------------------------------------------------ icon bar

QStringList MainWindow::iconBarIcons() const
{
    const AppSettings &s = AppSettings::instance();
    QStringList l = s.iconBarSet ? s.iconBar : standardIconBar();
    while (l.size() > IconBarDialog::kMax)
        l.removeLast();
    return l;
}

void MainWindow::createIconBar()
{
    m_iconBar = new QToolBar(tr("Icon Bar"), this);
    m_iconBar->setObjectName(QStringLiteral("iconBar"));
    m_iconBar->setMovable(false);
    m_iconBar->setIconSize(QSize(20, 20));
    addToolBar(Qt::TopToolBarArea, m_iconBar);
    connect(a[QStringLiteral("showIconBar")], &QAction::toggled, m_iconBar, &QWidget::setVisible);
    m_iconBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_iconBar, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu;
        menu.addAction(a[QStringLiteral("customizeIconBar")]);
        menu.addAction(a[QStringLiteral("showIconBar")]);
        menu.exec(m_iconBar->mapToGlobal(pos));
    });
    rebuildIconBar();
}

void MainWindow::rebuildIconBar()
{
    if (!m_iconBar)
        return;
    m_iconBar->clear();
    const Project *p = m_project.get();
    auto *label = new QWidgetAction(m_iconBar);
    auto *l = new QLabel(tr("Icon:"));
    l->setContentsMargins(6, 0, 4, 0);
    label->setDefaultWidget(l);
    m_iconBar->addAction(label);
    int n = 0;
    for (const QString &id : iconBarIcons()) {
        QAction *ac = m_iconBar->addAction(icons::iconFor(p, id), icons::label(id));
        ac->setObjectName(QStringLiteral("iconBar_") + id);
        ac->setToolTip(tr("Give the selected documents the icon “%1”").arg(icons::label(id)));
        ac->setData(id);
        ac->setEnabled(m_project != nullptr);
        connect(ac, &QAction::triggered, this, [this, id] { setIconFor(id); });
        ++n;
    }
    m_iconBar->addSeparator();
    QAction *reset = m_iconBar->addAction(icons::action(QStringLiteral("edit-undo")), tr("Default Icon"));
    reset->setObjectName(QStringLiteral("iconBar_reset"));
    reset->setToolTip(tr("Give the selected documents their standard icon back"));
    reset->setEnabled(m_project != nullptr);
    connect(reset, &QAction::triggered, this, [this] { setIconFor(QString()); });
    m_iconBar->addAction(a[QStringLiteral("customizeIconBar")]);
    Q_UNUSED(n);
}

void MainWindow::chooseIconBar()
{
    IconBarDialog dlg(m_project.get(), iconBarIcons(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    AppSettings &s = AppSettings::instance();
    s.iconBar = dlg.icons();
    s.iconBarSet = true;
    s.save();
    rebuildIconBar();
    a[QStringLiteral("showIconBar")]->setChecked(true);
}

// ------------------------------------------------------------------ color buttons

void MainWindow::createColorButtons()
{
    AppSettings &s = AppSettings::instance();
    m_textColorBtn = new ColorPickButton(ColorPickButton::TextColor);
    m_textColorBtn->setColor(s.barTextColor);
    m_highlightBtn = new ColorPickButton(ColorPickButton::Highlight);
    m_highlightBtn->setColor(s.barHighlightColor);
    for (ColorPickButton *b : {m_textColorBtn, m_highlightBtn}) {
        auto *wa = new QWidgetAction(this);
        wa->setDefaultWidget(b);
        // they take the places of the commands of the same name in the format bar
        m_barItems.insert(b->kind() == ColorPickButton::TextColor ? QStringLiteral("textColor") : QStringLiteral("highlight"), wa);
        m_barButtons << b;
        connect(b, &ColorPickButton::applyColor, this, [this, b](const QColor &c) {
            AppSettings &st = AppSettings::instance();
            if (b->kind() == ColorPickButton::TextColor)
                st.barTextColor = c;
            else
                st.barHighlightColor = c;
            st.save();
            if (ObjectTextEdit *o = objectTextTarget()) {
                // the text of an object (0.21.1)
                if (b->kind() == ColorPickButton::TextColor)
                    o->applyTextColor(c);
                else
                    o->applyHighlight(c);
                o->setFocus();
                updateFormatBar();
                return;
            }
            TextEditor *e = editor();
            if (!e)
                return;
            if (b->kind() == ColorPickButton::TextColor)
                e->applyTextColor(c);
            else
                e->applyHighlight(c);
            e->setFocus();
            updateFormatBar();
        });
    }
}

// ------------------------------------------------------------------ help

void MainWindow::showDeveloperGuide()
{
    static QPointer<QDialog> win;
    if (!win) {
        win = new QDialog(this);
        win->setAttribute(Qt::WA_DeleteOnClose);
        win->setWindowTitle(tr("Developer Guide: Updates and Add-ons"));
        win->resize(860, 780);
        auto *l = new QVBoxLayout(win);
        auto *tb = new QTextBrowser(win);
        tb->setObjectName(QStringLiteral("developerGuide"));
        tb->setOpenExternalLinks(true);
        QFile f(QStringLiteral(":/docs/developer-guide.md"));
        f.open(QIODevice::ReadOnly);
        QString text = QString::fromUtf8(f.readAll());
        text.replace(QLatin1String("{{APP}}"), QLatin1String(QB_APP_NAME))
            .replace(QLatin1String("{{VERSION}}"), QLatin1String(QB_APP_VERSION))
            .replace(QLatin1String("{{RUNTIME}}"), QLatin1String(QB_RUNTIME_ID));
        tb->setMarkdown(text);
        l->addWidget(tb);
    }
    win->show();
    win->raise();
}

// ------------------------------------------------------------------ compare and transfer (0.18)

void MainWindow::chooseCompareFile()
{
    if (!m_project)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Compare with File"), AppSettings::instance().lastProjectDir,
        tr("Word and LibreOffice files (*.docx *.odt);;Other texts, read only (*.doc *.rtf *.txt *.md);;All files (*)"));
    if (!path.isEmpty())
        compareWithFile(path);
}

bool MainWindow::compareWithFile(const QString &path)
{
    if (!m_project)
        return false;
    compare::Result r;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = compare::compareWithFile(m_project.get(), path, &r, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Compare and Transfer"), tr("The file cannot be read.\n%1").arg(err));
        return false;
    }
    if (r.related < 65) {
        QMessageBox box(QMessageBox::Question, tr("Compare and Transfer"),
                        tr("Only %1 % of the text of \"%2\" is in what Compile makes of this project; 65 % are needed to "
                           "call them related.\n\nThe file may belong to another project, or Compile leaves out what the file "
                           "has (documents with Include in Compile switched off, another compile group).")
                            .arg(r.related)
                            .arg(QFileInfo(path).fileName()),
                        QMessageBox::Cancel, this);
        box.setObjectName(QStringLiteral("compareNotRelated"));
        QPushButton *any = box.addButton(tr("Compare Anyway"), QMessageBox::AcceptRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != any)
            return false;
    }
    CompareDialog dlg(m_project.get(), r, this);
    connect(&dlg, &CompareDialog::transferred, this, [this, &dlg](const compare::Report &rep) {
        QString text;
        if (!rep.writtenFile.isEmpty()) {
            text = tr("Written: %1\n\n%n change(s) are marked in it. Open it in Word or LibreOffice to accept or reject "
                      "each of them. The file you compared is unchanged.",
                      nullptr, rep.places)
                       .arg(QDir::toNativeSeparators(rep.writtenFile));
            if (dlg.result().fileKind != QLatin1String("docx"))
                text = tr("Written: %1\n\n%n change(s) are made in it (LibreOffice files get the changes directly, without "
                          "change marks). The file you compared is unchanged.",
                          nullptr, rep.places)
                           .arg(QDir::toNativeSeparators(rep.writtenFile));
        } else {
            text = tr("Taken into the project: %n place(s)", nullptr, rep.places) + QStringLiteral(" (")
                   + tr("%n document(s) changed", nullptr, rep.documentsChanged) + QStringLiteral(", ")
                   + tr("%n new", nullptr, rep.documentsAdded) + QStringLiteral(", ")
                   + tr("%n moved to the Trash", nullptr, rep.documentsTrashed) + QStringLiteral(").\n\n")
                   + tr("Every changed document has a snapshot from before (Documents > Snapshots).");
        }
        if (!rep.warnings.isEmpty())
            text += QStringLiteral("\n\n") + rep.warnings.join(QLatin1Char('\n'));
        QMessageBox box(QMessageBox::Information, tr("Compare and Transfer"), text, QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("compareReport"));
        QPushButton *open = nullptr;
        if (!rep.writtenFile.isEmpty())
            open = box.addButton(tr("Open the File"), QMessageBox::ActionRole);
        box.exec();
        if (open && box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl::fromLocalFile(rep.writtenFile));
        refreshInspector();
    });
    dlg.exec();
    return true;
}

} // namespace qb

