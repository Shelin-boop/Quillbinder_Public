#include "ui/CrashDialogs.h"
#include "AppInfo.h"
#include "core/Addons.h"
#include "core/BinderItem.h"
#include "core/Launcher.h"
#include "core/Paths.h"
#include "core/Project.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QShortcutEvent>
#include <QSplitter>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyle>
#include <QSysInfo>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <gnu/libc-version.h>

namespace qb {

namespace {

QString home(const QString &path)
{
    const QString h = QDir::homePath();
    if (!h.isEmpty() && (path == h || path.startsWith(h + QLatin1Char('/'))))
        return QLatin1Char('~') + path.mid(h.size());
    return path;
}

QString gb(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("?");
    if (bytes < 1000000000LL)
        return QStringLiteral("%1 MB").arg(QString::number(double(bytes) / 1e6, 'f', 0));
    return QStringLiteral("%1 GB").arg(QString::number(double(bytes) / 1e9, 'f', 1));
}

// "ext4, 120.3 GB free of 500.1 GB" (and a hint for network and sync folders)
QString storage(const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("(none)");
    QString p = path;
    while (!p.isEmpty() && !QFileInfo::exists(p))
        p = QFileInfo(p).absolutePath() == p ? QString() : QFileInfo(p).absolutePath();
    const QStorageInfo si(p.isEmpty() ? QDir::homePath() : p);
    if (!si.isValid())
        return home(path) + QStringLiteral(" (not found)");
    const QString fs = QString::fromLatin1(si.fileSystemType());
    QString out = QStringLiteral("%1 (%2, %3 free of %4%5)")
                      .arg(home(path), fs, gb(si.bytesAvailable()), gb(si.bytesTotal()),
                           si.isReadOnly() ? QStringLiteral(", read-only") : QString());
    static const QStringList remote = {QStringLiteral("nfs"), QStringLiteral("nfs4"), QStringLiteral("cifs"),
                                       QStringLiteral("smb3"), QStringLiteral("smbfs"), QStringLiteral("sshfs"),
                                       QStringLiteral("fuse.sshfs"), QStringLiteral("davfs"), QStringLiteral("fuse.rclone")};
    if (remote.contains(fs) || fs.startsWith(QLatin1String("fuse")))
        out += QStringLiteral(" [network or user-space file system: saving can be slower or fail when the connection drops]");
    return out;
}

// bytes in a folder (at most `maxFiles` files are counted; -1 if the folder is missing)
qint64 folderBytes(const QString &dir, int maxFiles = 50000)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return -1;
    qint64 sum = 0;
    int n = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && n++ < maxFiles) {
        it.next();
        sum += it.fileInfo().size();
    }
    return sum;
}

QString fileValue(const QString &file, const QString &key)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith(key.toLatin1())) {
            const int colon = line.indexOf(':');
            return QString::fromUtf8(colon >= 0 ? line.mid(colon + 1) : line.mid(key.size())).trimmed();
        }
    return QString();
}

qint64 kbValue(const QString &file, const QString &key)
{
    return fileValue(file, key).section(QLatin1Char(' '), 0, 0).toLongLong() * 1024;
}

QString envList()
{
    static const char *vars[] = {"XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION",
                                 "KDE_FULL_SESSION", "KDE_SESSION_VERSION", "WAYLAND_DISPLAY", "DISPLAY",
                                 "QT_QPA_PLATFORM", "QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE", "QT_SCALE_FACTOR",
                                 "QT_SCREEN_SCALE_FACTORS", "QT_AUTO_SCREEN_SCALE_FACTOR", "QT_ENABLE_HIGHDPI_SCALING",
                                 "LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE", "APPIMAGE", "APPDIR"};
    QStringList out;
    for (const char *v : vars)
        if (qEnvironmentVariableIsSet(v))
            out << QStringLiteral("  %1=%2").arg(QLatin1String(v), home(qEnvironmentVariable(v)));
    return out.isEmpty() ? QStringLiteral("  (none of the usual ones set)") : out.join(QLatin1Char('\n'));
}

QDateTime g_started = QDateTime::currentDateTime();

// ---------------------------------------------------------------- recent actions

QString clean(QString s)
{
    s.remove(QLatin1Char('&'));
    s.replace(QChar(0x2026), QLatin1String("..."));
    return s.simplified();
}

