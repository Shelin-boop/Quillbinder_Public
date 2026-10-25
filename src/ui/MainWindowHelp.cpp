// Help menu: the welcome project (made from the outline in resources/welcome
// at the first start and by Help > Welcome Project), About, and the list of placeholders.
#include "ui/MainWindow.h"

#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/Project.h"
#include "core/OutlineProject.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/LicensesDialog.h"

#include "AppInfo.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QShortcut>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QJsonObject>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qb {

QString MainWindow::welcomeName() { return tr("Welcome to %1").arg(QLatin1String(QB_APP_NAME)); }

// The folder the welcome project goes into: where it was made last time, else Documents, else home.
static QString welcomeParent()
{
    QString parent = AppSettings::instance().welcomeFolder;
    if (parent.isEmpty() || !QFileInfo(parent).isDir())
        parent = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (parent.isEmpty() || !QFileInfo(parent).isDir())
        parent = QDir::homePath();
    return parent;
}

QString MainWindow::welcomeFolder()
{
    return QDir(welcomeParent()).filePath(text::safeFileName(welcomeName()) + QLatin1String(QB_PROJECT_SUFFIX));
}

bool MainWindow::openWelcomeOnFirstStart()
{
    if (AppSettings::instance().welcomeShown || qEnvironmentVariableIsSet("QB_NO_WELCOME"))
        return false;
    openWelcomeProject(true);
    return m_project != nullptr;
}

void MainWindow::openWelcomeProject(bool firstStart)
{
    const QString parent = welcomeParent();
    const QString name = welcomeName();
    const QString folder = welcomeFolder();
    const QString title = tr("Welcome Project");
    if (QFileInfo::exists(folder)) {
        if (firstStart) {
            if (openProject(Project::resolveProjectFile(folder))) {
                AppSettings::instance().welcomeShown = true;
                AppSettings::instance().save();
            }
            return;
        }
        QMessageBox box(QMessageBox::Question, title,
                        tr("The welcome project is already there:\n%1").arg(QDir::toNativeSeparators(folder)), QMessageBox::Cancel,
                        this);
        box.setInformativeText(tr("Open it as you left it, or make a fresh copy to start over. A fresh copy replaces "
                                  "this one: what you changed in it goes to the desktop's trash."));
        QPushButton *open = box.addButton(tr("Open the Welcome Project"), QMessageBox::AcceptRole);
        QPushButton *copy = box.addButton(tr("Make a Fresh Copy"), QMessageBox::DestructiveRole);
        box.setDefaultButton(open);
        box.exec();
        if (box.clickedButton() == open) {
            openProject(Project::resolveProjectFile(folder));
            return;
        }
        if (box.clickedButton() != copy)
            return;
        // a project that is open must be closed before its folder can go
        if (m_project && QFileInfo(m_project->folderPath()).canonicalFilePath() == QFileInfo(folder).canonicalFilePath()
            && !closeProject())
            return;
        // the old copy goes to the trash; if the desktop has none, it keeps a dated name. (Qt puts the
        // trash into the current folder when the data folder of the user is missing: make it first.)
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
        if (!QFile::moveToTrash(folder)) {
            QString old = folder;
            old.chop(int(qstrlen(QB_PROJECT_SUFFIX)));
            old += QStringLiteral(" (%1)").arg(QDate::currentDate().toString(Qt::ISODate));
            int n = 2;
            while (QFileInfo::exists(old + QLatin1String(QB_PROJECT_SUFFIX)))
                old = old.section(QLatin1String(" ("), 0, -2) + QStringLiteral(" (%1 %2)").arg(QDate::currentDate().toString(Qt::ISODate)).arg(n++);
            if (!QDir().rename(folder, old + QLatin1String(QB_PROJECT_SUFFIX))) {
                QMessageBox::warning(this, title, tr("The old welcome project could not be put aside:\n%1").arg(QDir::toNativeSeparators(folder)));
                return;
            }
        }
    }
    QString err;
    const QJsonObject o = outline::read(QStringLiteral(":/welcome/outline.json"), &err);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    outline::Result r = o.isEmpty() ? outline::Result() : outline::build(o, QStringLiteral(":/welcome"), parent, name);
    QApplication::restoreOverrideCursor();
    if (!r.project) {
        if (!err.isEmpty())
            r.errors.prepend(err);
        QMessageBox::warning(this, title, tr("The welcome project could not be made:\n%1").arg(r.errors.mid(0, 8).join(QLatin1Char('\n'))));
        return;
    }
    AppSettings::instance().welcomeFolder = parent;
    AppSettings::instance().welcomeShown = true;
    AppSettings::instance().addRecent(r.project->displayFile());
    AppSettings::instance().save();
    Project *proj = r.project.get();
    if (!adoptProject(std::move(r.project)))
        return;
    // it begins with the document at the top of the binder
    for (BinderItem *it : proj->root()->children)
        if (it->hasText() && it->type == ItemType::Text) {
            m_binder->select({it}, true);
            break;
        }
    statusBar()->showMessage(tr("The welcome project is saved in %1. Help > Welcome Project opens it again.")
                                 .arg(QDir::toNativeSeparators(QFileInfo(proj->folderPath()).absolutePath())),
                             15000);
}

