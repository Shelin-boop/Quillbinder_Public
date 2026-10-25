// Menus, toolbars, window layouts and the lists of commands that change
// with the project (styles) or the data folder (layouts).
#include "ui/MainWindow.h"
#include <QMimeData>
#include "ui/QuickRef.h"
#include "ui/DragDrop.h"
#include "AppInfo.h"
#include "core/BinderModel.h"
#include "core/ItemIcons.h"
#include "core/Search.h"
#include "text/Styles.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Commands.h"
#include "ui/Corkboard.h"
#include "ui/Inspector.h"
#include "ui/Layouts.h"
#include "ui/Outliner.h"
#include "ui/TargetLineEdit.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/ThemeDialogs.h"
#include "ui/ToolbarConfig.h"
#include "ui/Windows.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSet>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <cmath>
#include <functional>

namespace qb {

// ------------------------------------------------------------------ menus

namespace {

// Position of the letter after a single '&' (Alt+letter in a menu), or -1
int mnemonicPos(const QString &t)
{
    for (int i = 0; i + 1 < t.size(); ++i)
        if (t.at(i) == QLatin1Char('&')) {
            if (t.at(i + 1) == QLatin1Char('&')) {
                ++i;
                continue;
            }
            return i + 1;
        }
    return -1;
}

QString unescapeMnemonic(const QString &t)
{
    QString out;
    for (int i = 0; i < t.size(); ++i) {
        if (t.at(i) == QLatin1Char('&')) {
            if (i + 1 < t.size() && t.at(i + 1) == QLatin1Char('&'))
                ++i;
            else
                continue;
        }
        out += t.at(i);
    }
    return out;
}

// Two entries of one menu with the same Alt letter only select each other in
// turn; the later entry gets a free letter of its text (a word start first).
// Entries without a letter (names of documents, styles, files) stay as they are.
void fixMnemonics(QMenu *m)
{
    QSet<QChar> used;
    QList<QAction *> clashing;
    for (QAction *a : m->actions()) {
        if (a->isSeparator() || !a->isVisible())
            continue;
        const QString text = a->text();
        const int tab = int(text.indexOf(QLatin1Char('\t')));
        const QString main = tab < 0 ? text : text.left(tab);
        const int p = mnemonicPos(main);
        if (p < 0)
            continue;
        const QChar c = main.at(p).toLower();
        if (used.contains(c))
            clashing << a;
        else
            used.insert(c);
    }
    for (QAction *a : clashing) {
        const QString text = a->text();
        const int tab = int(text.indexOf(QLatin1Char('\t')));
        const QString main = tab < 0 ? text : text.left(tab);
        const QString rest = tab < 0 ? QString() : text.mid(tab);
        const QString label = unescapeMnemonic(main);
        int best = -1;
        for (int pass = 0; pass < 2 && best < 0; ++pass)
            for (int i = 0; i < label.size(); ++i) {
                const QChar c = label.at(i);
                if (!c.isLetterOrNumber() || c.unicode() > 127 || used.contains(c.toLower()))
                    continue;
                if (pass == 0 && i > 0 && label.at(i - 1).isLetterOrNumber())
                    continue;
                best = i;
                break;
            }
        QString fixed;
        for (int i = 0; i < label.size(); ++i) {
            if (i == best)
                fixed += QLatin1Char('&');
            fixed += label.at(i) == QLatin1Char('&') ? QStringLiteral("&&") : QString(label.at(i));
        }
        if (best >= 0)
            used.insert(label.at(best).toLower());
        fixed += rest;
        if (fixed != text)
            a->setText(fixed);
    }
}

// Every menu (and its sub-menus, also those made when it opens) is checked
// each time it opens, after the entries of the moment are in.
void watchMnemonics(QMenu *m)
{
    if (!m || m->property("qbMnemonics").toBool())
        return;
    m->setProperty("qbMnemonics", true);
    auto run = [m] {
        fixMnemonics(m);
        for (QAction *a : m->actions())
            if (a->menu())
                watchMnemonics(a->menu());
    };
    QObject::connect(m, &QMenu::aboutToShow, m, run);
    // lists filled by a later aboutToShow handler of their own
    QObject::connect(m, &QMenu::aboutToShow, m, [m] { QTimer::singleShot(0, m, [m] { fixMnemonics(m); }); });
    run();
}

} // namespace

QAction *MainWindow::menuAlias(QMenu *menu, const QString &key, const QString &text)
{
    QAction *original = a.value(key);
    auto *alias = new QAction(menu);
    alias->setProperty("qbAliasOf", QVariant::fromValue<QObject *>(original));
    auto sync = [alias, original, text] {
        const QString shortcut = original->shortcut().toString(QKeySequence::NativeText);
        alias->setText(shortcut.isEmpty() ? text : text + QLatin1Char('\t') + shortcut);
        alias->setIcon(original->icon());
        alias->setEnabled(original->isEnabled());
        alias->setVisible(original->isVisible());
        alias->setCheckable(original->isCheckable());
        alias->setChecked(original->isChecked());
    };
    sync();
    connect(original, &QAction::changed, alias, sync);
    connect(alias, &QAction::triggered, original, &QAction::trigger);
    menu->addAction(alias);
    return alias;
}

void MainWindow::createMenus()
{
    QMenuBar *mb = menuBar();
    // "" = separator
    auto add = [this](QMenu *m, std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            if (!*k)
                m->addSeparator();
            else
                m->addAction(a[QLatin1String(k)]);
        }
    };