class ActivityRecorder : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::Shortcut:
            if (auto *a = qobject_cast<QAction *>(o)) {
                const auto *se = static_cast<QShortcutEvent *>(e);
                crash::note(QStringLiteral("shortcut %1: %2").arg(se->key().toString(), clean(a->text())));
            }
            break;
        case QEvent::MouseButtonRelease: {
            const auto *me = static_cast<QMouseEvent *>(e);
            if (auto *m = qobject_cast<QMenu *>(o)) {
                if (QAction *a = m->actionAt(me->position().toPoint()))
                    if (!a->menu() && !a->isSeparator() && a->isEnabled())
                        crash::note(QStringLiteral("menu: %1 > %2").arg(clean(m->title()), clean(a->text())));
            } else if (auto *b = qobject_cast<QAbstractButton *>(o)) {
                if (b->rect().contains(me->position().toPoint()) && b->isEnabled()) {
                    QString what = clean(b->text());
                    if (auto *tb = qobject_cast<QToolButton *>(b); tb && tb->defaultAction())
                        what = clean(tb->defaultAction()->text());
                    if (what.isEmpty())
                        what = clean(b->toolTip()).left(60);
                    if (what.isEmpty())
                        what = b->objectName();
                    crash::note(QStringLiteral("button: %1").arg(what.isEmpty() ? QStringLiteral("(no name)") : what));
                }
            }
            break;
        }
        case QEvent::KeyPress: {
            if (!o->isWidgetType() || o != QApplication::focusWidget())
                break;
            const auto *k = static_cast<QKeyEvent *>(e);
            const Qt::KeyboardModifiers mods = k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
            const int key = k->key();
            const bool typing = !mods
                                && ((!k->text().isEmpty() && k->text().at(0).isPrint()) || key == Qt::Key_Backspace
                                    || key == Qt::Key_Delete || key == Qt::Key_Return || key == Qt::Key_Enter
                                    || key == Qt::Key_Tab);
            const bool arrow = !mods && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down);
            const QString where = QString::fromLatin1(o->metaObject()->className()).section(QLatin1String("::"), -1);
            if (typing) {
                m_typed = m_lastGroup == QLatin1String("typing") ? m_typed + 1 : 1;
                m_lastGroup = QStringLiteral("typing");
                crash::noteInGroup(m_lastGroup, QStringLiteral("typing in %1: %2 (not recorded)").arg(where, text::plural(m_typed, QStringLiteral("key"), QStringLiteral("keys"))));
            } else if (arrow) {
                m_arrows = m_lastGroup == QLatin1String("arrows") ? m_arrows + 1 : 1;
                m_lastGroup = QStringLiteral("arrows");
                crash::noteInGroup(m_lastGroup, QStringLiteral("arrow keys in %1: %2").arg(where, text::plural(m_arrows, QStringLiteral("press"), QStringLiteral("presses"))));
            } else if (key != Qt::Key_Shift && key != Qt::Key_Control && key != Qt::Key_Alt && key != Qt::Key_Meta
                       && key != Qt::Key_AltGr && key != 0) {
                m_lastGroup.clear();
                crash::note(QStringLiteral("key %1 in %2").arg(QKeySequence(k->keyCombination()).toString(), where));
            }
            return false;
        }
        case QEvent::Show:
            if (o->isWidgetType()) {
                auto *w = static_cast<QWidget *>(o);
                if (w->isWindow() && (qobject_cast<QDialog *>(w) || w->windowType() == Qt::Window) && !qobject_cast<QMenu *>(w))
                    crash::note(QStringLiteral("window opened: %1").arg(clean(w->windowTitle()).isEmpty()
                                                                             ? QString::fromLatin1(w->metaObject()->className())
                                                                             : clean(w->windowTitle())));
            }
            break;
        default:
            break;
        }
        if (e->type() == QEvent::Shortcut || e->type() == QEvent::MouseButtonRelease)
            m_lastGroup.clear();
        return false;
    }

private:
    QString m_lastGroup;
    int m_typed = 0;
    int m_arrows = 0;
};

} // namespace

// ---------------------------------------------------------------- the snapshot

