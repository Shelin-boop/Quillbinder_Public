// Scripted run for milestone M13 and the Scrivener project format (0.14.0):
// File > Project Format (work in Scrivener format, convert both ways, copies),
// New Project in Scrivener format, opening .scrivx files, the compile window
// (outputs, metadata, preview, format files) and the format editor, and an
// audit of every menu: texts, connections, enabled states, every command
// triggered with and without a project, the menu places the manual names.
// Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_m13_tests /tmp/shots
#include "compile/CompileFormat.h"
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "import/ScrivenerFormat.h"
#include "ui/AppSettings.h"
#include "ui/CompileDialog.h"
#include "ui/MainWindow.h"
#include "ui/StartDialog.h"
#include "ui/Theme.h"
#include "ui/TextEditor.h"

#include <QAction>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QCryptographicHash>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QTextBrowser>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFontComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPdfDocument>
#include <QPdfView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <functional>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(2000)));
    }
    std::fflush(stdout);
}

static void info(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
    std::fflush(stdout);
}

static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QString menuText(QString t)
{
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.remove(QLatin1Char('&'));
    t.remove(QChar(0x2026));
    t.remove(QLatin1String("..."));
    return t.trimmed();
}

static QAction *menuPath(QMainWindow *w, const QStringList &path)
{
    QList<QAction *> level = w->menuBar()->actions();
    QAction *found = nullptr;
    for (int i = 0; i < path.size(); ++i) {
        found = nullptr;
        for (QAction *a : level)
            if (menuText(a->text()) == path[i]) {
                found = a;
                break;
            }
        if (!found)
            return nullptr;
        if (i + 1 < path.size()) {
            if (!found->menu())
                return nullptr;
            emit found->menu()->aboutToShow();
            level = found->menu()->actions();
        }
    }
    return found;
}

static QAction *byKey(QWidget *w, const QString &key) { return w->findChild<QAction *>(key); }