    // ---- File
    QMenu *file = mb->addMenu(tr("&File"));
    add(file, {"newProject", "openProject"});
    m_recentMenu = file->addMenu(icons::action(QStringLiteral("document-open-recent")), tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    add(file, {"closeProject", "", "save", "saveTemplate"});
    QMenu *projectFormat = file->addMenu(tr("Project &Format"));
    projectFormat->setObjectName(QStringLiteral("projectFormatMenu"));
    add(projectFormat, {"formatQuillbinder", "formatScrivener", "", "saveCopyScrivener", "saveCopyQuillbinder"});
    QMenu *bk = file->addMenu(tr("&Back Up"));
    add(bk, {"backupNow", "backupTo", "showBackups"});
    file->addSeparator();
    QMenu *imp = file->addMenu(icons::action(QStringLiteral("document-import")), tr("&Import"));
    menuAlias(imp, QStringLiteral("importFiles"), tr("&Files…"));
    menuAlias(imp, QStringLiteral("importWebPage"), tr("&Web Page…"));
    add(imp, {"importSplit"});
    menuAlias(imp, QStringLiteral("importScrivener"), tr("&Scrivener Project…"));
    QMenu *exp = file->addMenu(icons::action(QStringLiteral("document-export")), tr("&Export"));
    menuAlias(exp, QStringLiteral("exportFiles"), tr("&Files…"));
    menuAlias(exp, QStringLiteral("saveCopyScrivener"), tr("&Scrivener Project…"));
    QMenu *cmp = file->addMenu(tr("Compare and &Transfer"));
    cmp->setObjectName(QStringLiteral("compareMenu"));
    add(cmp, {"compareFile", "", "compareByChapter", "compareStepByStep"});
    add(file, {"compile", "", "printSettings", "printPreview", "print", "", "showProjectFolder", "convertFile", "", "installPackage",
               "options", "", "quit"});

    // ---- Edit
    QMenu *edit = mb->addMenu(tr("&Edit"));
    add(edit, {"undo", "redo", "", "cut", "copy", "paste", "pastePlain", "", "selectAll"});
    QMenu *select = edit->addMenu(tr("&Select"));
    add(select, {"selectAnnotation"});
    edit->addSeparator();
    QMenu *find = edit->addMenu(icons::action(QStringLiteral("edit-find")), tr("&Find"));
    add(find, {"find", "replace", "findNext", "findPrev", "useSelectionForFind", "", "projectSearch", "projectReplace", "",
               "findFormat", "findFormatNext", "findFormatPrev"});
    QMenu *move = edit->addMenu(tr("&Move"));
    add(move, {"moveUp", "moveDown", "moveLeft", "moveRight"});
    // commands of the writing tools (add-ons); the menu is there without them as well
    QMenu *writing = edit->addMenu(icons::action(QStringLiteral("tools-wizard")), tr("&Writing Tools"));
    writing->setObjectName(QStringLiteral("writingToolsMenu"));
    fillWritingToolsMenu(writing);
    add(edit, {"", "docLink", "objectLink", "hyperlink"});

    // ---- Insert
    QMenu *insert = mb->addMenu(tr("&Insert"));
    insert->setProperty("qbNeedsProject", true);
    add(insert, {"image", "", "comment", "footnote", "annotation", "inlineFootnote", ""});
    menuAlias(insert, QStringLiteral("mediaTimestamp"), tr("Media &Timestamp"));
    insert->addSeparator();
    QMenu *autoNum = insert->addMenu(tr("Auto &Number"));
    const QList<QPair<QString, QString>> numbers = {{tr("1, 2, 3 …"), QStringLiteral("<$n>")},
                                                    {tr("one, two, three …"), QStringLiteral("<$w>")},
                                                    {tr("One, Two, Three …"), QStringLiteral("<$W>")},
                                                    {tr("I, II, III …"), QStringLiteral("<$R>")},
                                                    {tr("i, ii, iii …"), QStringLiteral("<$r>")},
                                                    {tr("a, b, c …"), QStringLiteral("<$l>")},
                                                    {tr("A, B, C …"), QStringLiteral("<$L>")}};
    for (const auto &n : numbers) {
        const QString tag = n.second;
        autoNum->addAction(n.first + QStringLiteral("   ") + tag, this, [this, tag] {
            if (TextEditor *e = editor())
                e->insertPlainText(tag);
        });
    }
    QMenu *ph = insert->addMenu(tr("&Placeholders"));
    const QList<QPair<QString, QString>> placeholders = {{tr("Project title"), QStringLiteral("<$projecttitle>")},
                                                         {tr("Author"), QStringLiteral("<$author>")},
                                                         {tr("Author's last name"), QStringLiteral("<$surname>")},
                                                         {tr("Word count (rounded to 100)"), QStringLiteral("<$wc100>")},
                                                         {tr("Compile date"), QStringLiteral("<$date>")}};
    for (const auto &n : placeholders) {
        const QString tag = n.second;
        ph->addAction(n.first + QStringLiteral("   ") + tag, this, [this, tag] {
            if (TextEditor *e = editor())
                e->insertPlainText(tag);
        });
    }
    QMenu *chars = insert->addMenu(tr("&Special Characters"));
    for (const auto &c : {qMakePair(tr("Em dash —"), QString(QChar(0x2014))), qMakePair(tr("En dash –"), QString(QChar(0x2013))),
                          qMakePair(tr("Ellipsis …"), QString(QChar(0x2026))), qMakePair(tr("Non-breaking space"), QString(QChar(0x00A0))),
                          qMakePair(tr("Section sign §"), QString(QChar(0x00A7))), qMakePair(tr("Bullet •"), QString(QChar(0x2022)))}) {
        const QString s = c.second;
        chars->addAction(c.first, this, [this, s] {
            if (TextEditor *e = editor())
                e->insertPlainText(s);
        });
    }
    add(insert, {"", "insertDate", "insertTime"});

    // ---- View
    QMenu *view = mb->addMenu(tr("&View"));
    add(view, {"viewText", "viewCork", "viewOutline", ""});
    QMenu *layout = view->addMenu(tr("Editor &Layout"));
    add(layout, {"noSplit", "splitH", "splitV"});
    add(view, {"", "showBinder", "showCollections", "showInspector", ""});
    QMenu *bars = view->addMenu(tr("Tool&bars"));
    add(bars, {"showToolbar", "showFormatBar", "showIconBar", "", "customizeToolbars", "customizeIconBar"});
    QMenu *te = view->addMenu(tr("&Text Editing"));
    add(te, {"pageView", "pageSetup", "", "showRuler", "typewriter", "scrivTitles", "fixedWidth"});
    QMenu *cork = view->addMenu(tr("&Corkboard Options"));
    add(cork, {"stamps", "", "corkFreeform", "corkLabels", "corkCommit", "", "selectionAffectsOther", "selectionAffectsCopyholder"});
    QMenu *outl = view->addMenu(tr("&Outliner Options"));
    outl->setObjectName(QStringLiteral("outlinerOptionsMenu"));
    auto fillOutlinerMenu = [this, outl] {
        outl->clear();
        outl->addAction(a[QStringLiteral("outlinerSynopsis")]);
        outl->addAction(a[QStringLiteral("selectionAffectsOther")]);
        outl->addAction(a[QStringLiteral("selectionAffectsCopyholder")]);
        outl->addSeparator();
        if (m_project && m_active && m_active->mode() == ViewMode::Outliner)
            m_active->outliner()->fillColumnMenu(outl, false);
        else
            outl->addAction(tr("The columns are listed here while an outliner is shown"))->setEnabled(false);
    };
    fillOutlinerMenu();
    connect(outl, &QMenu::aboutToShow, this, fillOutlinerMenu);
    QMenu *labelIn = view->addMenu(tr("Use Label Color &In"));
    menuAlias(labelIn, QStringLiteral("labelBinder"), tr("&Binder"));
    menuAlias(labelIn, QStringLiteral("labelIcons"), tr("I&cons"));
    menuAlias(labelIn, QStringLiteral("tintCards"), tr("&Index Cards"));
    add(view, {"", "zoomIn", "zoomOut", "zoomReset", "zoomInspectorPerTab", "", "composition", "fullScreen"});

    // ---- Navigate
    QMenu *nav = mb->addMenu(tr("&Navigate"));
    nav->setProperty("qbNeedsProject", true);
    add(nav, {"back", "forward"});
    // the documents the active editor has shown; the nearest steps first
    for (bool back : {true, false}) {
        QMenu *hist = nav->addMenu(back ? tr("Back &To") : tr("For&ward To"));
        hist->setObjectName(back ? QStringLiteral("historyBackMenu") : QStringLiteral("historyForwardMenu"));
        hist->addAction(QString()); // placeholder so the empty menu can open
        connect(hist, &QMenu::aboutToShow, this, [this, hist, back] {
            if (m_active && m_project)
                m_active->fillHistoryMenu(hist, back);
            else {
                hist->clear();
                hist->addAction(tr("No project is open"))->setEnabled(false);
            }
        });
    }
    add(nav, {"", "prevDoc", "nextDoc", "enclosingGroup"});
    QMenu *goTo = nav->addMenu(icons::action(QStringLiteral("go-jump")), tr("&Go To"));
    goTo->setObjectName(QStringLiteral("goToMenu"));
    goTo->addAction(QString()); // placeholder so the empty menu can open
    connect(goTo, &QMenu::aboutToShow, this, [this, goTo] { fillGoToMenu(goTo); });
    nav->addSeparator();
    QMenu *open = nav->addMenu(tr("&Open"));
    menuAlias(open, QStringLiteral("openInEditor"), tr("in &Editor"));
    menuAlias(open, QStringLiteral("openOther"), tr("in &Other Editor"));
    menuAlias(open, QStringLiteral("openCopyholder"), tr("in &Copyholder"));
    menuAlias(open, QStringLiteral("quickRef"), tr("as &Quick Reference"));
    QMenu *openQr = nav->addMenu(icons::action(QStringLiteral("window-new")), tr("Open &Quick Reference"));
    openQr->setObjectName(QStringLiteral("openQuickRefMenu"));
    openQr->addAction(QString()); // placeholder so the empty menu can open
    connect(openQr, &QMenu::aboutToShow, this, [this, openQr] {
        openQr->clear();
        if (!m_project) {
            openQr->addAction(tr("No project is open"))->setEnabled(false);
            return;
        }
        // the binder as nested menus; a group opens itself through its first entry
        std::function<void(QMenu *, BinderItem *)> fill = [&](QMenu *menu, BinderItem *parent) {
            for (BinderItem *it : parent->children) {
                if (it == m_project->trash())
                    continue;
                const QString title = QString(it->title).replace(QLatin1Char('&'), QLatin1String("&&"));
                if (it->children.isEmpty()) {
                    menu->addAction(icons::forItem(m_project.get(), it), title, this, [this, it] { openQuickReference(it); });
                } else {
                    QMenu *sub = menu->addMenu(icons::forItem(m_project.get(), it), title);
                    if (!it->isRootFolder()) {
                        sub->addAction(icons::forItem(m_project.get(), it), title, this, [this, it] { openQuickReference(it); });
                        sub->addSeparator();
                    }
                    fill(sub, it);
                }
            }
        };
        fill(openQr, m_project->root());
    });
    add(nav, {"reveal", ""});
    createMediaMenu(nav);
    QMenu *ed = nav->addMenu(tr("&Editor"));
    add(ed, {"lock", "", "closeCopyholder", "", "focusEditor", "switchEditor"});
    QMenu *affects = nav->addMenu(tr("&Binder Selection Affects"));
    for (QAction *ac : m_affectsGroup->actions())
        affects->addAction(ac);
    QMenu *inspect = nav->addMenu(tr("&Inspect"));
    add(inspect, {"inspect0", "inspect1", "inspect5", "inspect6", "inspect2", "inspect3", "inspect4"});
    add(nav, {"", "focusBinder", ""});
    createFontMenu(nav);

    // ---- Project
    QMenu *proj = mb->addMenu(tr("&Project"));
    proj->setProperty("qbNeedsProject", true);
    add(proj, {"newText", "newFolder"});
    m_templateMenu = proj->addMenu(icons::action(QStringLiteral("folder-templates")), tr("New From &Template"));
    connect(m_templateMenu, &QMenu::aboutToShow, this, [this] {
        m_templateMenu->clear();
        BinderItem *tf = m_project ? m_project->item(m_project->templatesFolderId) : nullptr;
        if (!tf || tf->children.isEmpty()) {
            m_templateMenu->addAction(tr("No templates folder set for this project"))->setEnabled(false);
            m_templateMenu->addAction(tr("Set Up in Project Settings…"), this, [this] {
                if (!m_project)
                    return;
                ProjectSettingsDialog dlg(m_project.get(), ProjectSettingsDialog::SpecialFolders, this);
                dlg.exec();
            });
            return;
        }
        for (BinderItem *t : tf->children) {
            const QString id = t->id;
            m_templateMenu->addAction(icons::forItem(m_project.get(), t), t->title, this,
                                      [this, id] { newItem(ItemType::Text, id); });
        }
    });
    add(proj, {"", "projectSettings", "targets", "keywords", "objectTemplates", "statistics"});
    for (QAction *ac : toolActionsAt(QStringLiteral("project")))
        proj->addAction(ac);
    add(proj, {"history", "", "emptyTrash"});

    // ---- Documents
    QMenu *docs = mb->addMenu(tr("&Documents"));
    docs->setProperty("qbNeedsProject", true);
    QMenu *snaps = docs->addMenu(icons::action(QStringLiteral("camera-photo")), tr("S&napshots"));
    add(snaps, {"snapshot", "snapshotTitle", "showSnapshots"});
    add(docs, {"", "rename", "duplicate", "trash", ""});
    QMenu *splitMenu = docs->addMenu(tr("S&plit"));
    menuAlias(splitMenu, QStringLiteral("split"), tr("at &Selection"));
    menuAlias(splitMenu, QStringLiteral("splitTitle"), tr("with Selection as &Title"));
    menuAlias(docs, QStringLiteral("merge"), tr("&Merge"));
    QMenu *convert = docs->addMenu(tr("Con&vert"));
    add(convert, {"toFolder", "toText", "", "defaultFormatting"});
    docs->addSeparator();
    m_iconMenu = docs->addMenu(icons::action(QStringLiteral("preferences-desktop-icons")), tr("Change &Icon"));
    m_defaultTemplateMenu = docs->addMenu(tr("Default &Template for Subdocuments"));
    m_labelMenu = docs->addMenu(tr("&Label"));
    m_statusMenu = docs->addMenu(tr("&Status"));
    add(docs, {"includeCompile", "bookmark"});
    auto fillLabelStatus = [this] {
        m_labelMenu->clear();
        m_statusMenu->clear();
        if (!m_project)
            return;
        m_labelMenu->setTitle(m_project->labelTitle);
        m_statusMenu->setTitle(m_project->statusTitle);
        auto addLabel = [this](int id, const QString &name, const QColor &c) {
            QAction *ac = m_labelMenu->addAction(name);
            if (c.isValid()) {
                QPixmap pm(12, 12);
                pm.fill(c);
                ac->setIcon(QIcon(pm));
            }
            connect(ac, &QAction::triggered, this, [this, id] {
                for (BinderItem *it : actionItems()) {
                    it->label = id;
                    m_project->notifyItemChanged(it, false);
                }
            });
        };
        addLabel(-1, tr("No %1").arg(m_project->labelTitle), QColor());
        for (const Label &l : m_project->labels)
            addLabel(l.id, l.name, l.color);
        m_statusMenu->addAction(tr("No %1").arg(m_project->statusTitle), this, [this] {
            for (BinderItem *it : actionItems()) {
                it->status = -1;
                m_project->notifyItemChanged(it, false);
            }
        });
        for (const Status &s : m_project->statuses) {
            const int id = s.id;
            m_statusMenu->addAction(s.name, this, [this, id] {
                for (BinderItem *it : actionItems()) {
                    it->status = id;
                    m_project->notifyItemChanged(it, false);
                }
            });
        }
    };
    connect(m_labelMenu, &QMenu::aboutToShow, this, fillLabelStatus);
    connect(m_statusMenu, &QMenu::aboutToShow, this, fillLabelStatus);
    fillIconMenu(m_iconMenu);
    connect(m_iconMenu, &QMenu::aboutToShow, this, [this] { fillIconMenu(m_iconMenu); });
    connect(m_defaultTemplateMenu, &QMenu::aboutToShow, this, [this] {
        m_defaultTemplateMenu->clear();
        m_defaultTemplateMenu->addAction(tr("None"), this, [this] {
            for (BinderItem *it : actionItems()) {
                it->defaultTemplate.clear();
                m_project->markIndexDirty();
            }
        });
        BinderItem *tf = m_project ? m_project->item(m_project->templatesFolderId) : nullptr;
        if (!tf)
            return;
        for (BinderItem *t : tf->children) {
            const QString id = t->id;
            m_defaultTemplateMenu->addAction(t->title, this, [this, id] {
                for (BinderItem *it : actionItems()) {
                    it->defaultTemplate = id;
                    m_project->markIndexDirty();
                }
            });
        }
    });

    // ---- Format
    QMenu *format = mb->addMenu(tr("F&ormat"));
    format->setProperty("qbNeedsProject", true);
    QMenu *fontMenu = format->addMenu(icons::action(QStringLiteral("font")), tr("&Font"));
    add(fontMenu, {"showFonts", "", "bold", "italic", "underline", "strike", "superscript", "subscript", "", "fontBigger",
                   "fontSmaller", "", "textColor", "textColorAuto"});
    QMenu *paraMenu = format->addMenu(tr("&Paragraph"));
    add(paraMenu, {"paragraph", "", "alignLeft", "alignCenter", "alignRight", "alignJustify", ""});
    QMenu *lineMenu = paraMenu->addMenu(tr("&Line Spacing"));
    add(lineMenu, {"line100", "line115", "line120", "line150", "line200", ""});
    lineMenu->addAction(tr("Other…"), this, [this] { a[QStringLiteral("paragraph")]->trigger(); });
    add(paraMenu, {"indentMore", "indentLess", "", "resetParagraph"});
    m_styleMenu = format->addMenu(tr("&Style"));
    m_styleMenu->setObjectName(QStringLiteral("styleMenu"));
    fillStyleMenu(m_styleMenu, nullptr, true);
    connect(m_styleMenu, &QMenu::aboutToShow, this, [this] { fillStyleMenu(m_styleMenu, nullptr, true); });
    QMenu *lists = format->addMenu(tr("&Lists"));
    add(lists, {"bullets", "numbers", "indent", "outdent"});
    add(format, {"", "highlight", "", "copyFormat", "pasteFormat", "clearFormat", ""});
    QMenu *rev = format->addMenu(tr("&Revision Mode"));
    for (int i = 0; i <= 5; ++i)
        rev->addAction(a[QStringLiteral("rev%1").arg(i)]);
    createScriptMenu(format);
    add(format, {"", "citations", "", "defaultTextFormatting"});

    // ---- Window
    QMenu *window = mb->addMenu(tr("&Window"));
    window->setObjectName(QStringLiteral("windowMenu"));
    QMenu *themes = window->addMenu(tr("&Themes"));
    themes->setObjectName(QStringLiteral("themeMenu"));
    // filled when opened: add-ons and the theme list can add and remove themes at any time
    auto fillThemes = [this, themes] {
        themes->clear();
        theme::Kind last = theme::Kind::BuiltIn;
        for (const theme::Entry &entry : theme::available()) {
            if (entry.kind != last)
                themes->addSeparator();
            last = entry.kind;
            QAction *ta = themes->addAction(entry.name);
            ta->setCheckable(true);
            ta->setChecked(AppSettings::instance().theme == entry.id);
            const QString id = entry.id;
            connect(ta, &QAction::triggered, this, [this, id] { themeui::choose(this, id); });
        }
        themes->addSeparator();
        for (const char *k : {"themeSave", "themeImport", "themeManage", "themeCustomize"})
            themes->addAction(a[QLatin1String(k)]);
    };
    fillThemes();
    connect(themes, &QMenu::aboutToShow, this, fillThemes);
    m_layoutMenu = window->addMenu(icons::action(QStringLiteral("view-split-left-right")), tr("&Layouts"));
    m_layoutMenu->setObjectName(QStringLiteral("layoutMenu"));
    add(m_layoutMenu, {"layoutSave", "layoutManage"}); // rebuilt with the list of layouts
    const QList<QAction *> windowTools = toolActionsAt(QStringLiteral("window"));
    if (!windowTools.isEmpty()) {
        window->addSeparator();
        for (QAction *ac : windowTools)
            window->addAction(ac);
    }
    add(window, {"", "objectsWindow", "", "floatQuickRef", "closeQuickRefs"});
    // open Quick Reference windows at the end of the menu
    connect(window, &QMenu::aboutToShow, this, [this, window] {
        for (QAction *old : window->findChildren<QAction *>(QStringLiteral("qbWindowEntry"), Qt::FindDirectChildrenOnly))
            delete old;
        bool first = true;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *qr = qobject_cast<QuickRefWindow *>(w);
            if (!qr || !qr->isVisible())
                continue;
            if (first) {
                auto *sep = new QAction(window);
                sep->setSeparator(true);
                sep->setObjectName(QStringLiteral("qbWindowEntry"));
                window->addAction(sep);
                first = false;
            }
            auto *entry = new QAction(icons::action(QStringLiteral("window")), qr->windowTitle().replace(QLatin1Char('&'), QLatin1String("&&")), window);
            entry->setObjectName(QStringLiteral("qbWindowEntry"));
            connect(entry, &QAction::triggered, qr, [qr] {
                qr->showNormal();
                qr->raise();
                qr->activateWindow();
            });
            window->addAction(entry);
        }
    });