namespace sysinfo {

QStringList concerns(const Project *project)
{
    QStringList out;
    const AppSettings &s = AppSettings::instance();
    // ---- drives: full, read-only, on the network
    struct Place {
        QString what, path;
    };
    QList<Place> places;
    if (project)
        places << Place{QStringLiteral("the project"), project->displayFolder()};
    if (project && !project->scrivenerPath.isEmpty())
        places << Place{QStringLiteral("the project's working copy"), project->folderPath()};
    places << Place{QStringLiteral("the program data"), dataDir()}
           << Place{QStringLiteral("the backups"), s.backupDir.isEmpty() ? dataDir() + QLatin1String("/Backups") : s.backupDir};
    QSet<QString> seenDrives;
    for (const Place &p : places) {
        QString path = p.path;
        while (!path.isEmpty() && !QFileInfo::exists(path))
            path = QFileInfo(path).absolutePath() == path ? QString() : QFileInfo(path).absolutePath();
        const QStorageInfo si(path.isEmpty() ? QDir::homePath() : path);
        if (!si.isValid())
            continue;
        const QString drive = si.rootPath();
        if (seenDrives.contains(drive))
            ; // said already
        else if (si.isReadOnly())
            out << QStringLiteral("The drive of %1 (%2) is read-only: nothing can be saved there.").arg(p.what, home(p.path));
        else if (si.bytesAvailable() >= 0 && si.bytesAvailable() < 1000000000LL)
            out << QStringLiteral("Only %1 free on the drive of %2 (%3): saving and backups fail when it is full.")
                       .arg(gb(si.bytesAvailable()), p.what, home(p.path));
        seenDrives.insert(drive);
        const QString fs = QString::fromLatin1(si.fileSystemType());
        if (p.what == QLatin1String("the project")
            && (fs.startsWith(QLatin1String("fuse")) || fs.startsWith(QLatin1String("nfs")) || fs == QLatin1String("cifs")
                || fs.startsWith(QLatin1String("smb")) || fs == QLatin1String("davfs")))
            out << QStringLiteral("The project is on a network or user-space file system (%1): saving can be slower or "
                                  "fail when the connection drops.")
                       .arg(fs);
    }
    // ---- the project
    if (project) {
        // (before 0.20 a backup of more than 4.29 GB was damaged, and backups of projects in
        // Scrivener format left out the working copy; both are fixed)
        const qint64 bytes = folderBytes(project->displayFolder());
        const QString backups = s.backupDir.isEmpty() ? dataDir() + QLatin1String("/Backups") : s.backupDir;
        QString existing = backups;
        while (!existing.isEmpty() && !QFileInfo::exists(existing))
            existing = QFileInfo(existing).absolutePath() == existing ? QString() : QFileInfo(existing).absolutePath();
        const QStorageInfo bsi(existing.isEmpty() ? QDir::homePath() : existing);
        if (bytes > 1000000000LL && bsi.isValid() && bsi.bytesAvailable() < bytes)
            out << QStringLiteral("The project folder holds %1, the drive of the backups (%2) has %3 free: a backup "
                                  "(a ZIP file of the project) may not fit.")
                       .arg(gb(bytes), home(backups), gb(bsi.bytesAvailable()));
    }
    // ---- memory
    const qint64 avail = kbValue(QStringLiteral("/proc/meminfo"), QStringLiteral("MemAvailable"));
    if (avail > 0 && avail < 500000000LL)
        out << QStringLiteral("Only %1 of memory is available: the program can become slow, or the system can end it.").arg(gb(avail));
    // ---- settings
    if (s.autosaveSeconds > 60)
        out << QStringLiteral("Automatic saving waits for %1 s without typing: that much work can be lost if the program "
                              "ends unexpectedly (File > Options > General).")
                   .arg(s.autosaveSeconds);
    if (!s.backupOnClose && !s.backupOnOpen)
        out << QStringLiteral("Automatic backups are off (File > Options > Backup).");
    // ---- earlier trouble
    int recent = 0;
    const QDateTime monthAgo = QDateTime::currentDateTime().addDays(-30);
    for (const crash::Report &r : crash::reports())
        if (r.when >= monthAgo)
            ++recent;
    if (recent > 0)
        out << QStringLiteral("%1 in the last 30 days (Help > Crash Reports).")
                   .arg(text::plural(recent, QStringLiteral("crash or freeze report"), QStringLiteral("crash or freeze reports")));
    if (QLatin1String(qVersion()) != QLatin1String(QT_VERSION_STR))
        out << QStringLiteral("Qt %1 is running, but the program was built with Qt %2.").arg(QLatin1String(qVersion()), QLatin1String(QT_VERSION_STR));
    return out;
}

QString text(const Project *project)
{
    QStringList t;
    const AppSettings &s = AppSettings::instance();
    t << QStringLiteral("This is a snapshot of the program and the computer. It names files and folders, but holds no "
                        "text of your documents.");
    t << QString();
    // ---- what may cause trouble soon
    t << QStringLiteral("Things to look at");
    const QStringList c = concerns(project);
    if (c.isEmpty())
        t << QStringLiteral("  Nothing noticed.");
    for (const QString &line : c)
        t << QStringLiteral("  - %1").arg(line);
    t << QString();
    // ---- program
    QString version = QStringLiteral("%1 %2").arg(QLatin1String(QB_APP_NAME), QLatin1String(QB_APP_VERSION_SHOWN));
    if (launcher::isOverlay())
        version += QStringLiteral(" (program update, running on the AppImage %1)").arg(launcher::builtInVersion());
    else if (!launcher::appDir().isEmpty())
        version += QStringLiteral(" (built into the AppImage)");
    else
        version += QStringLiteral(" (not started from an AppImage)");
    t << QStringLiteral("Program");
    t << QStringLiteral("  %1").arg(version);
    t << QStringLiteral("  Runtime: %1").arg(QLatin1String(QB_RUNTIME_ID));
    const QString bid = crash::buildId();
    t << QStringLiteral("  Program file: %1%2").arg(home(QCoreApplication::applicationFilePath()),
                                                    bid.isEmpty() ? QString() : QStringLiteral(" (build ID %1)").arg(bid));
    if (qEnvironmentVariableIsSet("APPIMAGE"))
        t << QStringLiteral("  AppImage: %1").arg(home(qEnvironmentVariable("APPIMAGE")));
    QStringList versions;
    for (const addons::CoreVersion &cv : addons::coreVersions()) {
        QStringList marks;
        if (cv.builtIn)
            marks << QStringLiteral("built in");
        if (cv.running)
            marks << QStringLiteral("running");
        if (cv.active)
            marks << QStringLiteral("active");
        if (!cv.usable)
            marks << QStringLiteral("not usable here");
        versions << (marks.isEmpty() ? cv.version : QStringLiteral("%1 (%2)").arg(cv.version, marks.join(QLatin1String(", "))));
    }
    t << QStringLiteral("  Program versions: %1").arg(versions.isEmpty() ? QStringLiteral("(only this one)") : versions.join(QLatin1String("; ")));
    QStringList ads;
    for (const addons::Installed &a : addons::installedAddons())
        ads << QStringLiteral("%1 %2 (%3)").arg(a.manifest.name, a.manifest.version, a.enabled ? QStringLiteral("on") : QStringLiteral("off"));
    t << QStringLiteral("  Add-ons: %1").arg(ads.isEmpty() ? QStringLiteral("none") : ads.join(QLatin1String("; ")));
    t << QStringLiteral("  Started: %1").arg(g_started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    t << QStringLiteral("  Process: %1").arg(QCoreApplication::applicationPid());

    // ---- system
    t << QString() << QStringLiteral("System");
    t << QStringLiteral("  Operating system: %1 (kernel %2, %3), C library glibc %4")
             .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture(),
                  QLatin1String(gnu_get_libc_version()));
    t << QStringLiteral("  Desktop: %1, session %2%3")
             .arg(qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("unknown")),
                  qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("unknown")),
                  qEnvironmentVariableIsSet("KDE_SESSION_VERSION")
                      ? QStringLiteral(", KDE Plasma %1").arg(qEnvironmentVariable("KDE_SESSION_VERSION"))
                      : QString());
    QString style = QStringLiteral("?");
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
        style = app->style() ? app->style()->name() : style;
    t << QStringLiteral("  Qt: %1 (the program was built with %2), platform %3, style %4, platform theme %5")
             .arg(QLatin1String(qVersion()), QLatin1String(QT_VERSION_STR), QGuiApplication::platformName(), style,
                  qEnvironmentVariable("QT_QPA_PLATFORMTHEME", QStringLiteral("(default)")));
    const QString cpu = fileValue(QStringLiteral("/proc/cpuinfo"), QStringLiteral("model name"));
    t << QStringLiteral("  Processor: %1 (%2 threads)").arg(cpu.isEmpty() ? QStringLiteral("unknown") : cpu).arg(QThread::idealThreadCount());
    t << QStringLiteral("  Memory: %1, %2 available; swap %3; this program uses %4")
             .arg(gb(kbValue(QStringLiteral("/proc/meminfo"), QStringLiteral("MemTotal"))),
                  gb(kbValue(QStringLiteral("/proc/meminfo"), QStringLiteral("MemAvailable"))),
                  gb(kbValue(QStringLiteral("/proc/meminfo"), QStringLiteral("SwapTotal"))),
                  gb(kbValue(QStringLiteral("/proc/self/status"), QStringLiteral("VmRSS"))));
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        t << QStringLiteral("  Screens: none");
    for (QScreen *sc : screens) {
        const QSize px = sc->size() * sc->devicePixelRatio();
        t << QStringLiteral("  Screen %1: %2 x %3 px, scale %4, %5 dpi, %6 x %7 mm, %8 Hz%9")
                 .arg(sc->name().isEmpty() ? QString::number(screens.indexOf(sc) + 1) : sc->name())
                 .arg(px.width())
                 .arg(px.height())
                 .arg(sc->devicePixelRatio())
                 .arg(qRound(sc->logicalDotsPerInch()))
                 .arg(qRound(sc->physicalSize().width()))
                 .arg(qRound(sc->physicalSize().height()))
                 .arg(qRound(sc->refreshRate()))
                 .arg(sc == QGuiApplication::primaryScreen() ? QStringLiteral(" (primary)") : QString());
    }
    t << QStringLiteral("  Language: %1 (%2), time zone %3")
             .arg(QLocale::system().name(), QLocale::system().uiLanguages().mid(0, 4).join(QLatin1String(", ")),
                  QString::fromLatin1(QTimeZone::systemTimeZoneId()));
    t << QStringLiteral("  Fonts installed: %1 families").arg(QFontDatabase::families().size());

    // ---- folders
    t << QString() << QStringLiteral("Folders");
    t << QStringLiteral("  Settings: %1").arg(storage(QFileInfo(QSettings().fileName()).absolutePath()));
    t << QStringLiteral("  Program data: %1").arg(storage(dataDir()));
    t << QStringLiteral("  Backups: %1").arg(storage(s.backupDir.isEmpty() ? dataDir() + QLatin1String("/Backups") : s.backupDir));
    t << QStringLiteral("  Crash reports: %1").arg(home(crash::reportDir()));
    t << QStringLiteral("  Temporary files: %1").arg(storage(QDir::tempPath()));

    // ---- settings that matter for saving
    t << QString() << QStringLiteral("Settings");
    t << QStringLiteral("  Save automatically after %1 s without typing").arg(s.autosaveSeconds);
    t << QStringLiteral("  Backup when a project is closed: %1 (keep %2), when opened: %3")
             .arg(s.backupOnClose ? QStringLiteral("yes") : QStringLiteral("no"))
             .arg(s.backupKeep == 0 ? QStringLiteral("all") : QString::number(s.backupKeep))
             .arg(s.backupOnOpen ? QStringLiteral("yes") : QStringLiteral("no"));
    t << QStringLiteral("  Reopen the last project: %1; theme %2; spelling %3")
             .arg(s.openLastProject ? QStringLiteral("yes") : QStringLiteral("no"), s.theme,
                  s.spellCheck ? s.spellLanguage : QStringLiteral("off"));

    // ---- project
    t << QString() << QStringLiteral("Project");
    if (!project) {
        t << QStringLiteral("  (none open)");
    } else {
        int texts = 0, words = 0;
        for (const BinderItem *it : project->allItems())
            if (it->hasText()) {
                ++texts;
                words += it->wordCount;
            }
        t << QStringLiteral("  %1").arg(project->title());
        t << QStringLiteral("  File: %1").arg(home(project->displayFile()));
        t << QStringLiteral("  Kept as: %1, documents stored as %2")
                 .arg(project->scrivenerPath.isEmpty() ? QStringLiteral("%1 project").arg(QLatin1String(QB_APP_NAME))
                                                       : QStringLiteral("Scrivener project (working copy %1)").arg(home(project->folderPath())),
                      project->storage == StorageFormat::Rtf ? QStringLiteral("RTF") : QStringLiteral("Markdown"));
        t << QStringLiteral("  Size: %1 items, %2 texts, %3 words").arg(project->allItems().size()).arg(texts).arg(words);
        t << QStringLiteral("  Folder: %1").arg(storage(project->displayFolder()));
    }

    t << QString() << QStringLiteral("Environment variables");
    t << envList();
    return t.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

