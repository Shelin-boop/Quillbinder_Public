// The system test, stage 2: helpers shared by the system, usability and whole-program cases.
#include "cs.h"

#include "core/Zip.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <utility>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QTest>
#include <QTreeView>

namespace cs {

using namespace qb;

bool menuPath(MainWindow *w, const QString &path)
{
    return chooseMenu(w, path.split(QStringLiteral(" > ")));
}

static QString plainLabel(QString t)
{
    return t.remove(QLatin1Char('&')).trimmed();
}

void clickLater(int ms, const QString &buttonText, int tries)
{
    QTimer::singleShot(ms, qApp, [buttonText, tries] {
        for (QWidget *tl : QApplication::topLevelWidgets()) {
            auto *d = qobject_cast<QDialog *>(tl);
            if (!d || !d->isVisible())
                continue;
            for (QAbstractButton *b : d->findChildren<QAbstractButton *>())
                if (b->isVisible() && b->isEnabled() && plainLabel(b->text()).contains(buttonText, Qt::CaseInsensitive)) {
                    note(QStringLiteral("(%1: %2) %3").arg(d->objectName().isEmpty() ? d->windowTitle() : d->objectName(), plainLabel(b->text()),
                                                         qobject_cast<QMessageBox *>(d) ? qobject_cast<QMessageBox *>(d)->text().left(120) : QString()));
                    b->click();
                    return;
                }
        }
        if (tries > 0)
            clickLater(150, buttonText, tries - 1);
        else
            note(QStringLiteral("(no window with a button \"%1\" came)").arg(buttonText));
    });
}

void answerBoxLater(int ms, QMessageBox::StandardButton button, BoxText *text)
{
    std::shared_ptr<bool> alive = text ? text->alive : nullptr;
    withModal<QMessageBox>(ms, [button, text, alive](QMessageBox *b) {
        if (text)
            *text = b->text();
        if (QAbstractButton *x = b->button(button))
            x->click();
        else if (button == QMessageBox::Cancel || button == QMessageBox::No)
            b->reject();
        else
            b->accept();
    }, 100, alive);
}

void fileDialogLater(int ms, const QString &path, bool *asked)
{
    withModal<QFileDialog>(ms, [path, asked](QFileDialog *fd) {
        if (asked)
            *asked = true;
        const QFileInfo fi(path);
        if (fi.isDir()) {
            fd->setDirectory(path);
            fd->selectFile(path);
            // 0.64.2: the same for a folder: the name field (focused) keeps what it had otherwise
            if (auto *name = fd->findChild<QLineEdit *>(QStringLiteral("fileNameEdit")); name && fd->fileMode() == QFileDialog::Directory)
                name->setText(path);
        } else {
            fd->setDirectory(fi.absolutePath());
            fd->selectFile(path);
            // 0.64.2: Qt does not change the name field while it has the focus (a save dialog gives it the
            // focus), so the suggested name stayed and S-C01 waited for a file under another name
            if (auto *name = fd->findChild<QLineEdit *>(QStringLiteral("fileNameEdit")))
                name->setText(fd->fileMode() == QFileDialog::ExistingFiles || fd->acceptMode() == QFileDialog::AcceptOpen
                                  ? path : fi.fileName());
        }
        static_cast<QDialog *>(fd)->accept();
    }, 150);   // up to about 22 s: the menus are opened by mouse first (shakedown 2026-11-09)
}

void closeDialogsLater(int ms)
{
    QTimer::singleShot(ms, qApp, [] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QDialog *>(tl); d && d->isVisible())
                d->reject();
    });
}

BinderPanel *binderOf(MainWindow *w)
{
    return w ? w->findChild<BinderPanel *>() : nullptr;
}

void selectItems(MainWindow *w, const QList<BinderItem *> &items)
{
    if (BinderPanel *bp = binderOf(w)) {
        bp->select(items, true);
        bp->tree()->setFocus(Qt::OtherFocusReason);
        settle(150);
    }
}

QString projectPathOf(Project *p)
{
    if (!p)
        return QString();
    return p->scrivenerPath.isEmpty() ? p->indexPath() : p->scrivenerPath;
}

MainWindow *reopenWindow(MainWindow *w)
{
    if (!w || !w->project())
        return w;
    const QString path = projectPathOf(w->project());
    QString err;
    w->project()->save(&err);
    w->closeProject();
    settle(400);
    delete w;
    return openWindow(path);
}

QString docText(Project *p, BinderItem *it)
{
    return p && it ? rawText(p->docs()->text(it->id)) : QString();
}