// Runs fn on the modal window of type T that opens within delayMs (a timer
// started before the command that opens it).
template<typename T>
static void withModal(int delayMs, std::function<void(T *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        if (auto *m = qobject_cast<T *>(QApplication::activeModalWidget())) {
            fn(m);
            return;
        }
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<T *>(tl))
                if (d->isVisible()) {
                    fn(d);
                    return;
                }
        info(QStringLiteral("(the expected window (%1) did not open)").arg(QString::fromLatin1(T::staticMetaObject.className())));
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

// A message box: the button with this text is clicked
static void answer(int delayMs, const QString &buttonText, QString *seenText = nullptr)
{
    withModal<QMessageBox>(delayMs, [buttonText, seenText](QMessageBox *b) {
        if (seenText)
            *seenText = b->text();
        for (QAbstractButton *btn : b->buttons())
            if (menuText(btn->text()) == buttonText) {
                btn->click();
                return;
            }
        b->reject();
    });
}

// a file dialog of Qt (no desktop dialog in this run): name it and accept
static void fileDialog(int delayMs, const QString &path, std::function<void()> after = {})
{
    withModal<QFileDialog>(delayMs, [path, after](QFileDialog *d) {
        // selectFile() leaves the name field alone while it has the keyboard, so the field is filled in
        auto *name = d->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        if (d->fileMode() == QFileDialog::Directory) {
            d->setDirectory(path);
            if (name)
                name->setText(path);
        } else {
            d->setDirectory(QFileInfo(path).absolutePath());
            if (name)
                name->setText(QFileInfo(path).fileName());
            else
                d->selectFile(QFileInfo(path).fileName());
        }
        QMetaObject::invokeMethod(d, "accept", Qt::DirectConnection);
        if (after)
            QTimer::singleShot(300, qApp, after);
    });
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// links opened "in the desktop" are recorded here instead
class UrlRecorder : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void open(const QUrl &u) { urls << u; }
};

static UrlRecorder *g_urls = nullptr;

// ------------------------------------------------------------------ menu audit helpers

struct MenuEntry {
    QString path;     // "File > Project Format > Scrivener 3 Project (.scriv)"
    QPointer<QAction> action; // menus that are made anew when they open delete their old entries
    QMenu *menu;      // the menu it is in
    QList<QPointer<QAction>> chain; // the entries of the menus above it (menu bar first)
};

// usable: enabled, visible, and every menu above it can be opened
static bool usable(const MenuEntry &e)
{
    if (!e.action || !e.action->isEnabled() || !e.action->isVisible())
        return false;
    for (const QPointer<QAction> &m : e.chain)
        if (!m || !m->isEnabled() || !m->isVisible())
            return false;
    return true;
}

static QStringList g_sharedMenus;
static void collect(QMenu *menu, const QString &prefix, QList<MenuEntry> *out, QList<QPointer<QMenu>> *menus,
                    const QList<QPointer<QAction>> &chain)
{
    if (menus->contains(menu)) {
        // the same menu in a second place (opening it again would make its entries anew)
        g_sharedMenus << prefix;
        return;
    }
    emit menu->aboutToShow();
    menus->append(menu);
    for (QAction *a : menu->actions()) {
        if (a->isSeparator())
            continue;
        const QString path = prefix + QLatin1String(" > ") + menuText(a->text());
        if (a->menu())
            collect(a->menu(), path, out, menus, QList<QPointer<QAction>>(chain) << QPointer<QAction>(a));
        else
            out->append({path, a, menu, chain});
    }
}

static QList<MenuEntry> allEntries(QMainWindow *w, QList<QPointer<QMenu>> *menus)
{
    QList<MenuEntry> out;
    g_sharedMenus.clear();
    for (QAction *top : w->menuBar()->actions())
        if (top->menu())
            collect(top->menu(), menuText(top->text()), &out, menus, {QPointer<QAction>(top)});
    return out;
}

// What the window shows and holds: changes when a command did something.
static QByteArray stateSignature(MainWindow &w)
{
    QByteArray s = w.windowTitle().toUtf8() + w.saveState().toBase64() + QByteArray::number(w.isFullScreen());
    int visible = 0;
    for (QWidget *c : w.findChildren<QWidget *>())
        if (c->isVisible()) {
            ++visible;
            s += QByteArray::number(c->width()) + 'x' + QByteArray::number(c->height()) + ',';
        }
    s += QByteArray::number(visible);
    if (QWidget *f = QApplication::focusWidget())
        s += QByteArray(f->metaObject()->className()) + QByteArray::number(quintptr(f));
    for (QTextEdit *t : w.findChildren<QTextEdit *>())
        if (t->isVisible())
            s += QCryptographicHash::hash(t->toHtml().toUtf8(), QCryptographicHash::Md5) + QByteArray::number(t->textCursor().position())
                 + ':' + QByteArray::number(t->textCursor().anchor()) + ':' + QByteArray::number(t->font().pointSizeF())
                 + ':' + QByteArray::number(t->verticalScrollBar()->value()) + ':' + QByteArray::number(t->isReadOnly());
    for (QLineEdit *l : w.findChildren<QLineEdit *>())
        if (l->isVisible())
            s += l->text().toUtf8() + '|';
    for (QAbstractItemView *v : w.findChildren<QAbstractItemView *>())
        if (v->isVisible()) {
            s += QByteArray::number(v->currentIndex().row()) + v->currentIndex().data().toString().toUtf8();
            if (v->selectionModel())
                s += QByteArray::number(v->selectionModel()->selectedIndexes().size());
            if (v->model())
                s += QByteArray::number(v->model()->rowCount());
        }
    if (Project *p = w.project()) {
        for (BinderItem *it : p->allItems())
            s += it->id.toUtf8() + it->title.toUtf8() + (it->parent ? it->parent->id.toUtf8() : QByteArray()) + QByteArray::number(it->row())
                 + QByteArray::number(it->label) + QByteArray::number(it->status) + it->icon.toUtf8()
                 + QByteArray::number(it->includeInCompile) + QByteArray::number(int(it->type)) + QByteArray::number(it->hasSnapshots)
                 + QByteArray::number(it->scriptMode) + it->sectionType.toUtf8() + it->defaultTemplate.toUtf8()
                 + QByteArray::number(it->bookmarks.size());
        s += QByteArray::number(p->projectBookmarks.size());
        s += QByteArray::number(quintptr(p->formatting.styles.data())) + QByteArray::number(quintptr(p->formatting.script.data()))
             + QByteArray::number(p->formatting.fontSize) + p->formatting.fontFamily.toUtf8();
    }
    return QCryptographicHash::hash(s, QCryptographicHash::Md5);
}

// QObject::receivers is protected; the audit only reads it
struct ReceiverPeek : public QObject {
    int count(const char *signal) const { return receivers(signal); }
};
static int receiverCount(QObject *o, const char *signal) { return static_cast<ReceiverPeek *>(o)->count(signal); }

static bool isConnected(QAction *a)
{
    if (receiverCount(a, SIGNAL(triggered(bool))) > 0 || receiverCount(a, SIGNAL(toggled(bool))) > 0)
        return true;
    if (a->actionGroup() && receiverCount(a->actionGroup(), SIGNAL(triggered(QAction *))) > 0)
        return true;
    // a QMenu::addAction(text, receiver, slot) connects the triggered signal as well
    return false;
}

// Closes whatever the command opened: modal windows (their Cancel), menus,
// and top-level windows that were not there before.
static QStringList g_modalsSeen;
static void dismissModals()
{
    for (int round = 0; round < 6; ++round) {
        QWidget *m = QApplication::activeModalWidget();
        if (!m) {
            if (QWidget *p = QApplication::activePopupWidget()) {
                g_modalsSeen << QStringLiteral("popup:") + QString::fromLatin1(p->metaObject()->className());
                p->close();
            }
            return;
        }
        g_modalsSeen << QString::fromLatin1(m->metaObject()->className()) + QLatin1Char(':') + m->windowTitle();
        if (auto *mb = qobject_cast<QMessageBox *>(m)) {
            // the safe answer: Cancel / No / Close
            QAbstractButton *safe = nullptr;
            for (QAbstractButton *b : mb->buttons()) {
                const auto role = mb->buttonRole(b);
                if (role == QMessageBox::RejectRole || role == QMessageBox::NoRole)
                    safe = b;
            }
            if (safe)
                safe->click();
            else
                mb->reject();
        } else if (auto *d = qobject_cast<QDialog *>(m)) {
            d->reject();
        } else {
            m->close();
        }
        QCoreApplication::processEvents();
    }
}

// A window that stays open for long (nothing in the run answers it) is closed,
// so that the run cannot hang; it is reported as a failure.
static void startWatchdog()
{
    static QPointer<QWidget> last;
    static QElapsedTimer since;
    auto *t = new QTimer(qApp);
    t->setInterval(500);
    QObject::connect(t, &QTimer::timeout, qApp, [] {
        QWidget *m = QApplication::activeModalWidget();
        if (m != last) {
            last = m;
            since.start();
            return;
        }
        if (m && since.elapsed() > 7000) {
            QString text = m->windowTitle();
            if (auto *b = qobject_cast<QMessageBox *>(m))
                text += QLatin1String(": ") + b->text();
            check(false, QStringLiteral("a window was left open (closed by the run)"),
                  QString::fromLatin1(m->metaObject()->className()) + QLatin1Char(' ') + text);
            if (auto *d = qobject_cast<QDialog *>(m))
                d->reject();
            else
                m->close();
            last = nullptr;
        }
    });
    t->start();
}

// Every control of the format editor changes the format (a control that is
// not connected to anything would be found here). Each control is changed in
// a dialog of its own, from a format that has one style override and one
// replacement, and the result is compared with that of an unchanged dialog.
static void auditFormatEditor(Project *p, QWidget *parent)
{
    compile::Format f0 = compile::formatByName(QStringLiteral("Modern"));
    f0.builtin = false;
    f0.name = QStringLiteral("Audit Format");
    compile::StyleOverride so;
    so.style = QStringLiteral("Block Quote");
    so.mode = compile::StyleOverride::Restyle;
    f0.styleOverrides << so;
    compile::Replacement rp;
    rp.find = QStringLiteral("colour");
    rp.replace = QStringLiteral("color");
    f0.replacements << rp;
    auto json = [](const compile::Format &f) { return QJsonDocument(f.toJson()).toJson(QJsonDocument::Compact); };
    QByteArray j0;
    QStringList names;
    {
        FormatEditorDialog d(f0, p, parent);
        for (QWidget *c : d.findChildren<QWidget *>())
            if (c->objectName().startsWith(QLatin1String("fmt_")) && !qobject_cast<QTextBrowser *>(c) && !qobject_cast<QListWidget *>(c)
                && !qobject_cast<QTableWidget *>(c))
                names << c->objectName();
        d.accept();
        j0 = json(d.format());
    }
    names.removeDuplicates();
    {
        FormatEditorDialog d(f0, p, parent);
        d.accept();
        check(json(d.format()) == j0, QStringLiteral("format editor: OK without changes gives the same format"));
    }
    QStringList dead, skipped, changed;
    for (const QString &n : names) {
        FormatEditorDialog d(f0, p, parent);
        QWidget *c = d.findChild<QWidget *>(n);
        if (!c->isEnabled()) {
            skipped << n;
            continue;
        }
        if (auto *b = qobject_cast<QCheckBox *>(c)) {
            if (b->isTristate())
                b->setCheckState(b->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            else
                b->setChecked(!b->isChecked());
        } else if (auto *fc = qobject_cast<QFontComboBox *>(c)) {
            fc->setCurrentFont(QFont(fc->currentFont().family() == QLatin1String("DejaVu Sans Mono") ? QStringLiteral("DejaVu Serif")
                                                                                                     : QStringLiteral("DejaVu Sans Mono")));
        } else if (auto *cb = qobject_cast<QComboBox *>(c)) {
            if (cb->count() < 2) {
                skipped << n;
                continue;
            }
            const int i = (cb->currentIndex() + 1) % cb->count();
            cb->setCurrentIndex(i);
            emit cb->activated(i);
        } else if (auto *sb = qobject_cast<QSpinBox *>(c)) {
            sb->setValue(sb->value() + sb->singleStep() <= sb->maximum() ? sb->value() + sb->singleStep() : sb->value() - sb->singleStep());
        } else if (auto *ds = qobject_cast<QDoubleSpinBox *>(c)) {
            const double step = qMax(ds->singleStep(), 1.0);
            ds->setValue(ds->value() + step <= ds->maximum() ? ds->value() + step : ds->value() - step);
        } else if (auto *le = qobject_cast<QLineEdit *>(c)) {
            le->setText(le->text() + QStringLiteral("x"));
            emit le->editingFinished();
        } else if (auto *pb = qobject_cast<QAbstractButton *>(c)) {
            if (n == QLatin1String("fmt_styleColor")) {
                skipped << n; // opens the color window
                continue;
            }
            pb->click();
        } else {
            skipped << n;
            continue;
        }
        d.accept();
        // an added replacement is an empty row until it is filled in (checked above with a filled row)
        if (json(d.format()) != j0)
            changed << n;
        else if (n == QLatin1String("fmt_addReplacement"))
            skipped << n;
        else
            dead << n;
    }
    info(QStringLiteral("format editor: %1 controls change the format; not tried (off or a window of their own): %2")
             .arg(changed.size())
             .arg(skipped.join(QLatin1String(", "))));
    check(dead.isEmpty() && changed.size() > 60, QStringLiteral("format editor: every control changes the format"),
          QStringLiteral("without effect: ") + dead.join(QLatin1String(", ")));
    // the paper list: Modern is A4; A5 sets 148 x 210 mm
    {
        FormatEditorDialog d(f0, p, parent);
        auto *paper = d.findChild<QComboBox *>(QStringLiteral("fmt_paper"));
        const bool a4 = paper->currentText() == QLatin1String("A4");
        const int a5 = paper->findText(QStringLiteral("A5"));
        paper->setCurrentIndex(a5);
        emit paper->activated(a5);
        d.accept();
        check(a4 && qAbs(d.format().page.widthMm - 148) < 0.01 && qAbs(d.format().page.heightMm - 210) < 0.01,
              QStringLiteral("format editor: the paper list shows A4 and sets A5"),
              QStringLiteral("%1 %2 x %3").arg(a4).arg(d.format().page.widthMm).arg(d.format().page.heightMm));
    }
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderM13Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderM13Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    // Qt's own file dialogs, so that the run can fill them in
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    UrlRecorder urls;
    g_urls = &urls;
    for (const char *scheme : {"file", "http", "https", "mailto"})
        QDesktopServices::setUrlHandler(QLatin1String(scheme), &urls, "open");

    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.forename = QStringLiteral("Ann");
    as.surname = QStringLiteral("Keeper");
    as.save();
    theme::apply();
    // the add-ons of the build (packaging/build-addons.sh <build> <build>/addons-dist), so that their menu entries are audited too
    {
        const QDir dist(QStringLiteral(QB_TEST_BUILD_DIR "/addons-dist"));
        QStringList installed, problems;
        for (const QString &f : dist.entryList({QStringLiteral("*.qbpkg")}, QDir::Files, QDir::Name)) {
            QStringList log;
            QString err;
            bool restart = false;
            const addons::PackageInfo pi = addons::inspect(dist.filePath(f));
            if (pi.isValid() && addons::install(dist.filePath(f), &log, &err, &restart))
                installed << pi.manifest.id;
            else
                problems << f + QLatin1String(": ") + pi.error + err;
        }
        info(QStringLiteral("add-ons installed: %1").arg(installed.isEmpty() ? QStringLiteral("none (not built)") : installed.join(QLatin1String(", "))));
        check(problems.isEmpty(), QStringLiteral("(the add-ons of the build install)"), problems.join(QLatin1String("; ")));
    }
    compile::PluginManager::instance().load();

    QTemporaryDir work;
    const QString W = work.path();
    startWatchdog();

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);

    // =========================================================== the menu entries
    {
        QAction *fmt = menuPath(&w, {QStringLiteral("File"), QStringLiteral("Project Format")});
        QStringList texts;
        if (fmt && fmt->menu())
            for (QAction *a : fmt->menu()->actions())
                if (!a->isSeparator())
                    texts << menuText(a->text());
        check(texts == QStringList{QStringLiteral("Quillbinder Project"), QStringLiteral("Scrivener 3 Project (.scriv)"),
                                   QStringLiteral("Save a Copy as Scrivener Project"), QStringLiteral("Save a Copy as Quillbinder Project")},
              QStringLiteral("File > Project Format: own format, Scrivener 3 format, two kinds of copies"), texts.join(QLatin1String(" | ")));
        check(fmt && fmt->menu() && byKey(&w, QStringLiteral("formatScrivener"))->isCheckable()
                  && byKey(&w, QStringLiteral("formatScrivener"))->actionGroup()
                  && byKey(&w, QStringLiteral("formatScrivener"))->actionGroup()->isExclusive(),
              QStringLiteral("the two formats are a choice of one (radio entries)"));
        bool off = true;
        for (const char *k : {"formatQuillbinder", "formatScrivener", "saveCopyScrivener", "saveCopyQuillbinder"})
            off = off && !byKey(&w, QLatin1String(k))->isEnabled();
        check(off, QStringLiteral("without a project the format entries are off"));
        check(menuPath(&w, {QStringLiteral("File"), QStringLiteral("Export"), QStringLiteral("Scrivener Project")}) != nullptr
                  && menuPath(&w, {QStringLiteral("File"), QStringLiteral("Import"), QStringLiteral("Scrivener Project")}) != nullptr,
              QStringLiteral("File > Export > Scrivener Project and File > Import > Scrivener Project"));
    }

    // =========================================================== new project in Scrivener format
    QString scrivNew = QDir(W).filePath(QStringLiteral("Harbour Lights.scriv"));
    {
        withModal<StartDialog>(500, [W](StartDialog *d) {
            auto *combo = d->findChild<QComboBox *>(QStringLiteral("projectFormat"));
            if (auto *e = d->findChild<QLineEdit *>(QStringLiteral("projectName")))
                e->setText(QStringLiteral("Harbour Lights"));
            if (auto *e = d->findChild<QLineEdit *>(QStringLiteral("projectLocation")))
                e->setText(W);
            check(combo && combo->count() == 2 && combo->itemText(1).contains(QLatin1String("Scrivener")),
                  QStringLiteral("New Project: a Format list with the Scrivener format"));
            if (combo)
                combo->setCurrentIndex(1);
            shot(d, QStringLiteral("m13-new-project-format"));
            for (QPushButton *b : d->findChildren<QPushButton *>())
                if (b->text() == QLatin1String("Create"))
                    b->click();
        });
        w.showStartDialog(false);
        settle(800);
        Project *p = w.project();
        check(p && p->scrivenerPath == QFileInfo(scrivNew).absoluteFilePath() && QFile::exists(scrivener::scrivxIn(scrivNew)),
              QStringLiteral("New Project in Scrivener format: the .scriv is made and the project is saved there"),
              p ? p->scrivenerPath : QStringLiteral("no project"));
        check(!QDir(QDir(W).filePath(QStringLiteral("Harbour Lights.quillbinder"))).exists(),
              QStringLiteral("New Project in Scrivener format: no project folder of the own format beside it"));
        check(w.windowTitle().contains(QLatin1String("(Scrivener format)")), QStringLiteral("the window title says \"Scrivener format\""),
              w.windowTitle());
        check(byKey(&w, QStringLiteral("formatScrivener"))->isChecked() && !byKey(&w, QStringLiteral("formatQuillbinder"))->isChecked(),
              QStringLiteral("File > Project Format ticks Scrivener 3"));
        check(AppSettings::instance().recentProjects.value(0) == scrivener::scrivxIn(scrivNew),
              QStringLiteral("Open Recent lists the .scrivx"), AppSettings::instance().recentProjects.value(0));
        if (p) {
            BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Beacon"));
            QTextCursor c(p->docs()->text(it->id));
            c.insertText(QStringLiteral("The beacon was lit at dusk."));
            settle(100);
            QString err;
            p->save(&err);
            const QString content = scrivNew + QStringLiteral("/Files/Data/") + it->id + QStringLiteral("/content.rtf");
            check(readAll(content).contains("beacon was lit"), QStringLiteral("saving writes the new document into the .scriv"), err);
            urls.urls.clear();
            byKey(&w, QStringLiteral("showProjectFolder"))->trigger();
            settle(100);
            check(!urls.urls.isEmpty() && QFileInfo(urls.urls.last().toLocalFile()) == QFileInfo(scrivNew),
                  QStringLiteral("Show Project in File Manager shows the .scriv"),
                  urls.urls.isEmpty() ? QString() : urls.urls.last().toString());
            shot(&w, QStringLiteral("m13-scrivener-format-window"));
        }
        w.closeProject();
        settle(200);
        // open it again: no question, the working copy is used
        QString seen;
        answer(800, QStringLiteral("Cancel"), &seen);
        const bool opened = w.openProject(scrivener::scrivxIn(scrivNew));
        settle(900);
        check(opened && w.project() && w.project()->scrivenerPath == QFileInfo(scrivNew).absoluteFilePath() && seen.isEmpty(),
              QStringLiteral("opening the .scrivx again: straight into Scrivener format, no question"), seen);
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    }

    // =========================================================== a Scrivener project from elsewhere
    QString foreign = QDir(W).filePath(QStringLiteral("Fog Bell.scriv"));
    QString foreign2 = QDir(W).filePath(QStringLiteral("Second Bell.scriv"));
    {
        QString err;
        auto src = Project::create(W, QStringLiteral("Fog Bell Source"), &err);
        BinderItem *it = src->model()->createItem(src->draft(), -1, ItemType::Text, QStringLiteral("Bell"));
        QTextCursor(src->docs()->text(it->id)).insertText(QStringLiteral("The bell rang in the fog."));
        src->save(&err);
        scrivener::WriteReport rep;
        check(scrivener::saveCopyAsScrivener(src.get(), foreign, &rep, &err) && scrivener::saveCopyAsScrivener(src.get(), foreign2, &rep, &err),
              QStringLiteral("(two Scrivener projects made for the next checks)"), err);
        src.reset();
        w.closeProject();
        settle(200);
        QString text;
        withModal<QMessageBox>(600, [&text](QMessageBox *b) {
            text = b->text();
            shot(b, QStringLiteral("m13-open-scrivener-question"));
            for (QAbstractButton *btn : b->buttons())
                if (btn->objectName() == QLatin1String("workInScrivener"))
                    btn->click();
        });
        w.openProject(QDir(foreign).filePath(QStringLiteral("Fog Bell.scrivx")));
        settle(900);
        bool hasText = false;
        if (w.project())
            for (BinderItem *x : w.project()->allItems())
                hasText |= w.project()->docs()->plainText(x->id).contains(QLatin1String("rang in the fog"));
        check(text.contains(QLatin1String("Scrivener project")) && w.project() && !w.project()->scrivenerPath.isEmpty() && hasText,
              QStringLiteral("File > Open of a .scrivx: asks, \"Work in Scrivener Format\" opens it"), text.left(200));
        // the other one: converted into a project of our own
        w.closeProject();
        settle(200);
        withModal<QMessageBox>(600, [](QMessageBox *b) {
            for (QAbstractButton *btn : b->buttons())
                if (btn->objectName() == QLatin1String("convertScrivener"))
                    btn->click();
        });
        withModal<QDialog>(1600, [W](QDialog *d) {
            for (QLineEdit *e : d->findChildren<QLineEdit *>())
                if (e->text() == QLatin1String("Second Bell"))
                    e->setText(QStringLiteral("Second Bell Converted"));
                else if (QDir(e->text()).exists())
                    e->setText(W);
            for (QPushButton *b : d->findChildren<QPushButton *>())
                if (b->text() == QLatin1String("Import"))
                    b->click();
        });
        QString report;
        answer(3200, QStringLiteral("OK"), &report);
        w.openProject(foreign2);
        settle(3500);
        check(w.project() && w.project()->scrivenerPath.isEmpty()
                  && QFileInfo(w.project()->folderPath()).fileName() == QLatin1String("Second Bell Converted.quillbinder"),
              QStringLiteral("\"Convert…\" makes a project of its own from the .scriv"),
              w.project() ? w.project()->folderPath() : QStringLiteral("none"));
        // Scrivener has it open: the question, "No" keeps it closed
        QFile lock(foreign + QStringLiteral("/Files/user.lock"));
        lock.open(QIODevice::WriteOnly);
        lock.write("platform=win\nhost=writing-desk\nuser=Ann\napp=Scrivener\n");
        lock.close();
        QString lockText;
        answer(600, QStringLiteral("No"), &lockText);
        const bool openedLocked = w.openProject(foreign);
        settle(400);
        check(!openedLocked && lockText.contains(QLatin1String("writing-desk")) && w.project()
                  && w.project()->scrivenerPath.isEmpty(),
              QStringLiteral("a .scriv that Scrivener has open: a warning names the computer, \"No\" leaves it closed"), lockText.left(200));
        QFile::remove(lock.fileName());
    }

    // =========================================================== converting the open project
    {
        Project *p = w.project();
        const QString ownFolder = p ? p->folderPath() : QString();
        const QString target = QDir(W).filePath(QStringLiteral("Converted.scriv"));
        fileDialog(500, target);
        QString seen;
        answer(2500, QStringLiteral("OK"), &seen);
        byKey(&w, QStringLiteral("formatScrivener"))->trigger();
        settle(2800);
        check(w.project() && w.project()->scrivenerPath == QFileInfo(target).absoluteFilePath() && QFile::exists(scrivener::scrivxIn(target))
                  && QDir(ownFolder).exists() && byKey(&w, QStringLiteral("formatScrivener"))->isChecked(),
              QStringLiteral("Project Format > Scrivener 3: the project is saved as .scriv from now on, the old folder stays"),
              seen.left(300));
        // and back
        const QString backParent = QDir(W).filePath(QStringLiteral("back"));
        QDir().mkpath(backParent);
        fileDialog(500, backParent);
        answer(2500, QStringLiteral("OK"));
        byKey(&w, QStringLiteral("formatQuillbinder"))->trigger();
        settle(2800);
        check(w.project() && w.project()->scrivenerPath.isEmpty() && w.project()->folderPath().startsWith(backParent)
                  && QFile::exists(scrivener::scrivxIn(target)) && byKey(&w, QStringLiteral("formatQuillbinder"))->isChecked(),
              QStringLiteral("Project Format > own format: a project folder again, the .scriv stays as it was"),
              w.project() ? w.project()->folderPath() : QString());
        // a canceled file dialog changes nothing (and the tick stays where it was)
        withModal<QFileDialog>(500, [](QFileDialog *d) { d->reject(); });
        byKey(&w, QStringLiteral("formatScrivener"))->trigger();
        settle(900);
        check(w.project() && w.project()->scrivenerPath.isEmpty() && byKey(&w, QStringLiteral("formatQuillbinder"))->isChecked()
                  && !byKey(&w, QStringLiteral("formatScrivener"))->isChecked(),
              QStringLiteral("canceling the switch: nothing changes, the tick goes back"));
        // copies
        const QString copyScriv = QDir(W).filePath(QStringLiteral("Copy For Scrivener.scriv"));
        fileDialog(500, copyScriv);
        answer(2000, QStringLiteral("OK"));
        QAction *exportAlias = menuPath(&w, {QStringLiteral("File"), QStringLiteral("Export"), QStringLiteral("Scrivener Project")});
        if (exportAlias)
            exportAlias->trigger();
        settle(2300);
        check(QFile::exists(scrivener::scrivxIn(copyScriv)) && w.project()->scrivenerPath.isEmpty(),
              QStringLiteral("File > Export > Scrivener Project: a copy in Scrivener format, the project stays as it is"));
        const QString copyParent = QDir(W).filePath(QStringLiteral("copies"));
        QDir().mkpath(copyParent);
        fileDialog(500, copyParent);
        answer(2000, QStringLiteral("OK"));
        byKey(&w, QStringLiteral("saveCopyQuillbinder"))->trigger();
        settle(2300);
        check(!QDir(copyParent).entryList({QStringLiteral("*.quillbinder")}, QDir::Dirs).isEmpty(),
              QStringLiteral("Save a Copy as Quillbinder Project: a project folder in the chosen place"));
    }

    // =========================================================== compile window
    {
        Project *p = w.project();
        QStringList names;
        for (ExporterInterface *e : compile::PluginManager::instance().exporters())
            names << e->name();
        check(names.contains(QStringLiteral("PDF (.pdf)")) && names.contains(QStringLiteral("E-book (.epub)")),
              QStringLiteral("Compile for: PDF and E-book are built in"), names.join(QLatin1String(", ")));
        CompileDialog dlg(p, &w);
        dlg.show();
        settle(300);
        shot(&dlg, QStringLiteral("m13-compile-window"));
        // metadata: a tab of the compile window since 0.21 (was a window of its own)
        {
            dlg.editMetadata();
            settle(200);
            auto set = [&dlg](const char *name, const QString &v) {
                if (auto *e = dlg.findChild<QLineEdit *>(QLatin1String(name)))
                    e->setText(v);
            };
            set("meta_subtitle", QStringLiteral("A Tale of the Coast"));
            set("meta_publisher", QStringLiteral("Harbour Press"));
            set("meta_language", QStringLiteral("en-GB"));
            if (auto *e = dlg.findChild<QPlainTextEdit *>(QStringLiteral("meta_description")))
                e->setPlainText(QStringLiteral("A keeper and a storm."));
            shot(&dlg, QStringLiteral("m13-compile-metadata"));
        }
        // preview (it keeps the settings, among them the metadata typed above)
        dlg.findChild<QPushButton *>(QStringLiteral("compilePreview"))->click();
        settle(1200);
        check(p->compile.publisher == QLatin1String("Harbour Press") && p->compile.language == QLatin1String("en-GB")
                  && p->compile.description == QLatin1String("A keeper and a storm."),
              QStringLiteral("Compile > Metadata: the details are kept with the project"));
        QDialog *pv = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (tl->objectName() == QLatin1String("compilePreviewWindow") && tl->isVisible())
                pv = qobject_cast<QDialog *>(tl);
        QPdfView *view = pv ? pv->findChild<QPdfView *>() : nullptr;
        check(view && view->document() && view->document()->pageCount() >= 1, QStringLiteral("Compile > Preview: the pages as PDF in a window"),
              view && view->document() ? QString::number(view->document()->pageCount()) : QStringLiteral("no view"));
        if (pv) {
            shot(pv, QStringLiteral("m13-compile-preview"));
            pv->close();
        }
        // the format editor
        auto *formats = dlg.findChild<QListWidget *>();
        for (int i = 0; formats && i < formats->count(); ++i)
            if (formats->item(i)->text() == QLatin1String("Modern"))
                formats->setCurrentRow(i);
        withModal<FormatEditorDialog>(500, [](FormatEditorDialog *e) {
            check(e->sectionCount() == 9, QStringLiteral("format editor: nine sections"), QString::number(e->sectionCount()));
            auto *name = e->findChild<QLineEdit *>(QStringLiteral("fmt_name"));
            name->setText(QStringLiteral("My Modern"));
            QStringList sectionShots = {QStringLiteral("text"), QStringLiteral("layouts"), QStringLiteral("assignments"),
                                        QStringLiteral("styles"), QStringLiteral("page"), QStringLiteral("notes"),
                                        QStringLiteral("replacements"), QStringLiteral("transformations"), QStringLiteral("contents")};
            for (int i = 0; i < e->sectionCount(); ++i) {
                e->showSection(i);
                shot(e, QStringLiteral("m13-format-editor-") + sectionShots.value(i));
            }
            // a style override, a replacement, the contents
            auto *add = e->findChild<QComboBox *>(QStringLiteral("fmt_addStyleOverride"));
            const int verse = add->findText(QStringLiteral("Block Quote"));
            add->setCurrentIndex(verse);
            emit add->activated(verse);
            e->findChild<QComboBox *>(QStringLiteral("fmt_styleMode"))->setCurrentIndex(1);
            e->findChild<QCheckBox *>(QStringLiteral("fmt_styleItalic"))->setCheckState(Qt::Checked);
            e->findChild<QLineEdit *>(QStringLiteral("fmt_stylePrefix"))->setText(QStringLiteral("> "));
            e->findChild<QPushButton *>(QStringLiteral("fmt_addReplacement"))->click();
            auto *table = e->findChild<QTableWidget *>(QStringLiteral("fmt_replacements"));
            table->item(table->rowCount() - 1, 1)->setText(QStringLiteral("colour"));
            table->item(table->rowCount() - 1, 2)->setText(QStringLiteral("color"));
            e->findChild<QCheckBox *>(QStringLiteral("fmt_toc"))->setChecked(true);
            e->findChild<QCheckBox *>(QStringLiteral("fmt_smartQuotes"))->setChecked(true);
            e->findChild<QCheckBox *>(QStringLiteral("fmt_rightPage"))->setChecked(true);
            e->findChild<QLineEdit *>(QStringLiteral("fmt_footerLeft"))->setText(QStringLiteral("<$p>"));
            e->accept();
        });
        dlg.findChild<QPushButton *>(QStringLiteral("formatDuplicate"))->click();
        settle(800);
        const compile::Format mine = compile::formatByName(QStringLiteral("My Modern"));
        check(mine.name == QLatin1String("My Modern") && mine.toc && mine.transform.smartQuotes && mine.page.chaptersOnRightPage
                  && mine.page.footerLeft == QLatin1String("<$p>") && mine.replacements.size() == 1
                  && mine.replacements[0].find == QLatin1String("colour") && mine.styleOverrides.size() == 1
                  && mine.styleOverrides[0].style == QLatin1String("Block Quote") && mine.styleOverrides[0].italic == 1
                  && mine.styleOverrides[0].prefix == QLatin1String("> "),
              QStringLiteral("format editor: the new settings are saved with the format"),
              QStringLiteral("%1 toc %2 repl %3 styles %4").arg(mine.name).arg(mine.toc).arg(mine.replacements.size()).arg(mine.styleOverrides.size()));
        auditFormatEditor(p, &dlg);
        // export and import of a format file
        const QString ff = QDir(W).filePath(QStringLiteral("my-modern.qbformat"));
        fileDialog(400, ff);
        dlg.findChild<QPushButton *>(QStringLiteral("formatExport"))->click();
        settle(700);
        check(QFile::exists(ff) && readAll(ff).contains("quillbinder-compile-format"), QStringLiteral("Compile > Export…: the format as a file"));
        fileDialog(400, ff);
        dlg.findChild<QPushButton *>(QStringLiteral("formatImport"))->click();
        settle(700);
        bool imported = false;
        for (int i = 0; formats && i < formats->count(); ++i)
            imported |= formats->item(i)->text() == QLatin1String("My Modern (2)");
        check(imported, QStringLiteral("Compile > Import…: the format comes in under a name of its own"));
        // compile to PDF and to an e-book
        auto *exporter = dlg.findChild<QComboBox *>(QStringLiteral("compileExporter"));
        for (const QString &id : {QStringLiteral("pdf"), QStringLiteral("epub")}) {
            exporter->setCurrentIndex(exporter->findData(id));
            const QString out = QDir(W).filePath(QStringLiteral("book.") + id);
            fileDialog(400, out);
            answer(2500, QStringLiteral("Close"));
            dlg.findChild<QPushButton *>(QStringLiteral("compileNow"))->click();
            settle(2800);
            check(QFile::exists(out) && QFileInfo(out).size() > 1000, QStringLiteral("Compile… to %1: the file is written").arg(id.toUpper()));
            dlg.show();
        }
        check(!p->compile.identifier.isEmpty(), QStringLiteral("the e-book got an identifier that stays with the project"));
        dlg.close();
    }

    // =========================================================== menu audit
    {
        QList<QPointer<QMenu>> menus;
        const QList<MenuEntry> entries = allEntries(&w, &menus);
        info(QStringLiteral("menu audit: %1 entries in %2 menus").arg(entries.size()).arg(menus.size()));
        // texts
        QStringList noText, dupText, unconnected, emptyMenus, mnemonicDup;
        info(QStringLiteral("menus that are in two places: %1").arg(g_sharedMenus.join(QLatin1String("; "))));
        for (const MenuEntry &e : entries)
            if (e.action && menuText(e.action->text()).isEmpty())
                noText << e.path;
        for (QMenu *m : menus) {
            if (!m)
                continue;
            QSet<QString> seen;
            QHash<QChar, QString> keys;
            int visible = 0;
            for (QAction *a : m->actions()) {
                if (a->isSeparator())
                    continue;
                if (a->isVisible())
                    ++visible;
                const QString t = menuText(a->text());
                if (seen.contains(t))
                    dupText << menuText(m->title()) + QLatin1String(" > ") + t;
                seen.insert(t);
                const int amp = int(a->text().indexOf(QLatin1Char('&')));
                if (amp >= 0 && amp + 1 < a->text().size() && a->text().at(amp + 1) != QLatin1Char('&') && a->isVisible()) {
                    const QChar k = a->text().at(amp + 1).toLower();
                    if (keys.contains(k))
                        mnemonicDup << QStringLiteral("%1: %2 / %3 (%4)").arg(menuText(m->title()), keys.value(k), t, k);
                    keys.insert(k, t);
                }
            }
            if (visible == 0)
                emptyMenus << menuText(m->title());
        }
        for (const MenuEntry &e : entries)
            if (e.action && !isConnected(e.action) && !e.action->property("qbAliasOf").isValid() && e.action->isEnabled())
                unconnected << e.path;
        check(noText.isEmpty(), QStringLiteral("menu audit: every entry has a text"), noText.join(QLatin1String("; ")));
        check(dupText.isEmpty(), QStringLiteral("menu audit: no entry twice in one menu"), dupText.join(QLatin1String("; ")));
        check(emptyMenus.isEmpty(), QStringLiteral("menu audit: no empty menu"), emptyMenus.join(QLatin1String("; ")));
        check(unconnected.isEmpty(), QStringLiteral("menu audit: every enabled entry does something (is connected)"),
              unconnected.join(QLatin1String("; ")));
        check(mnemonicDup.isEmpty(), QStringLiteral("menu audit: no letter for Alt used twice in one menu"), mnemonicDup.join(QLatin1String("; ")));
        // the menu bar's own letters
        QSet<QChar> top;
        bool topUnique = true;
        for (QAction *a : w.menuBar()->actions()) {
            const int amp = int(a->text().indexOf(QLatin1Char('&')));
            if (amp < 0)
                continue;
            const QChar k = a->text().at(amp + 1).toLower();
            topUnique = topUnique && !top.contains(k);
            top.insert(k);
        }
        check(topUnique, QStringLiteral("menu audit: the menu bar's Alt letters are unique"));
        // shortcuts used twice
        QHash<QString, QString> shortcuts;
        QStringList clash;
        for (QAction *a : w.findChildren<QAction *>()) {
            if (a->property("qbAliasOf").isValid())
                continue;
            for (const QKeySequence &k : a->shortcuts()) {
                const QString ks = k.toString();
                if (ks.isEmpty())
                    continue;
                if (shortcuts.contains(ks) && shortcuts.value(ks) != a->objectName())
                    clash << QStringLiteral("%1: %2 / %3").arg(ks, shortcuts.value(ks), a->objectName());
                shortcuts.insert(ks, a->objectName());
            }
        }
        check(clash.isEmpty(), QStringLiteral("menu audit: no shortcut belongs to two commands"), clash.join(QLatin1String("; ")));

        // the menu places the manual names
        QString manual = QString::fromUtf8(readAll(QStringLiteral(QB_TEST_SOURCE_DIR "/../docs/manual.md")));
        static const QRegularExpression pathRe(QStringLiteral("\\*\\*([A-Z][^*\\n]*? > [^*\\n]+)\\*\\*"));
        QStringList missing;
        QSet<QString> checkedPaths;
        auto it = pathRe.globalMatch(manual);
        while (it.hasNext()) {
            const QString raw = it.next().captured(1);
            if (raw.contains(QLatin1Char('"')))
                continue; // a sentence in bold (troubleshooting), not a menu place
            QStringList parts = raw.split(QStringLiteral(" > "));
            for (QString &x : parts)
                x = menuText(x);
            if (checkedPaths.contains(parts.join(QLatin1Char('>'))))
                continue;
            checkedPaths.insert(parts.join(QLatin1Char('>')));
            // only paths that start with a menu of the menu bar; they may end in a window's page
            if (!menuPath(&w, {parts.first()}))
                continue;
            QList<QAction *> level = w.menuBar()->actions();
            for (int i = 0; i < parts.size(); ++i) {
                QAction *found = nullptr;
                // "Bigger / Smaller": two entries; "Script Mode" for "Script Mode - Screenplay"
                const QStringList alternatives = i + 1 == parts.size() ? parts[i].split(QStringLiteral(" / ")) : QStringList{parts[i]};
                int foundCount = 0;
                for (const QString &alt : alternatives)
                    for (QAction *a : level) {
                        const QString t = menuText(a->text());
                        if (t == alt || t.startsWith(alt + QLatin1String(" - "))) {
                            found = a;
                            ++foundCount;
                            break;
                        }
                    }
                if (foundCount < alternatives.size())
                    found = nullptr;
                if (!found) {
                    missing << raw;
                    break;
                }
                if (!found->menu())
                    break; // a command; what follows is a page of its window
                emit found->menu()->aboutToShow();
                level = found->menu()->actions();
            }
        }
        info(QStringLiteral("manual: %1 menu places checked").arg(checkedPaths.size()));
        check(missing.isEmpty(), QStringLiteral("every menu place the manual names is in the menus"), missing.join(QLatin1String("; ")));

        // every command once, with the project open, on a text with a selected word in the
        // editor that has the keyboard: no crash, the window still there, and something happens
        // ("welcome" opens another project: checked by qb_v040_tests)
        const QSet<QString> skip = {QStringLiteral("quit"), QStringLiteral("closeProject"), QStringLiteral("formatQuillbinder"),
                                    QStringLiteral("formatScrivener"), QStringLiteral("welcome")};
        // a text with "bell" outside the trash (commands such as Split or Move to Trash change it)
        auto ensureText = [&w]() -> QString {
            Project *p = w.project();
            for (BinderItem *x : p->allItems())
                if (x->hasText() && !p->isInTrash(x) && p->docs()->plainText(x->id).contains(QLatin1String("bell")))
                    return x->id;
            BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Audit"));
            QTextCursor(p->docs()->text(it->id)).insertText(QStringLiteral("The bell rang in the fog. The fog stayed."));
            return it->id;
        };
        QString textId;
        auto prepare = [&] {
            if (!w.project())
                return;
            if (w.isFullScreen())
                w.showNormal();
            textId = ensureText();
            w.toolShowDocument(textId);
            settle(150);
            QApplication::setActiveWindow(&w);
            for (TextEditor *ed : w.findChildren<TextEditor *>())
                if (ed->isVisible() && !ed->isReadOnly() && ed->toPlainText().contains(QLatin1String("bell"))) {
                    ed->setFocus();
                    QTextCursor c = ed->textCursor();
                    const int at = int(ed->toPlainText().indexOf(QLatin1String("bell")));
                    c.setPosition(at);
                    c.setPosition(at + 4, QTextCursor::KeepAnchor);
                    ed->setTextCursor(c);
                    break;
                }
            settle(40);
        };
        QStringList noEffect, tried;
        QTimer dismiss;
        dismiss.setInterval(120);
        QObject::connect(&dismiss, &QTimer::timeout, qApp, dismissModals);
        dismiss.start();
        for (const MenuEntry &e : entries) {
            QAction *a = e.action;
            if (!usable(e) || skip.contains(a->objectName()) || e.path.startsWith(QLatin1String("File > Open Recent")))
                continue;
            if (!w.project())
                break;
            prepare();
            if (!a->isEnabled())
                continue; // it needs another place (the binder, a corkboard ...)
            const QWidgetList tops = QApplication::topLevelWidgets();
            const QSet<QWidget *> before(tops.begin(), tops.end());
            const bool wasChecked = a->isChecked();
            const int modalsBefore = g_modalsSeen.size();
            const int urlsBefore = urls.urls.size();
            const QByteArray sigBefore = stateSignature(w);
            if (qEnvironmentVariableIsSet("QB_AUDIT_TRACE"))
                info(QStringLiteral("trigger %1").arg(e.path));
            const bool dbg = qEnvironmentVariableIsSet("QB_AUDIT_DEBUG") && e.path.contains(qEnvironmentVariable("QB_AUDIT_DEBUG"));
            auto *fe = qobject_cast<TextEditor *>(QApplication::focusWidget());
            const QString htmlBefore = fe ? fe->toHtml() : QString();
            if (dbg)
                info(QStringLiteral("debug %1: focus %2 sel '%3'").arg(e.path, QApplication::focusWidget() ? QString::fromLatin1(QApplication::focusWidget()->metaObject()->className()) : QStringLiteral("none"),
                                                                       fe ? fe->textCursor().selectedText() : QString()));
            a->trigger();
            settle(160);
            dismissModals();
            if (dbg)
                info(QStringLiteral("   after: html changed %1, sel '%2'").arg(fe && fe->toHtml() != htmlBefore).arg(fe ? fe->textCursor().selectedText() : QString()));
            bool effect = g_modalsSeen.size() > modalsBefore || a->isChecked() != wasChecked || urls.urls.size() > urlsBefore
                          || stateSignature(w) != sigBefore;
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (!before.contains(tl) && tl->isVisible() && !qobject_cast<QMenu *>(tl)) {
                    effect = true;
                    tl->close();
                }
            if (a->isCheckable() && a->isChecked() != wasChecked) {
                a->trigger(); // back as it was
                settle(80);
                dismissModals();
            }
            tried << e.path;
            if (!effect)
                noEffect << e.path;
        }
        dismiss.stop();
        dismissModals();
        if (w.isFullScreen())
            w.showNormal();
        settle(300);
        check(w.isVisible() && w.project() != nullptr, QStringLiteral("menu audit: %1 commands triggered with a project open; the window and project are still there")
                                                           .arg(tried.size()));
        info(QStringLiteral("commands with no change to see in this run: %1").arg(noEffect.join(QLatin1String("; "))));

        // without a project: what can be used, and every such command once
        w.closeProject();
        settle(300);
        QList<QPointer<QMenu>> menusNo;
        const QList<MenuEntry> entriesNo = allEntries(&w, &menusNo);
        // the commands that make sense without a project: the start (new, open, import, convert, install),
        // settings of the program and the window, the writing tools that need no project, help
        static const QStringList allowed = {
            QStringLiteral("File > New Project"), QStringLiteral("File > Open Project"), QStringLiteral("File > Open Recent > "),
            QStringLiteral("File > Back Up > Show Backup Folder"), QStringLiteral("File > Import > Scrivener Project"),
            QStringLiteral("File > Convert Markdown"), QStringLiteral("File > Install Update or Add-on"), QStringLiteral("File > Options"),
            QStringLiteral("File > Exit"), QStringLiteral("Edit > Undo"), QStringLiteral("Edit > Redo"), QStringLiteral("Edit > Cut"),
            QStringLiteral("Edit > Copy"), QStringLiteral("Edit > Paste"), QStringLiteral("Edit > Select All"),
            QStringLiteral("Edit > Writing Tools > "), QStringLiteral("View > Show Binder"), QStringLiteral("View > Show Collections"),
            QStringLiteral("View > Show Inspector"), QStringLiteral("View > Toolbars > "), QStringLiteral("View > Text Editing > "),
            QStringLiteral("View > Corkboard Options > Status Stamps"), QStringLiteral("View > Use Label Color In > "),
            QStringLiteral("View > Full Screen"), QStringLiteral("View > Zoom Inspector Tabs Separately"), QStringLiteral("Window > Themes > "), QStringLiteral("Window > Layouts > "),
            QStringLiteral("Window > Scratch Pad"), QStringLiteral("Window > Float Quick Reference Panels"), QStringLiteral("Help > ")};
        QStringList onWithout, wrongWithout;
        for (const MenuEntry &e : entriesNo)
            if (usable(e)) {
                onWithout << e.path;
                bool ok = false;
                for (const QString &x : allowed)
                    ok = ok || e.path == x || (x.endsWith(QLatin1String(" > ")) && e.path.startsWith(x)) || e.path.startsWith(x + QLatin1Char(' '));
                if (!ok)
                    wrongWithout << e.path;
            }
        check(wrongWithout.isEmpty(), QStringLiteral("menu audit: without a project only the commands that need none can be used"),
              wrongWithout.join(QLatin1String("; ")));
        QStringList topOff;
        for (QAction *top : w.menuBar()->actions())
            if (!top->isEnabled())
                topOff << menuText(top->text());
        info(QStringLiteral("menus off without a project: %1").arg(topOff.join(QLatin1String(", "))));
        QStringList triedNo;
        dismiss.start();
        for (const MenuEntry &e : entriesNo) {
            QAction *a = e.action;
            if (!usable(e) || skip.contains(a->objectName()) || e.path.startsWith(QLatin1String("File > Open Recent")))
                continue;
            a->trigger();
            settle(120);
            dismissModals();
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (tl != &w && tl->isVisible() && tl->isWindow() && !qobject_cast<QMenu *>(tl))
                    tl->close();
            if (w.project()) {
                info(QStringLiteral("(a project was opened by %1)").arg(e.path));
                w.closeProject();
            }
            if (w.isFullScreen())
                w.showNormal();
            triedNo << e.path;
        }
        dismiss.stop();
        settle(200);
        check(w.isVisible() && !w.project(), QStringLiteral("menu audit: %1 commands triggered without a project; the window is still there").arg(triedNo.size()));
        // keys of commands that are off: a shortcut must not reach them either
        QStringList keysOn;
        for (QAction *ac : w.findChildren<QAction *>())
            if (!ac->shortcut().isEmpty() && ac->isEnabled() && ac->parent() == &w) {
                bool inAllowed = false;
                for (const MenuEntry &e : entriesNo)
                    if (e.action == ac || (e.action && e.action->property("qbAliasOf").value<QObject *>() == ac))
                        for (const QString &x : allowed)
                            inAllowed = inAllowed || e.path.startsWith(x);
                if (!inAllowed)
                    keysOn << ac->objectName() + QLatin1Char(' ') + ac->shortcut().toString();
            }
        info(QStringLiteral("shortcuts that still work without a project: %1").arg(keysOn.join(QLatin1String(", "))));
    }

    w.closeProject();
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}

#include "m13_tests.moc"