void refresh(const Project *project) { crash::setSnapshot(text(project).toUtf8()); }

void installActivityRecorder()
{
    static ActivityRecorder *r = nullptr;
    if (r)
        return;
    r = new ActivityRecorder(qApp);
    qApp->installEventFilter(r);
}

} // namespace sysinfo

// ---------------------------------------------------------------- System Information

static QPlainTextEdit *reportView(QWidget *parent, const QString &name)
{
    auto *t = new QPlainTextEdit(parent);
    t->setObjectName(name);
    t->setReadOnly(true);
    t->setLineWrapMode(QPlainTextEdit::NoWrap);
    t->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return t;
}

SystemInfoDialog::SystemInfoDialog(const Project *project, QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("systemInfoDialog"));
    setWindowTitle(tr("System Information"));
    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("What %1 knows about itself and this computer. Copy it into a message when you report a "
                                "problem; it names files and folders but holds no text of your documents.")
                                 .arg(QLatin1String(QB_APP_NAME)),
                             this);
    intro->setWordWrap(true);
    l->addWidget(intro);
    m_text = reportView(this, QStringLiteral("systemInfoText"));
    m_text->setPlainText(sysinfo::text(project));
    l->addWidget(m_text, 1);
    auto *bb = new QDialogButtonBox(this);
    QPushButton *copy = bb->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("copySystemInfo"));
    QPushButton *save = bb->addButton(tr("Save As…"), QDialogButtonBox::ActionRole);
    bb->addButton(QDialogButtonBox::Close);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_text->toPlainText()); });
    connect(save, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(
            this, tr("Save System Information"),
            QDir::homePath() + QStringLiteral("/%1 system information %2.txt")
                                   .arg(QLatin1String(QB_APP_NAME), QDate::currentDate().toString(Qt::ISODate)),
            tr("Text files (*.txt)"));
        if (f.isEmpty())
            return;
        QFile out(f);
        if (out.open(QIODevice::WriteOnly))
            out.write(m_text->toPlainText().toUtf8());
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    resize(860, 620);
}

