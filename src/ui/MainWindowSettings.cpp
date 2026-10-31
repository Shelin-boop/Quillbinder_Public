// Project > Save Project Settings to File / Load Project Settings from File (0.40.2):
// core/SettingsFile.* writes and compares the sections, ui/SettingsTransfer.* asks per
// section what to do, this file carries the choices out.
#include "ui/MainWindow.h"

#include "core/DocumentStore.h"
#include "core/Project.h"
#include "text/Script.h"
#include "text/Styles.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/FormattingChange.h"
#include "ui/ProjectLook.h"
#include "ui/SettingsTransfer.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSaveFile>
#include <QStatusBar>

namespace qb {

namespace sf = settingsfile;

QJsonObject MainWindow::currentProjectSettings()
{
    if (!m_project)
        return QJsonObject();
    QJsonObject s = sf::capture(m_project.get());
    // the look as the project shows it (settings/look.json is made when the project opens)
    s.insert(sf::key(sf::Look), look::active().toJson());
    return s;
}

bool MainWindow::exportProjectSettings(const QString &fileIn)
{
    if (!m_project)
        return false;
    QString file = fileIn;
    if (file.isEmpty()) {
        const QString suggestion = QDir(AppSettings::instance().lastProjectDir.isEmpty() ? QDir::homePath()
                                                                                          : AppSettings::instance().lastProjectDir)
                                       .filePath(text::safeFileName(m_project->title()) + QLatin1String(" settings.qbsettings"));
        file = QFileDialog::getSaveFileName(this, tr("Save Project Settings"), suggestion, tr("Project settings (*.qbsettings)"));
        if (file.isEmpty())
            return false;
        if (!file.endsWith(QLatin1String(".qbsettings")))
            file += QLatin1String(".qbsettings");
    }
    QJsonObject doc = sf::document(m_project.get());
    doc.insert(QStringLiteral("sections"), currentProjectSettings());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(doc).toJson()) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Save Project Settings"), tr("Cannot write %1: %2").arg(file, f.errorString()));
        return false;
    }
    statusBar()->showMessage(tr("The project's settings were saved to %1.").arg(QDir::toNativeSeparators(file)), 10000);
    return true;
}

bool MainWindow::importProjectSettings(const QString &fileIn)
{
    if (!m_project)
        return false;
    QString file = fileIn;
    if (file.isEmpty()) {
        file = QFileDialog::getOpenFileName(this, tr("Load Project Settings"),
                                            AppSettings::instance().lastProjectDir.isEmpty() ? QDir::homePath()
                                                                                             : AppSettings::instance().lastProjectDir,
                                            tr("Project settings (*.qbsettings);;All files (*)"));
        if (file.isEmpty())
            return false;
    }
    QJsonObject doc;
    QString err;
    if (!sf::load(file, &doc, &err)) {
        QMessageBox::warning(this, tr("Load Project Settings"), err);
        return false;
    }
    const QJsonObject now = currentProjectSettings();
    const QJsonObject defaults = settingstransfer::defaults();
    SettingsImportDialog dlg(file, doc, now, defaults, this);
    if (dlg.differing() == 0) {
        QMessageBox box(QMessageBox::Information, tr("Load Project Settings"),
                        tr("The project's settings are the same as in the file. Nothing was changed."), QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("settingsSameNotice"));
        box.exec();
        return true;
    }
    if (dlg.exec() != QDialog::Accepted)
        return false;

    const QJsonObject fromFile = doc.value(QStringLiteral("sections")).toObject();
    // the values to take over, section by section
    QJsonObject take;
    for (int i = 0; i < sf::SectionCount; ++i) {
        const auto s = sf::Section(i);
        if (!dlg.differs(s))
            continue;
        switch (dlg.choice(s)) {
        case SettingsImportDialog::TakeFile: take.insert(sf::key(s), fromFile.value(sf::key(s))); break;
        case SettingsImportDialog::UseDefault:
            if (defaults.contains(sf::key(s)) && !sf::same(defaults.value(sf::key(s)), now.value(sf::key(s))))
                take.insert(sf::key(s), defaults.value(sf::key(s)));
            break;
        case SettingsImportDialog::KeepProject: break;
        }
    }
    if (take.isEmpty())
        return true;

    // the settings as they were, so that the writer can go back (Load Project Settings with this file)
    const QString backup = QDir(m_project->settingsDir())
                               .filePath(QStringLiteral("settings-before-loading-%1.qbsettings")
                                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"))));
    {
        QJsonObject before = sf::document(m_project.get());
        before.insert(QStringLiteral("sections"), now);
        QSaveFile f(backup);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(before).toJson());
        f.commit();
    }
    QStringList notes;
    applyProjectSettings(take, &notes);
    QString done = tr("%n section(s) of the project's settings were changed.", nullptr, int(take.size()));
    done += QLatin1Char('\n') + tr("The settings as they were before are kept in %1; load that file to go back.")
                                    .arg(QDir::toNativeSeparators(backup));
    if (!notes.isEmpty())
        done += QStringLiteral("\n\n") + notes.join(QLatin1Char('\n'));
    QMessageBox box(QMessageBox::Information, tr("Load Project Settings"), done, QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("settingsLoadedNotice"));
    if (!qEnvironmentVariableIsSet("QB_NO_DIALOGS"))
        box.exec();
    m_lastSettingsNotes = notes;
    return true;
}