QString pdfToText(const QString &pdf, bool layout)
{
    int rc = -1;
    QStringList args;
    if (layout)
        args << QStringLiteral("-layout");
    args << pdf << QStringLiteral("-");
    const QString t = runTool(QStringLiteral("pdftotext"), args, &rc, 60000);
    if (rc != 0)
        return QString();
    // 0.64.2: typographic ligatures ("ﬁ", "ﬂ") as their letters, so that words are found
    QString out = t;
    for (const auto &pr : {std::pair<ushort, const char *>{0xFB00, "ff"}, {0xFB01, "fi"}, {0xFB02, "fl"}, {0xFB03, "ffi"}, {0xFB04, "ffl"}, {0xFB05, "st"}, {0xFB06, "st"}})
        out.replace(QChar(pr.first), QLatin1String(pr.second));
    return out;
}

QByteArray zipFile(const QString &zip, const QString &name)
{
    ZipReader z(zip);
    return z.fileData(name);
}

QString variant()
{
    return qEnvironmentVariable("QB_CS_VARIANT");
}

int subCase(const QString &variantName, const QMap<QString, QString> &env, QString *summary)
{
    const QString dir = ctx().dir + QStringLiteral("/variant-") + variantName;
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString home = dir + QStringLiteral("/home");
    QDir().mkpath(home);
    QProcess pr;
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert(QStringLiteral("HOME"), home);
    pe.insert(QStringLiteral("XDG_RUNTIME_DIR"), home);
    pe.remove(QStringLiteral("XDG_CONFIG_HOME"));
    pe.remove(QStringLiteral("XDG_DATA_HOME"));
    pe.insert(QStringLiteral("QB_CS_VARIANT"), variantName);
    for (auto i = env.begin(); i != env.end(); ++i)
        if (i.value().isNull())
            pe.remove(i.key());
        else
            pe.insert(i.key(), i.value());
    pr.setProcessEnvironment(pe);
    pr.setProcessChannelMode(QProcess::MergedChannels);
    pr.setStandardOutputFile(dir + QStringLiteral("/case.log"));
    pr.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--case"), ctx().id, dir});
    step(QStringLiteral("variant %1 in a fresh home").arg(variantName));
    if (!pr.waitForStarted(10000))
        return -1;
    waitFor([&pr] { return pr.state() == QProcess::NotRunning; }, 600000);
    if (pr.state() != QProcess::NotRunning) {
        pr.kill();
        pr.waitForFinished(5000);
    }
    const QString log = readText(dir + QStringLiteral("/case.log"));
    static const QRegularExpression re(QStringLiteral("\\n(\\d+) failure\\(s\\)"));
    const auto m = re.match(log);
    QStringList fails;
    for (const QString &l : log.split(QLatin1Char('\n')))
        if (l.startsWith(QLatin1String("FAIL")))
            fails << l.left(200);
    if (summary)
        *summary = fails.join(QLatin1Char('\n'));
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

bool realDrag(QWidget *from, const QPoint &a, QWidget *to, const QPoint &b)
{
    const QPoint ga = from->mapToGlobal(a), gb = to->mapToGlobal(b);
    QProcess x;
    const QPoint mid = (ga + gb) / 2;
    x.start(QStringLiteral("xdotool"), {QStringLiteral("mousemove"), QString::number(ga.x()), QString::number(ga.y()), QStringLiteral("sleep"), QStringLiteral("0.2"),
                                        QStringLiteral("mousedown"), QStringLiteral("1"), QStringLiteral("sleep"), QStringLiteral("0.2"),
                                        QStringLiteral("mousemove"), QString::number(ga.x() + 12), QString::number(ga.y() + 12), QStringLiteral("sleep"), QStringLiteral("0.3"),
                                        QStringLiteral("mousemove"), QString::number(mid.x()), QString::number(mid.y()), QStringLiteral("sleep"), QStringLiteral("0.3"),
                                        QStringLiteral("mousemove"), QString::number(gb.x()), QString::number(gb.y()), QStringLiteral("sleep"), QStringLiteral("0.4"),
                                        QStringLiteral("mouseup"), QStringLiteral("1")});
    if (!x.waitForStarted(5000))
        return false;
    waitFor([&x] { return x.state() == QProcess::NotRunning; }, 15000);
    settle(400);
    return x.exitCode() == 0;
}

QPoint binderPoint(MainWindow *w, BinderItem *it)
{
    BinderPanel *bp = binderOf(w);
    if (!bp || !it)
        return QPoint(-1, -1);
    bp->reveal(it);
    const QModelIndex ix = w->project()->model()->indexFor(it);
    bp->tree()->scrollTo(ix);
    settle(80);
    return bp->tree()->visualRect(ix).center();
}

void typeInto(QWidget *w, const QString &text)
{
    keysInto(w, text);
    settle(100);
}

} // namespace cs