QString SystemInfoDialog::text() const { return m_text->toPlainText(); }

// ---------------------------------------------------------------- Crash Reports

static QString when(const QDateTime &t) { return t.isValid() ? t.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("?"); }

CrashReportsDialog::CrashReportsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("crashReportsDialog"));
    setWindowTitle(tr("Crash Reports"));
    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("When %1 ends because of an error, or does not respond for 30 seconds, it writes a report "
                                "on this computer. Nothing is sent anywhere. To report the problem, copy the report into a "
                                "message (or send the file). It names files, folders and the commands used before the "
                                "error, but holds no text of your documents.")
                                 .arg(QLatin1String(QB_APP_NAME)),
                             this);
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *split = new QSplitter(Qt::Horizontal, this);
    m_list = new QListWidget(split);
    m_list->setObjectName(QStringLiteral("crashReportList"));
    m_text = reportView(split, QStringLiteral("crashReportText"));
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({250, 650});
    l->addWidget(split, 1);
    m_where = new QLabel(this);
    m_where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(m_where);
    auto *bb = new QDialogButtonBox(this);
    m_copy = bb->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
    m_copy->setObjectName(QStringLiteral("copyCrashReport"));
    QPushButton *folder = bb->addButton(tr("Open Folder"), QDialogButtonBox::ActionRole);
    m_delete = bb->addButton(tr("Delete"), QDialogButtonBox::ActionRole);
    m_delete->setObjectName(QStringLiteral("deleteCrashReport"));
    m_deleteAll = bb->addButton(tr("Delete All…"), QDialogButtonBox::ActionRole);
    m_deleteAll->setObjectName(QStringLiteral("deleteAllCrashReports"));
    bb->addButton(QDialogButtonBox::Close);
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { showCurrent(); });
    connect(m_copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_text->toPlainText()); });
    connect(folder, &QPushButton::clicked, this, [] {
        QDir().mkpath(crash::reportDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(crash::reportDir()));
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem *it = m_list->currentItem()) {
            QFile::remove(it->data(Qt::UserRole).toString());
            reload();
        }
    });
    connect(m_deleteAll, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Delete All Crash Reports"), tr("Delete all %1?").arg(text::plural(count(), tr("crash report"), tr("crash reports"))))
            != QMessageBox::Yes)
            return;
        for (const crash::Report &r : crash::reports())
            QFile::remove(r.file);
        reload();
    });
    reload();
    resize(980, 640);
}