void MainWindow::applyProjectSettings(const QJsonObject &take, QStringList *notes)
{
    Project *p = m_project.get();
    if (!p)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // the plain settings first (section types before compile, which names them)
    for (sf::Section s : {sf::Labels, sf::Status, sf::SectionTypes, sf::CustomMetadata, sf::Keywords, sf::Targets, sf::Compile, sf::PageSetup, sf::Printing, sf::ObjectTemplates})
        if (take.contains(sf::key(s)))
            sf::applyData(p, s, take.value(sf::key(s)), notes);
    // the formatting of the project: every document follows (as Project Settings > Formatting)
    if (take.contains(sf::key(sf::Formatting))) {
        const qb::Formatting oldF = p->formatting;
        qb::Formatting f = oldF;
        sf::formattingFrom(take.value(sf::key(sf::Formatting)), &f);
        p->formatting = f;
        p->docs()->applyFormatting();
        QString err;
        if (formatchange::followProject(p, oldF, true, &err) < 0 && notes)
            *notes << tr("Some documents could not follow the new formatting:\n%1").arg(err);
    }
    // the styles: the file's, and those the text uses that the file does not have
    if (take.contains(sf::key(sf::Styles))) {
        StyleSheet sheet;
        bool builtIn = false;
        if (sf::stylesFrom(take.value(sf::key(sf::Styles)), &sheet, &builtIn)) {
            const StyleSheet &now = styles::sheet(p->formatting);
            for (const QString &name : sf::stylesInUse(p)) {
                if (sheet.find(name))
                    continue;
                if (const Style *st = now.find(name)) {
                    sheet.styles << *st;
                    if (notes)
                        *notes << tr("Style “%1” kept: the text uses it.").arg(name);
                }
            }
            commitStyles(sheet);
            // the chosen style list (0.40.3): files without one give the plain list
            const QJsonObject so = take.value(sf::key(sf::Styles)).toObject();
            p->styleListOrder.clear();
            p->styleListHidden.clear();
            for (const QJsonValue &v : so.value(QStringLiteral("list")).toArray())
                p->styleListOrder << v.toString();
            for (const QJsonValue &v : so.value(QStringLiteral("hidden")).toArray())
                p->styleListHidden << v.toString();
            refreshStyleUi();
        } else if (notes) {
            *notes << tr("The styles in the file could not be read; the project keeps its styles.");
        }
    }
    if (take.contains(sf::key(sf::ScriptFormat))) {
        const QJsonObject o = take.value(sf::key(sf::ScriptFormat)).toObject();
        bool ok = true;
        const script::Format sf = o.value(QStringLiteral("builtIn")).toBool() ? script::standard()
                                                                              : script::fromJson(o.value(QStringLiteral("format")).toObject(), &ok);
        QString err;
        if (ok)
            p->setScriptFormat(sf, {}, {}, &err);
        else if (notes)
            *notes << tr("The script format in the file could not be read; the project keeps its own.");
    }
    if (take.contains(sf::key(sf::Look))) {
        const look::Values v = look::Values::fromJson(take.value(sf::key(sf::Look)).toObject(), look::forNewProjects());
        QString err;
        if (!look::change(v, &err) && notes)
            *notes << err;
    }
    p->markIndexDirty();
    p->notifySettingsChanged();
    QString err;
    p->save(&err);
    QApplication::restoreOverrideCursor();
    refreshStyleUi();
    updateActions();
}

} // namespace qb