    // ---- Help
    QMenu *help = mb->addMenu(tr("&Help"));
    add(help, {"manual", "quickStart", "welcome", "placeholders", "keyboardShortcuts", "", "addonManager", "pluginFolder",
               "integrate", "", "mindMapHelp", "developerGuide", "", "systemInfo", "crashReports", "", "licenses", "about"});

    // binder footer menus
    m_addMenu = new QMenu(this);
    add(m_addMenu, {"newText", "newFolder"});
    m_addMenu->addMenu(m_templateMenu);
    add(m_addMenu, {"", "importFiles"});
    m_binderGearMenu = new QMenu(this);
    connect(m_binderGearMenu, &QMenu::aboutToShow, this, [this] {
        m_binderGearMenu->clear();
        fillItemMenu(m_binderGearMenu);
    });
    m_binder->setFooterMenus(m_addMenu, m_binderGearMenu);
    for (QAction *top : mb->actions())
        watchMnemonics(top->menu());
}

// ------------------------------------------------------------------ toolbars

namespace {

QToolButton *barButton(const QString &name)
{
    auto *b = new QToolButton;
    b->setObjectName(name);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

void MainWindow::createToolBars()
{
    m_toolbar = addToolBar(tr("Main Toolbar"));
    m_toolbar->setObjectName(QStringLiteral("mainToolbar"));
    m_toolbar->setMovable(false);
    connect(a[QStringLiteral("showToolbar")], &QAction::toggled, m_toolbar, &QWidget::setVisible);

    // The controls are made once and live in widget actions of the window, so
    // a bar can be rebuilt (or do without one of them) and they keep their state.
    auto wrap = [this](const QString &id, QWidget *w) {
        auto *wa = new QWidgetAction(this);
        wa->setDefaultWidget(w);
        m_barItems.insert(id, wa);
        if (auto *b = qobject_cast<QToolButton *>(w))
            m_barButtons << b;
    };

    // View: the parts of the window and the layouts
    auto *viewBtn = barButton(QStringLiteral("viewButton"));
    viewBtn->setIcon(icons::action(QStringLiteral("view-sidetree")));
    viewBtn->setText(tr("View"));
    viewBtn->setToolTip(tr("Show or hide the binder, the collections and the inspector; layouts"));
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    auto *viewMenu = new QMenu(viewBtn);
    for (const char *k : {"showBinder", "showCollections", "showInspector"})
        viewMenu->addAction(a[QLatin1String(k)]);
    viewMenu->addSeparator();
    viewMenu->addMenu(m_layoutMenu);
    viewMenu->addSeparator();
    viewMenu->addAction(a[QStringLiteral("pageView")]);
    viewMenu->addSeparator();
    for (const char *k : {"showFormatBar", "showRuler", "customizeToolbars"})
        viewMenu->addAction(a[QLatin1String(k)]);
    viewBtn->setMenu(viewMenu);
    wrap(QStringLiteral("view"), viewBtn);

    auto *addBtn = barButton(QStringLiteral("addButton"));
    addBtn->setDefaultAction(a[QStringLiteral("newText")]);
    addBtn->setMenu(m_addMenu);
    addBtn->setPopupMode(QToolButton::MenuButtonPopup);
    wrap(QStringLiteral("add"), addBtn);

    auto *searchBtn = barButton(QStringLiteral("searchButton"));
    searchBtn->setDefaultAction(a[QStringLiteral("projectSearch")]);
    auto *searchMenu = new QMenu(searchBtn);
    searchMenu->addAction(a[QStringLiteral("projectSearch")]);
    menuAlias(searchMenu, QStringLiteral("find"), tr("Search in &Document…"));
    searchMenu->addSeparator();
    for (const char *k : {"projectReplace", "replace", "findFormat"})
        searchMenu->addAction(a[QLatin1String(k)]);
    searchMenu->addSeparator();
    for (const char *k : {"findNext", "findPrev"})
        searchMenu->addAction(a[QLatin1String(k)]);
    searchBtn->setMenu(searchMenu);
    searchBtn->setPopupMode(QToolButton::MenuButtonPopup);
    wrap(QStringLiteral("searchMenu"), searchBtn);

    m_quickSearch = new TargetLineEdit;
    m_quickSearch->setObjectName(QStringLiteral("quickSearch"));
    m_quickSearch->setPlaceholderText(tr("Quick search (titles, synopses, text)"));
    m_quickSearch->setClearButtonEnabled(true);
    m_quickSearch->setMinimumWidth(260);
    m_quickSearch->setMaximumWidth(380);
    wrap(QStringLiteral("search"), m_quickSearch);

    auto *bmBtn = barButton(QStringLiteral("bookmarksButton"));
    bmBtn->setIcon(icons::action(QStringLiteral("bookmarks")));
    bmBtn->setText(tr("Bookmarks"));
    bmBtn->setToolTip(tr("Project bookmarks"));
    bmBtn->setPopupMode(QToolButton::InstantPopup);
    m_bookmarkMenu = new QMenu(bmBtn);
    bmBtn->setMenu(m_bookmarkMenu);
    connect(m_bookmarkMenu, &QMenu::aboutToShow, this, [this] {
        m_bookmarkMenu->clear();
        if (!m_project)
            return;
        for (const Bookmark &b : m_project->projectBookmarks) {
            if (b.isInternal()) {
                BinderItem *it = m_project->item(b.internalId());
                if (!it)
                    continue;
                m_bookmarkMenu->addAction(icons::forItem(m_project.get(), it), it->title, this, [this, it] {
                    loadInPane(m_active, {it});
                });
            } else {
                const QString target = b.target;
                m_bookmarkMenu->addAction(b.title, this, [target] { QDesktopServices::openUrl(QUrl(target)); });
            }
        }
        if (m_project->projectBookmarks.isEmpty())
            m_bookmarkMenu->addAction(tr("No project bookmarks yet"))->setEnabled(false);
        m_bookmarkMenu->addSeparator();
        m_bookmarkMenu->addAction(a[QStringLiteral("bookmark")]);
    });
    wrap(QStringLiteral("bookmarks"), bmBtn);

    auto *noteBtn = barButton(QStringLiteral("notesButton"));
    noteBtn->setDefaultAction(a[QStringLiteral("comment")]);
    auto *noteMenu = new QMenu(noteBtn);
    for (const char *k : {"comment", "footnote", "annotation", "inlineFootnote"})
        noteMenu->addAction(a[QLatin1String(k)]);
    noteBtn->setMenu(noteMenu);
    noteBtn->setPopupMode(QToolButton::MenuButtonPopup);
    wrap(QStringLiteral("notes"), noteBtn);

    auto *layoutBtn = barButton(QStringLiteral("layoutsButton"));
    layoutBtn->setIcon(icons::action(QStringLiteral("view-split-left-right")));
    layoutBtn->setText(tr("Layouts"));
    layoutBtn->setToolTip(tr("Window layouts"));
    layoutBtn->setPopupMode(QToolButton::InstantPopup);
    layoutBtn->setMenu(m_layoutMenu);
    wrap(QStringLiteral("layoutsButton"), layoutBtn);

    // right-click on a bar: what is shown, and the editor for the buttons
    auto barMenu = [this](QToolBar *bar, int which) {
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar, which](const QPoint &pos) {
            QMenu menu;
            menu.addAction(a[QStringLiteral("showToolbar")]);
            menu.addAction(a[QStringLiteral("showFormatBar")]);
            menu.addSeparator();
            menu.addAction(a[QStringLiteral("customizeToolbars")]->icon(), tr("Customize This Toolbar…"), this,
                           [this, which] { customizeToolbars(which); });
            menu.exec(bar->mapToGlobal(pos));
        });
    };
    barMenu(m_toolbar, 0);