void CrashReportsDialog::reload()
{
    const QString was = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
    m_list->clear();
    for (const crash::Report &r : crash::reports()) {
        const QString what = r.kind == QLatin1String("freeze")
                                 ? (r.recovered ? tr("freeze (recovered)") : tr("freeze"))
                                 : (r.signal.isEmpty() ? tr("crash") : r.signal);
        auto *it = new QListWidgetItem(QStringLiteral("%1  %2").arg(when(r.when), what), m_list);
        it->setData(Qt::UserRole, r.file);
        it->setToolTip(QStringLiteral("%1\n%2 %3").arg(r.meaning, QLatin1String(QB_APP_NAME), r.version));
    }
    m_where->setText(tr("Folder: %1").arg(crash::reportDir()));
    if (m_list->count() == 0) {
        m_text->setPlainText(tr("There are no reports. %1 has not ended because of an error, nor stopped responding, since "
                                "the reports were last deleted.")
                                 .arg(QLatin1String(QB_APP_NAME)));
    } else {
        select(was);
        if (!m_list->currentItem())
            m_list->setCurrentRow(0);
    }
    m_copy->setEnabled(m_list->count() > 0);
    m_delete->setEnabled(m_list->count() > 0);
    m_deleteAll->setEnabled(m_list->count() > 0);
}