void MainWindow::showAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("About %1").arg(QLatin1String(QB_APP_NAME)));
    box.setObjectName(QStringLiteral("aboutBox"));
    box.setIconPixmap(QIcon(QStringLiteral(":/app/quillbinder.svg")).pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(aboutText());
    QPushButton *licenses = box.addButton(tr("Third-Party Licenses…"), QMessageBox::ActionRole);
    licenses->setObjectName(QStringLiteral("aboutLicenses"));
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == licenses)
        showThirdPartyLicenses(this);
}

void MainWindow::showPlaceholders()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("List of All Placeholders"));
    dlg.setObjectName(QStringLiteral("placeholdersDialog"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(tr("Placeholders are replaced when you compile: in the text, in titles of documents, in section "
                                "layouts, and in headers and footers. Insert numbers and the most common ones with "
                                "Insert > Auto Number and Insert > Placeholders."),
                             &dlg);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("<$n>"), tr("1, 2, 3 … (a counter that goes up each time)")},
        {QStringLiteral("<$w>"), tr("one, two, three …")},
        {QStringLiteral("<$W>"), tr("One, Two, Three …")},
        {QStringLiteral("<$WW>"), tr("ONE, TWO, THREE …")},
        {QStringLiteral("<$R>"), tr("I, II, III …")},
        {QStringLiteral("<$r>"), tr("i, ii, iii …")},
        {QStringLiteral("<$L>"), tr("A, B, C …")},
        {QStringLiteral("<$l>"), tr("a, b, c …")},
        {QStringLiteral("<$n:name>"), tr("a counter of its own called \"name\" (works with every number above: <$W:chapter>)")},
        {QStringLiteral("<$title>"), tr("the title of the document (in headers and footers of printouts: what is printed)")},
        {QStringLiteral("<$projecttitle>"), tr("the title of the project")},
        {QStringLiteral("<$shorttitle>"), tr("the first three words of the project title, in capitals")},
        {QStringLiteral("<$author>"), tr("your name (File > Options > General, or the compile metadata)")},
        {QStringLiteral("<$forename>"), tr("your first name")},
        {QStringLiteral("<$surname>"), tr("your last name")},
        {QStringLiteral("<$street>"), tr("your street (File > Options > General > Author Information; also <$city>, <$state>, <$zip>, <$country>)")},
        {QStringLiteral("<$phone>"), tr("your phone number")},
        {QStringLiteral("<$email>"), tr("your email address")},
        {QStringLiteral("<$address>"), tr("your whole address: street, ZIP and city, state, country on lines of their own")},
        {QStringLiteral("<$wc>"), tr("the number of words compiled")},
        {QStringLiteral("<$wc100>"), tr("the number of words, rounded to hundreds (also <$wc1000> and others)")},
        {QStringLiteral("<$date>"), tr("today's date (yyyy-mm-dd)")},
        {QStringLiteral("<$year>"), tr("the year")},
        {QStringLiteral("<$p>"), tr("the page number (headers and footers)")},
        {QStringLiteral("<$pages>"), tr("the number of pages (headers and footers of PDF files and printouts)")}};
    auto *table = new QTableWidget(int(rows.size()), 2, &dlg);
    table->setObjectName(QStringLiteral("placeholdersTable"));
    table->setHorizontalHeaderLabels({tr("Placeholder"), tr("Becomes")});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (int i = 0; i < rows.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(rows[i].first));
        table->setItem(i, 1, new QTableWidgetItem(rows[i].second));
    }
    table->resizeColumnToContents(0);
    table->horizontalHeader()->setStretchLastSection(true);
    lay->addWidget(table, 1);
    auto *note = new QLabel(tr("Select rows and press Ctrl+C to copy them."), &dlg);
    lay->addWidget(note);
    auto *copy = new QShortcut(QKeySequence::Copy, table);
    connect(copy, &QShortcut::activated, table, [table] {
        QStringList lines;
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->item(r, 0)->isSelected() || table->item(r, 1)->isSelected())
                lines << table->item(r, 0)->text() + QLatin1Char('\t') + table->item(r, 1)->text();
        if (!lines.isEmpty())
            QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    });
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    dlg.resize(680, 560);
    dlg.exec();
}

} // namespace qb