    // quick search completer
    m_completerModel = new QStandardItemModel(this);
    m_completer = new QCompleter(m_completerModel, this);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setMaxVisibleItems(22);
    m_quickSearch->setCompleter(m_completer);
    auto *qsTimer = new QTimer(this);
    qsTimer->setSingleShot(true);
    qsTimer->setInterval(200);
    connect(m_quickSearch, &QLineEdit::textEdited, qsTimer, qOverload<>(&QTimer::start));
    connect(qsTimer, &QTimer::timeout, this, &MainWindow::fillQuickSearch);
    connect(m_completer, qOverload<const QModelIndex &>(&QCompleter::activated), this, [this](const QModelIndex &idx) {
        if (!m_project)
            return;
        BinderItem *it = m_project->item(idx.data(Qt::UserRole + 1).toString());
        if (!it)
            return;
        // the words searched for are marked in the text and the first one is selected (0.14.1)
        const QString term = m_quickSearch->text().trimmed();
        loadInPane(m_active, {it});
        m_updatingUi = true;
        m_binder->select({it}, false);
        m_updatingUi = false;
        if (!term.isEmpty()) {
            m_quickSearchMark = true;
            m_active->setSearchHighlight(term);
        }
        QTimer::singleShot(0, this, [this, term] {
            m_quickSearch->clear();
            m_active->focusEditor();
            if (!term.isEmpty())
                m_active->revealFirstMatch();
        });
    });