void CrashReportsDialog::showCurrent()
{
    QListWidgetItem *it = m_list->currentItem();
    if (!it)
        return;
    QFile f(it->data(Qt::UserRole).toString());
    if (!f.open(QIODevice::ReadOnly)) {
        m_text->setPlainText(tr("The report cannot be read: %1").arg(f.errorString()));
        return;
    }
    m_text->setPlainText(crash::readable(QString::fromUtf8(f.readAll())));
}

void CrashReportsDialog::select(const QString &file)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == file) {
            m_list->setCurrentRow(i);
            return;
        }
}

int CrashReportsDialog::count() const { return m_list->count(); }
QString CrashReportsDialog::shownText() const { return m_text->toPlainText(); }

// ---------------------------------------------------------------- after an unexpected end

bool showPreviousEnds(QWidget *parent, const QList<crash::PreviousEnd> &ends, bool lastProjectWouldOpen)
{
    if (ends.isEmpty())
        return false;
    const crash::PreviousEnd &e = ends.first();
    const QString app = QLatin1String(QB_APP_NAME);
    QMessageBox box(parent);
    box.setObjectName(QStringLiteral("previousSessionNotice"));
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QCoreApplication::translate("qb::CrashReportsDialog", "Last Session Ended Unexpectedly"));
    QString text;
    const crash::Report r = e.report.isEmpty() ? crash::Report() : crash::readHeader(e.report);
    if (!e.report.isEmpty() && r.kind == QLatin1String("freeze")) {
        text = QCoreApplication::translate("qb::CrashReportsDialog",
                                           "%1 stopped responding on %2 (version %3) and was then ended from outside.")
                   .arg(app, when(e.crashed), e.version.isEmpty() ? r.version : e.version);
        text += QLatin1String("\n\n")
                + QCoreApplication::translate("qb::CrashReportsDialog",
                                              "A freeze report was saved on this computer. It shows where the program was "
                                              "stuck: Help > Crash Reports shows it, and you can copy it from there into a "
                                              "message. It holds no text of your documents.");
    } else if (!e.report.isEmpty()) {
        text = QCoreApplication::translate("qb::CrashReportsDialog", "%1 ended because of an error on %2 (version %3, %4).")
                   .arg(app, when(e.crashed), e.version.isEmpty() ? r.version : e.version,
                        r.meaning.isEmpty() ? r.signal : r.meaning.section(QLatin1Char(':'), 0, 0));
        text += QLatin1String("\n\n")
                + QCoreApplication::translate("qb::CrashReportsDialog",
                                              "A crash report was saved on this computer. It helps to find the cause: "
                                              "Help > Crash Reports shows it, and you can copy it from there into a message. "
                                              "It holds no text of your documents.");
    } else {
        text = QCoreApplication::translate("qb::CrashReportsDialog",
                                           "The last session of %1 (started %2) did not end normally. It was probably "
                                           "stopped from outside, or the computer was switched off without closing %1 "
                                           "first. No crash report was written.")
                   .arg(app, when(e.started));
    }
    if (ends.size() > 1)
        text += QLatin1String("\n\n")
                + QCoreApplication::translate("qb::CrashReportsDialog", "This happened %1 times since the last normal end.")
                      .arg(ends.size());
    text += QLatin1String("\n\n")
            + QCoreApplication::translate("qb::CrashReportsDialog",
                                          "Your work is saved up to the last automatic save, a few seconds before the end. "
                                          "If something is missing, look at the document's snapshots or at the newest backup "
                                          "(File > Back Up > Show Backup Folder).");
    box.setText(text);
    QPushButton *show = nullptr, *skip = nullptr;
    if (!e.report.isEmpty())
        show = box.addButton(r.kind == QLatin1String("freeze")
                                 ? QCoreApplication::translate("qb::CrashReportsDialog", "Show Freeze Report")
                                 : QCoreApplication::translate("qb::CrashReportsDialog", "Show Crash Report"),
                             QMessageBox::ActionRole);
    const bool quick = !e.report.isEmpty() && e.started.isValid() && e.crashed.isValid()
                       && e.started.secsTo(e.crashed) <= 60;
    if (lastProjectWouldOpen && quick) {
        skip = box.addButton(QCoreApplication::translate("qb::CrashReportsDialog", "Start Without the Last Project"),
                             QMessageBox::ActionRole);
        skip->setObjectName(QStringLiteral("startWithoutProject"));
        box.setInformativeText(QCoreApplication::translate("qb::CrashReportsDialog",
                                                           "It ended within a minute of starting. If it ends again when the "
                                                           "last project opens, start without it this time."));
    }
    box.setDefaultButton(box.addButton(QMessageBox::Ok));
    box.exec();
    if (show && box.clickedButton() == show) {
        CrashReportsDialog dlg(parent);
        dlg.select(e.report);
        dlg.exec();
    }
    return skip && box.clickedButton() == skip;
}

