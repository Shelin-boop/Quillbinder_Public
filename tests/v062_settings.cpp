// qb_v062_tests, part A: settings per project and the first start.
#include "v062_common.h"
#include "ui/KeyboardPage.h"

static QComboBox *combo(QWidget *w, const char *name) { return w->findChild<QComboBox *>(QLatin1String(name)); }

void settingsChecks()
{
    AppSettings &as = AppSettings::instance();

    // ================================================================ the first start
    {
        check(FirstStartDialog::due(), QStringLiteral("first start: the questions are due in a fresh installation"));
        FirstStartDialog d;
        d.show();
        settle(200);
        auto *backup = d.findChild<QCheckBox *>(QStringLiteral("firstBackup"));
        auto *keep = d.findChild<QSpinBox *>(QStringLiteral("firstBackupKeep"));
        auto *folder = d.findChild<QLineEdit *>(QStringLiteral("firstBackupFolder"));
        auto *projectFirst = d.findChild<QRadioButton *>(QStringLiteral("firstProjectFirst"));
        auto *programFirst = d.findChild<QRadioButton *>(QStringLiteral("firstProgramFirst"));
        check(backup && keep && folder && projectFirst && programFirst && keep->value() == as.backupKeep
                  && !folder->text().isEmpty() && projectFirst->isChecked(),
              QStringLiteral("first start: backups and which settings come first, filled in with the values in use"));
        shot(&d, QStringLiteral("v062-first-start"));
        const QString dir = QDir::tempPath() + QStringLiteral("/qb-v062-backups-%1").arg(QCoreApplication::applicationPid());
        if (backup && keep && folder && programFirst) {
            backup->setChecked(true);
            keep->setValue(7);
            folder->setText(dir);
            programFirst->setChecked(true);
        }
        d.take();
        check(as.backupOnClose && as.backupKeep == 7 && as.backupDir == dir && QDir(dir).exists() && !settingslayer::projectFirst()
                  && !FirstStartDialog::due() && programValue(QStringLiteral("backup/keep")).toInt() == 7,
              QStringLiteral("first start: the choices are taken over and the questions are not asked again"),
              QStringLiteral("%1 %2 %3").arg(as.backupKeep).arg(as.backupDir).arg(settingslayer::projectFirst()));
        settingslayer::setProjectFirst(true);
        as.backupOnClose = false;
        as.save();
        QDir(dir).removeRecursively();
    }

    // ================================================================ a project with settings of its own
    QTemporaryDir work;
    auto proj = blankProject(work.path(), QStringLiteral("Own Settings"));
    if (!proj)
        return;
    Project *p = proj.get();
    textDoc(p, p->draft(), QStringLiteral("Scene"), {QStringLiteral("He said, \"Later.\"")});
    QString err;
    p->save(&err);
    const QString projectFile = p->indexPath();

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(600);
    p = w.project();
    const QString ownFile = settingslayer::fileFor(p->settingsDir());
    check(settingslayer::active() && settingslayer::projectFile() == ownFile,
          QStringLiteral("a project opens with its settings layer (the program reads a merged copy)"));

    // Options in "This project" (the default with a project open)
    QString title;
    int countAfter = -1;
    {
        OptionsDialog dlg(&w);
        title = dlg.windowTitle();
        dlg.showSection(OptionsDialog::SCorrections);
        dlg.show();
        settle(200);
        auto *scope = combo(&dlg, "optScope");
        check(scope && scope->currentIndex() == OptionsDialog::ThisProject && dlg.optionScope() == OptionsDialog::ThisProject
                  && title.contains(QLatin1String("This Project")),
              QStringLiteral("Options: \"Settings for\" shows This project when a project is open"), title);
        if (auto *qs = combo(&dlg, "optQuoteStyle"))
            qs->setCurrentIndex(qs->findData(QStringLiteral("de")));
        dlg.apply();
        countAfter = OptionsDialog::projectValueCount();
        shot(&dlg, QStringLiteral("v062-options-this-project"));
        dlg.reject();
    }
    settle(200);
    check(countAfter == 1 && settingslayer::projectValues().value(QStringLiteral("editing/quoteStyle")).toString() == QLatin1String("de"),
          QStringLiteral("This project: only the changed option is kept with the project"),
          QStringLiteral("%1 values: %2").arg(countAfter).arg(settingslayer::projectValues().keys().join(QLatin1Char(' '))));
    check(as.quoteStyle == QLatin1String("de") && programValue(QStringLiteral("editing/quoteStyle")).toString() == QLatin1String("en"),
          QStringLiteral("This project: the project uses its value, the program keeps its own"),
          as.quoteStyle + QLatin1Char('/') + programValue(QStringLiteral("editing/quoteStyle")).toString());

    // Options in "All projects" while the project is open
    {
        OptionsDialog::setNextScope(OptionsDialog::AllProjects);
        OptionsDialog dlg(&w);
        dlg.showSection(OptionsDialog::SCorrections);
        auto *qs = combo(&dlg, "optQuoteStyle");
        const QString shown = qs ? qs->currentData().toString() : QString();
        auto *capI = dlg.findChild<QCheckBox *>(QStringLiteral("optCapI"));
        if (capI)
            capI->setChecked(true);
        dlg.apply();
        dlg.reject();
        check(shown == QLatin1String("en"), QStringLiteral("All projects: the window shows the program's own values"), shown);
    }
    settle(200);
    check(as.capitalizeI && as.quoteStyle == QLatin1String("de") && programValue(QStringLiteral("editing/capitalizeI")).toBool()
              && !settingslayer::projectValues().contains(QStringLiteral("editing/capitalizeI")),
          QStringLiteral("All projects: the change reaches the program; the open project keeps its own value on top"));

    // a change outside Options goes where the value came from
    as.showInvisibles = true;
    as.save();
    as.quoteStyle = QStringLiteral("fr");
    as.save();
    check(programValue(QStringLiteral("editing/showInvisibles")).toBool()
              && settingslayer::projectValues().value(QStringLiteral("editing/quoteStyle")).toString() == QLatin1String("fr")
              && programValue(QStringLiteral("editing/quoteStyle")).toString() == QLatin1String("en"),
          QStringLiteral("changes outside Options: the project's own key to the project, the others to the program"));
    as.showInvisibles = false;
    as.save();

    // keyboard shortcuts of the project
    {
        OptionsDialog dlg(&w);
        if (dlg.keyboardPage())
            dlg.keyboardPage()->assign(QStringLiteral("bold"), QKeySequence(QStringLiteral("Ctrl+Alt+B")), true);
        dlg.apply();
        dlg.reject();
    }
    settle(200);
    const QString boldHere = commands::shortcutOf(QStringLiteral("bold")).toString(QKeySequence::PortableText);
    check(boldHere == QLatin1String("Ctrl+Alt+B") && settingslayer::projectValues().contains(QStringLiteral("keyboard/shortcuts")),
          QStringLiteral("a keyboard shortcut can be the project's own"), boldHere);

    // the project's settings file carries the options
    const QString settingsFile = work.path() + QStringLiteral("/own.qbsettings");
    check(settingsfile::save(settingsFile, p, &err)
              && QString::fromUtf8([&] {
                     QFile f(settingsFile);
                     return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
                 }())
                     .contains(QLatin1String("editing/quoteStyle")),
          QStringLiteral("Project > Save Project Settings: the options of the project are in the file"), err);

    // which settings come first: the program's always
    {
        OptionsDialog::setNextScope(OptionsDialog::AllProjects);
        OptionsDialog dlg(&w);
        auto *pf = dlg.findChild<QCheckBox *>(QStringLiteral("optProjectFirst"));
        check(pf && pf->isEnabled() && pf->isChecked(), QStringLiteral("General > Startup: \"Which settings come first\" (on)"));
        if (pf)
            pf->setChecked(false);
        dlg.apply();
        dlg.reject();
    }
    settle(300);
    const QString boldProgram = commands::shortcutOf(QStringLiteral("bold")).toString(QKeySequence::PortableText);
    check(!settingslayer::active() && as.quoteStyle == QLatin1String("en") && boldProgram != QLatin1String("Ctrl+Alt+B")
              && settingslayer::projectValues().value(QStringLiteral("editing/quoteStyle")).toString() == QLatin1String("fr"),
          QStringLiteral("program settings always: the program's values apply, the project keeps its own for later"),
          as.quoteStyle + QLatin1Char(' ') + boldProgram);
    {
        OptionsDialog dlg(&w);
        auto *scope = combo(&dlg, "optScope");
        bool off = false;
        if (scope)
            if (auto *m = qobject_cast<QStandardItemModel *>(scope->model()))
                off = !m->item(OptionsDialog::ThisProject)->isEnabled();
        check(dlg.optionScope() == OptionsDialog::AllProjects && off,
              QStringLiteral("program settings always: \"This project\" cannot be chosen"));
        dlg.reject();
    }
    check(look::active() == look::forNewProjects(), QStringLiteral("program settings always: also the look for new projects"));
    settingslayer::setProjectFirst(true);
    settle(300);
    check(settingslayer::active() && as.quoteStyle == QLatin1String("fr"), QStringLiteral("project first again: its values return"),
          as.quoteStyle);

    // closed and opened again
    w.closeProject();
    settle(300);
    check(!settingslayer::active() && as.quoteStyle == QLatin1String("en") && as.capitalizeI,
          QStringLiteral("project closed: the program's own settings"), as.quoteStyle);
    {
        auto again = Project::open(projectFile, &err);
        check(again && w.adoptProject(std::move(again)), QStringLiteral("(opened again)"), err);
        settle(400);
    }
    check(as.quoteStyle == QLatin1String("fr"), QStringLiteral("opened again: the project's own settings come back"), as.quoteStyle);

    // the scope switch reopens the window in the other scope
    {
        int seen = -1;
        int round = 0;
        OptionsDialog::run(&w, [&](OptionsDialog &dlg) {
            ++round;
            seen = dlg.optionScope();
            QTimer::singleShot(150, &dlg, [&dlg, round] {
                if (round == 1) {
                    if (auto *scope = dlg.findChild<QComboBox *>(QStringLiteral("optScope")))
                        scope->setCurrentIndex(OptionsDialog::AllProjects);
                } else {
                    dlg.reject();
                }
            });
        });
        check(round == 2 && seen == OptionsDialog::AllProjects, QStringLiteral("changing \"Settings for\" opens the window again in that scope"),
              QStringLiteral("rounds %1, scope %2").arg(round).arg(seen));
    }

    // Use Program Settings
    {
        withModal<QMessageBox>(300, [](QMessageBox *b) { b->button(QMessageBox::Yes)->click(); });
        OptionsDialog dlg(&w);
        auto *reset = dlg.findChild<QPushButton *>(QStringLiteral("optScopeReset"));
        check(reset && reset->isEnabled(), QStringLiteral("Use Program Settings is offered while the project has own settings"));
        if (reset)
            reset->click();
        settle(300);
    }
    check(OptionsDialog::projectValueCount() == 0 && as.quoteStyle == QLatin1String("en")
              && commands::shortcutOf(QStringLiteral("bold")).toString(QKeySequence::PortableText) != QLatin1String("Ctrl+Alt+B"),
          QStringLiteral("Use Program Settings: the project has no settings of its own any more"), as.quoteStyle);

    // a settings file gives another project the options
    {
        QJsonObject doc;
        settingsfile::load(settingsFile, &doc, &err);
        QStringList notes;
        settingsfile::applyData(w.project(), settingsfile::Options,
                                doc.value(QStringLiteral("sections")).toObject().value(QStringLiteral("options")), &notes);
        settle(200);
        check(as.quoteStyle == QLatin1String("fr") && OptionsDialog::projectValueCount() >= 2,
              QStringLiteral("Project > Load Project Settings: the options from the file become the project's"),
              notes.join(QLatin1Char(' ')));
        settingslayer::clearProjectValues();
    }
    w.closeProject();
    settle(200);
    w.close();
    delete wp;
    settle(100);
}