    createColorButtons();

    // ---- format bar
    m_formatBar = addToolBar(tr("Format Bar"));
    m_formatBar->setObjectName(QStringLiteral("formatBar"));
    m_formatBar->setMovable(false);
    m_formatBar->setIconSize(QSize(18, 18));
    insertToolBarBreak(m_formatBar);
    barMenu(m_formatBar, 1);
    connect(a[QStringLiteral("showFormatBar")], &QAction::toggled, m_formatBar, &QWidget::setVisible);

    m_styleCombo = new QComboBox;
    m_styleCombo->setObjectName(QStringLiteral("styleCombo"));
    m_styleCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    rebuildStyleCombo();
    m_styleCombo->setToolTip(tr("Style of the paragraph or of the selected text"));
    m_styleCombo->setFocusPolicy(Qt::NoFocus);
    wrap(QStringLiteral("styleCombo"), m_styleCombo);
    connect(m_styleCombo, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (TextEditor *e = editor()) {
            e->applyStyle(m_styleCombo->itemData(i).toString());
            e->setFocus();
            updateFormatBar();
        }
    });
    m_fontCombo = new QFontComboBox;
    m_fontCombo->setMaximumWidth(200);
    m_fontCombo->setFocusPolicy(Qt::NoFocus);
    m_fontCombo->setToolTip(tr("Font of the selected text"));
    wrap(QStringLiteral("fontCombo"), m_fontCombo);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        if (m_updatingUi)
            return;
        if (TextEditor *e = editor()) {
            direct::CharSpec sp;
            sp.family = f.family();
            applyFontFromBar(sp, direct::Family);
            e->setFocus();
        }
    });
    m_sizeSpin = new QDoubleSpinBox;
    m_sizeSpin->setRange(6, 96);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setFocusPolicy(Qt::ClickFocus);
    m_sizeSpin->setToolTip(tr("Font size of the selected text in points"));
    // the arrows change the text at once, typed values with Enter
    m_sizeSpin->setKeyboardTracking(false);
    wrap(QStringLiteral("sizeSpin"), m_sizeSpin);
    connect(m_sizeSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updatingUi || !editor())
            return;
        if (qAbs(editor()->fontSizeAtCursor() - v) < 0.05 && !editor()->textCursor().hasSelection())
            return;
        direct::CharSpec sp;
        sp.size = v;
        applyFontFromBar(sp, direct::Size);
    });
    connect(m_sizeSpin, &QDoubleSpinBox::editingFinished, this, [this] {
        if (TextEditor *e = editor(); e && m_sizeSpin->hasFocus())
            e->setFocus(); // Enter goes back to the text
    });
    m_lineCombo = new QComboBox;
    m_lineCombo->setToolTip(tr("Line spacing of the paragraph"));
    m_lineCombo->setFocusPolicy(Qt::NoFocus);
    // 1.09: the writer's standard since 0.21.2 (shown as "Other…" before)
    for (double sp : {1.0, 1.09, 1.15, 1.2, 1.5, 2.0})
        m_lineCombo->addItem(QLocale().toString(sp, 'f', qRound(sp * 100) % 10 ? 2 : 1) + QStringLiteral("x"), sp);
    m_lineCombo->addItem(tr("Other…"), 0.0);
    wrap(QStringLiteral("lineCombo"), m_lineCombo);
    connect(m_lineCombo, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        TextEditor *e = editor();
        if (!e)
            return;
        const double sp = m_lineCombo->itemData(i).toDouble();
        if (sp > 0)
            e->setLineSpacing(sp);
        else
            showParagraphDialog(e);
        e->setFocus();
        updateFormatBar();
    });
}