// ------------------------------------------------------------------ log console

void showLogConsole(QWidget *parent)
{
    // one window; a second click brings it to the front
    static QPointer<QDialog> open;
    if (open) {
        open->raise();
        open->activateWindow();
        return;
    }
    auto *dlg = new QDialog(parent);
    open = dlg;
    dlg->setObjectName(QStringLiteral("logConsole"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QCoreApplication::translate("LogConsole", "Internal Log Console"));
    dlg->resize(720, 420);
    auto *l = new QVBoxLayout(dlg);
    auto *text = new QPlainTextEdit(dlg);
    text->setObjectName(QStringLiteral("logText"));
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    l->addWidget(text, 1);
    auto *note = new QLabel(QCoreApplication::translate("LogConsole",
                                                        "The program's messages since it was started (the last 1000). "
                                                        "They help to find out why something did not work."),
                            dlg);
    note->setWordWrap(true);
    note->setEnabled(false);
    l->addWidget(note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QPushButton *copy = bb->addButton(QCoreApplication::translate("LogConsole", "Copy All"), QDialogButtonBox::ActionRole);
    l->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    QObject::connect(copy, &QPushButton::clicked, dlg, [text] { QApplication::clipboard()->setText(text->toPlainText()); });

    // new lines are fetched twice a second (messages come from every thread)
    auto shown = std::make_shared<quint64>(0);
    auto fill = [text, shown] {
        const quint64 count = crash::logCount();
        if (count == *shown)
            return;
        const QStringList lines = crash::logLines();
        const quint64 fresh = count - *shown;
        if (*shown == 0 || fresh >= quint64(lines.size())) {
            text->setPlainText(lines.isEmpty() ? QString() : lines.join(QLatin1Char('\n')));
        } else {
            for (qsizetype i = lines.size() - qsizetype(fresh); i < lines.size(); ++i)
                text->appendPlainText(lines.at(i));
        }
        *shown = count;
        text->moveCursor(QTextCursor::End);
    };
    fill();
    if (crash::logCount() == 0)
        text->setPlaceholderText(QCoreApplication::translate("LogConsole", "No messages so far."));
    auto *timer = new QTimer(dlg);
    QObject::connect(timer, &QTimer::timeout, dlg, fill);
    timer->start(500);
    dlg->show();
}

} // namespace qb