void MainWindow::createFormatBar() {} // part of createToolBars() since 0.6

QAction *MainWindow::barItem(const QString &id) const
{
    if (QAction *w = m_barItems.value(id))
        return w;
    // commands only; the lists that come and go (styles, layouts) would leave holes
    if (id.contains(QLatin1Char(':')))
        return nullptr;
    return a.value(id);
}

void MainWindow::rebuildToolBars()
{
    if (!m_toolbar || !m_formatBar)
        return;
    const AppSettings &s = AppSettings::instance();
    const QStringList lists[2] = {toolbars::configured(toolbars::Bar::Main), toolbars::configured(toolbars::Bar::Format)};
    const QString signature = lists[0].join(QLatin1Char(',')) + QLatin1Char('#') + lists[1].join(QLatin1Char(','))
                              + QLatin1Char('#') + QString::number(s.toolbarIconSize) + s.toolbarStyle;
    if (signature == m_builtBars)
        return;
    m_builtBars = signature;

    QToolBar *bars[2] = {m_toolbar, m_formatBar};
    for (QToolBar *bar : bars)
        bar->clear();
    qDeleteAll(m_barOwned);
    m_barOwned.clear();
    QSet<QString> placed; // a control exists once
    for (int b = 0; b < 2; ++b) {
        for (const QString &id : lists[b]) {
            if (id == toolbars::Separator) {
                auto *sep = new QAction(this);
                sep->setSeparator(true);
                m_barOwned << sep;
                bars[b]->addAction(sep);
            } else if (id == toolbars::Spacer) {
                auto *space = new QWidget;
                space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                auto *wa = new QWidgetAction(this);
                wa->setDefaultWidget(space);
                m_barOwned << wa;
                bars[b]->addAction(wa);
            } else if (id == QLatin1String("viewModes")) {
                if (placed.contains(id))
                    continue;
                for (const char *k : {"viewText", "viewCork", "viewOutline"})
                    bars[b]->addAction(a[QLatin1String(k)]);
                placed.insert(id);
            } else if (QAction *ac = barItem(id)) {
                if (m_barItems.contains(id)) {
                    if (placed.contains(id))
                        continue;
                    placed.insert(id);
                }
                bars[b]->addAction(ac);
            }
        }
    }

    const Qt::ToolButtonStyle style = s.toolbarStyle == QLatin1String("beside") ? Qt::ToolButtonTextBesideIcon
                                      : s.toolbarStyle == QLatin1String("under") ? Qt::ToolButtonTextUnderIcon
                                                                                 : Qt::ToolButtonIconOnly;
    const QSize size(s.toolbarIconSize, s.toolbarIconSize);
    m_toolbar->setIconSize(size);
    m_toolbar->setToolButtonStyle(style);
    for (QToolButton *b : m_barButtons) {
        b->setIconSize(size);
        b->setToolButtonStyle(style);
    }
    installToolbarDropTargets();
}

// Navigate > Go To: the commands that move the active editor, then the binder
// as nested menus (a click loads the document into the active editor).
void MainWindow::fillGoToMenu(QMenu *menu)
{
    menu->clear();
    menu->addAction(a[QStringLiteral("prevDoc")]);
    menu->addAction(a[QStringLiteral("nextDoc")]);
    menu->addAction(a[QStringLiteral("enclosingGroup")]);
    menu->addSeparator();
    if (!m_project) {
        menu->addAction(tr("No project is open"))->setEnabled(false);
        return;
    }
    std::function<void(QMenu *, BinderItem *)> fill = [&](QMenu *m, BinderItem *parent) {
        for (BinderItem *it : parent->children) {
            if (it == m_project->trash())
                continue;
            const QString title = QString(it->title.isEmpty() ? tr("Untitled") : it->title).replace(QLatin1Char('&'), QLatin1String("&&"));
            auto go = [this, it] {
                loadInPane(m_active, {it});
                m_active->focusEditor();
            };
            if (it->children.isEmpty()) {
                m->addAction(icons::forItem(m_project.get(), it), title, this, go);
            } else {
                QMenu *sub = m->addMenu(icons::forItem(m_project.get(), it), title);
                // a group opens itself through its first entry
                sub->addAction(icons::forItem(m_project.get(), it), title, this, go);
                sub->addSeparator();
                fill(sub, it);
            }
        }
    };
    fill(menu, m_project->root());
}

// Documents can be dropped on two buttons of the toolbar: on Quick Reference
// (each one opens in a panel of its own) and on Bookmarks (added to the
// project bookmarks). The buttons are made anew when the bars are rebuilt.
void MainWindow::installToolbarDropTargets()
{
    auto holdsDocuments = [this](const QMimeData *md) {
        return m_project && !BinderModel::itemsFromMime(md, m_project.get()).isEmpty();
    };
    QList<QWidget *> quickRefButtons;
    for (QToolBar *bar : {m_toolbar, m_formatBar})
        if (bar)
            if (QWidget *w = bar->widgetForAction(a[QStringLiteral("quickRef")]))
                quickRefButtons << w;
    for (QWidget *w : quickRefButtons) {
        if (dnd::DropTarget::installedOn(w))
            continue;
        w->setToolTip(tr("Open as Quick Reference (documents dropped here open in panels of their own)"));
        new dnd::DropTarget(w, holdsDocuments, [this](const QMimeData *md, const QPoint &, Qt::KeyboardModifiers) {
            const QList<BinderItem *> items = BinderModel::itemsFromMime(md, m_project.get());
            // after the drag has ended: opening windows inside a drop confuses some desktops
            QTimer::singleShot(0, this, [this, items] {
                int n = 0;
                for (BinderItem *it : items)
                    if (QuickRefWindow *qr = openQuickReference(it))
                        qr->move(qr->pos() + QPoint(28 * n, 28 * n)), ++n; // side by side, not exactly on top of each other
            });
        });
    }
    if (auto *bm = findChild<QToolButton *>(QStringLiteral("bookmarksButton")); bm && !dnd::DropTarget::installedOn(bm)) {
        bm->setToolTip(tr("Project bookmarks (documents dropped here are added)"));
        new dnd::DropTarget(bm, holdsDocuments, [this](const QMimeData *md, const QPoint &, Qt::KeyboardModifiers) {
            addToProjectBookmarks(BinderModel::itemsFromMime(md, m_project.get()));
        });
    }
}

void MainWindow::customizeToolbars(int bar)
{
    ToolbarDialog dlg(this, bar == 1 ? toolbars::Bar::Format : toolbars::Bar::Main);
    dlg.exec();
}

// ------------------------------------------------------------------ commands that come and go

void MainWindow::rebuildStyleActions()
{
    for (QAction *old : m_styleActions) {
        removeAction(old);
        delete old;
    }
    m_styleActions.clear();
    commands::removeDynamic(QStringLiteral("style:"));
    const Formatting f = m_project ? m_project->formatting : Formatting();
    auto make = [this](const QString &name, const QString &text, const QKeySequence &standard) {
        auto *ac = new QAction(text, this);
        ac->setObjectName(QStringLiteral("style:") + name);
        ac->setData(name);
        ac->setShortcut(standard);
        connect(ac, &QAction::triggered, this, [this, name] {
            if (TextEditor *e = editor()) {
                e->applyStyle(name);
                updateFormatBar();
            }
        });
        addAction(ac);
        m_styleActions << ac;
        commands::add(ac, tr("Styles"), QString(), true);
    };
    make(QString(), tr("No Style"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0));
    for (const Style &s : styles::sheet(f).styles) {
        QKeySequence standard;
        for (int level = 1; level <= 3; ++level)
            if (s.name == QStringLiteral("Heading %1").arg(level))
                standard = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key(Qt::Key_0 + level));
        make(s.name, styles::marker(s) + s.name, standard);
    }
    commands::apply();
    for (QuickRefWindow *qr : quickRefWindows())
        shareShortcuts(qr); // the new style commands answer to their shortcuts there as well
    if (m_composition)
        shareShortcuts(m_composition);
}

void MainWindow::rebuildLayoutActions()
{
    for (QAction *old : m_layoutActions) {
        removeAction(old);
        delete old;
    }
    m_layoutActions.clear();
    commands::removeDynamic(QStringLiteral("layout:"));
    if (m_layoutMenu)
        m_layoutMenu->clear();
    layouts::Kind last = layouts::Kind::BuiltIn;
    for (const layouts::Layout &l : layouts::available()) {
        auto *ac = new QAction(QString(l.name).replace(QLatin1Char('&'), QLatin1String("&&")), this);
        ac->setObjectName(QStringLiteral("layout:") + l.id);
        ac->setToolTip(l.description);
        const QString id = l.id;
        connect(ac, &QAction::triggered, this, [this, id] { applyLayoutById(id); });
        addAction(ac);
        m_layoutActions << ac;
        commands::add(ac, tr("Layouts"), QString(), true);
        if (m_layoutMenu) {
            if (l.kind != last)
                m_layoutMenu->addSeparator();
            m_layoutMenu->addAction(ac);
        }
        last = l.kind;
    }
    if (m_layoutMenu) {
        m_layoutMenu->addSeparator();
        m_layoutMenu->addAction(a[QStringLiteral("layoutSave")]);
        m_layoutMenu->addAction(a[QStringLiteral("layoutManage")]);
        m_layoutMenu->setToolTipsVisible(true);
    }
}

// ------------------------------------------------------------------ layouts

static QString viewName(ViewMode m)
{
    switch (m) {
    case ViewMode::Corkboard: return QStringLiteral("corkboard");
    case ViewMode::Outliner: return QStringLiteral("outliner");
    case ViewMode::Single:
    case ViewMode::Combined: return QStringLiteral("text");
    default: return QStringLiteral("keep");
    }
}

QVariantMap MainWindow::captureLayout(bool withWindowSize) const
{
    auto share = [](double v) { return std::round(v * 1000.0) / 1000.0; };
    QVariantMap st;
    const bool binder = a[QStringLiteral("showBinder")]->isChecked();
    const bool inspector = a[QStringLiteral("showInspector")]->isChecked();
    st[QStringLiteral("binder")] = binder;
    st[QStringLiteral("collections")] = m_binder->collectionsVisible();
    st[QStringLiteral("inspector")] = inspector;
    st[QStringLiteral("inspectorTab")] = m_inspector->currentTab();
    st[QStringLiteral("toolbar")] = a[QStringLiteral("showToolbar")]->isChecked();
    st[QStringLiteral("formatBar")] = a[QStringLiteral("showFormatBar")]->isChecked();
    st[QStringLiteral("iconBar")] = a[QStringLiteral("showIconBar")]->isChecked();
    st[QStringLiteral("ruler")] = AppSettings::instance().showRuler;
    const QList<int> sizes = m_mainSplit->sizes();
    const double total = qMax(1, sizes.value(0) + sizes.value(1) + sizes.value(2));
    if (binder && sizes.value(0) > 0)
        st[QStringLiteral("binderShare")] = share(sizes.value(0) / total);
    if (inspector && sizes.value(2) > 0)
        st[QStringLiteral("inspectorShare")] = share(sizes.value(2) / total);
    const bool split = isSplit();
    st[QStringLiteral("split")] = !split                                             ? QStringLiteral("none")
                                  : m_editorSplit->orientation() == Qt::Horizontal ? QStringLiteral("vertical")
                                                                                   : QStringLiteral("horizontal");
    if (split) {
        const QList<int> es = m_editorSplit->sizes();
        if (es.value(0) + es.value(1) > 0)
            st[QStringLiteral("splitShare")] = share(double(es.value(0)) / (es.value(0) + es.value(1)));
    }
    QVariantList panes;
    for (int i = 0; i < (split ? 2 : 1); ++i) {
        const EditorPane *p = m_panes[i];
        QVariantMap pane;
        pane[QStringLiteral("view")] = viewName(p->mode());
        if (p->mode() == ViewMode::Corkboard)
            pane[QStringLiteral("cork")] = p->corkMode() == int(Corkboard::Mode::Freeform)       ? QStringLiteral("freeform")
                                           : p->corkMode() == int(Corkboard::Mode::LabelThreads) ? QStringLiteral("labels")
                                                                                                 : QStringLiteral("grid");
        if (p->selectionAffectsOther())
            pane[QStringLiteral("affectsOther")] = true;
        panes << pane;
    }
    st[QStringLiteral("panes")] = panes;
    st[QStringLiteral("binderAffects")] = m_binderAffects;
    st[QStringLiteral("active")] = split && m_active == m_panes[1] ? 1 : 0;
    if (withWindowSize) {
        QVariantMap win;
        win[QStringLiteral("maximized")] = isMaximized();
        win[QStringLiteral("width")] = normalGeometry().isValid() ? normalGeometry().width() : width();
        win[QStringLiteral("height")] = normalGeometry().isValid() ? normalGeometry().height() : height();
        st[QStringLiteral("window")] = win;
    }
    return st;
}

void MainWindow::applyLayout(const QVariantMap &st)
{
    if (st.isEmpty())
        return;
    auto flag = [&st](const char *key, bool fallback) { return st.value(QLatin1String(key), fallback).toBool(); };

    if (st.contains(QStringLiteral("window"))) {
        const QVariantMap win = st.value(QStringLiteral("window")).toMap();
        if (isFullScreen())
            a[QStringLiteral("fullScreen")]->setChecked(false);
        if (win.value(QStringLiteral("maximized")).toBool()) {
            showMaximized();
        } else {
            showNormal();
            const int w = win.value(QStringLiteral("width")).toInt(), h = win.value(QStringLiteral("height")).toInt();
            if (w >= 400 && h >= 300)
                resize(w, h);
        }
    }

    a[QStringLiteral("showToolbar")]->setChecked(flag("toolbar", true));
    a[QStringLiteral("showFormatBar")]->setChecked(flag("formatBar", true));
    a[QStringLiteral("showIconBar")]->setChecked(flag("iconBar", true));
    if (st.contains(QStringLiteral("ruler")))
        a[QStringLiteral("showRuler")]->setChecked(flag("ruler", true));
    const bool binder = flag("binder", true);
    const bool inspector = flag("inspector", a[QStringLiteral("showInspector")]->isChecked());
    a[QStringLiteral("showBinder")]->setChecked(binder);
    a[QStringLiteral("showInspector")]->setChecked(inspector);
    if (st.contains(QStringLiteral("collections")))
        a[QStringLiteral("showCollections")]->setChecked(flag("collections", false));
    const int tab = st.value(QStringLiteral("inspectorTab"), -1).toInt();
    if (inspector && tab >= 0 && tab <= int(Inspector::Objects))
        m_inspector->showTab(Inspector::Tab(tab));

    // editors
    const QString split = st.value(QStringLiteral("split"), QStringLiteral("none")).toString();
    if (split == QLatin1String("vertical"))
        setSplit(Qt::Horizontal, true);
    else if (split == QLatin1String("horizontal"))
        setSplit(Qt::Vertical, true);
    else
        setSplit(Qt::Vertical, false);

    // widths: shares of the window, so a layout fits small and large screens
    const QList<int> now = m_mainSplit->sizes();
    const int total = qMax(300, now.value(0) + now.value(1) + now.value(2));
    const int bw = binder ? int(total * qBound(0.05, st.value(QStringLiteral("binderShare"), 0.2).toDouble(), 0.6)) : 0;
    const int iw = inspector ? int(total * qBound(0.05, st.value(QStringLiteral("inspectorShare"), 0.22).toDouble(), 0.6)) : 0;
    m_mainSplit->setSizes({bw, qMax(100, total - bw - iw), iw});
    if (isSplit()) {
        const QList<int> es = m_editorSplit->sizes();
        const int et = qMax(100, es.value(0) + es.value(1));
        const int first = int(et * qBound(0.1, st.value(QStringLiteral("splitShare"), 0.5).toDouble(), 0.9));
        m_editorSplit->setSizes({first, et - first});
    }

    const QVariantList panes = st.value(QStringLiteral("panes")).toList();
    for (int i = 0; i < 2; ++i) {
        EditorPane *p = m_panes[i];
        if (i >= panes.size() || !p->isVisible())
            continue;
        const QVariantMap pane = panes[i].toMap();
        const QString view = pane.value(QStringLiteral("view"), QStringLiteral("keep")).toString();
        const QString cork = pane.value(QStringLiteral("cork")).toString();
        if (!cork.isEmpty())
            p->setCorkMode(cork == QLatin1String("freeform") ? int(Corkboard::Mode::Freeform)
                           : cork == QLatin1String("labels") ? int(Corkboard::Mode::LabelThreads)
                                                             : int(Corkboard::Mode::Grid));
        p->setSelectionAffectsOther(pane.value(QStringLiteral("affectsOther")).toBool());
        if (!m_project || view == QLatin1String("keep"))
            continue;
        // the view groups open in from now on, and the view of what is shown now
        const bool media = p->mode() == ViewMode::Media || p->mode() == ViewMode::Empty;
        if (view == QLatin1String("text")) {
            p->setGroupMode(ViewMode::Combined);
            if (!media && (p->mode() == ViewMode::Corkboard || p->mode() == ViewMode::Outliner))
                p->setViewMode(p->isGroup() ? ViewMode::Combined : ViewMode::Single);
        } else {
            const ViewMode wanted = view == QLatin1String("outliner") ? ViewMode::Outliner : ViewMode::Corkboard;
            p->setGroupMode(wanted);
            if (!media && p->isGroup())
                p->setViewMode(wanted);
        }
    }

    setBinderAffects(st.value(QStringLiteral("binderAffects"), QStringLiteral("current")).toString());
    if (isSplit())
        setActivePane(m_panes[st.value(QStringLiteral("active")).toInt() == 1 ? 1 : 0]);
    updateActions();
    refreshInspector();
    if (m_active && m_project)
        m_active->focusEditor();
}

bool MainWindow::applyLayoutById(const QString &id)
{
    layouts::Layout l;
    if (!layouts::load(id, &l))
        return false;
    applyLayout(l.state);
    return true;
}

void MainWindow::saveLayoutAs()
{
    QDialog ask(this);
    ask.setWindowTitle(tr("Save Current Layout"));
    auto *v = new QVBoxLayout(&ask);
    v->addWidget(new QLabel(tr("Name of the layout:"), &ask));
    auto *name = new QLineEdit(layouts::uniqueName(tr("My Layout")), &ask);
    name->setMinimumWidth(320);
    v->addWidget(name);
    auto *size = new QCheckBox(tr("Also remember the size of the window"), &ask);
    v->addWidget(size);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    name->selectAll();
    while (ask.exec() == QDialog::Accepted) {
        QString err;
        if (!layouts::saveUser(name->text(), captureLayout(size->isChecked()), &err).isEmpty())
            return;
        QMessageBox::warning(this, tr("Save Current Layout"), err);
    }
}

void MainWindow::manageLayouts()
{
    LayoutsDialog dlg([this](bool withWindowSize) { return captureLayout(withWindowSize); },
                      [this](const QVariantMap &st) { applyLayout(st); }, this);
    dlg.exec();
}

// ------------------------------------------------------------------ binder selection, quick reference windows

EditorPane *MainWindow::binderTarget() const
{
    const bool split = isSplit();
    EditorPane *target = m_active;
    if (split && m_binderAffects == QLatin1String("first"))
        target = m_panes[0];
    else if (split && m_binderAffects == QLatin1String("second"))
        target = m_panes[1];
    else if (split && m_binderAffects == QLatin1String("other"))
        target = otherPane(m_active);
    if (target->isLocked()) {
        // a locked editor stays as it is; with "current editor" the other one takes the click
        if (m_binderAffects != QLatin1String("current"))
            return nullptr;
        EditorPane *o = otherPane(target);
        return (o && o->isVisible() && !o->isLocked()) ? o : nullptr;
    }
    return target;
}

void MainWindow::setBinderAffects(const QString &mode)
{
    QString m = mode;
    if (m != QLatin1String("first") && m != QLatin1String("second") && m != QLatin1String("other"))
        m = QStringLiteral("current");
    m_binderAffects = m;
    for (QAction *ac : m_affectsGroup->actions())
        ac->setChecked(ac->data().toString() == m);
}

void MainWindow::applyQuickRefFloating()
{
    const bool on = AppSettings::instance().floatQuickRef;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *qr = qobject_cast<QuickRefWindow *>(w))
            qr->setFloating(on);
}

} // namespace qb
