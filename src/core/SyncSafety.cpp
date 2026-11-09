#include "core/SyncSafety.h"
#include "core/SafeSave.h"

#include "AppInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

namespace qb {
namespace syncsafe {

static QString &ownDeviceName()
{
    static QString name;
    return name;
}

void setDeviceName(const QString &name)
{
    QString n = name.simplified();
    // one line of the lock file; at most 64 characters
    n.remove(QLatin1Char('\n'));
    ownDeviceName() = n.left(64);
}

QString deviceName()
{
    const QString env = qEnvironmentVariable("QB_DEVICE_NAME"); // tests: two "computers" on one machine
    if (!env.isEmpty())
        return env;
    if (!ownDeviceName().isEmpty())
        return ownDeviceName();
    const QString h = QSysInfo::machineHostName();
    return h.isEmpty() ? QStringLiteral("this-computer") : h;
}

QString machineKey()
{
    auto keyOf = [](const QByteArray &id) {
        return QString::fromLatin1(QCryptographicHash::hash("quillbinder-lock:" + id, QCryptographicHash::Sha256).toHex().left(16));
    };
    const QString env = qEnvironmentVariable("QB_DEVICE_NAME"); // tests: two "computers" on one machine
    if (!env.isEmpty())
        return keyOf(env.toUtf8());
    static const QString key = [keyOf] {
        QByteArray id = QSysInfo::machineUniqueId();
        if (id.isEmpty())
            id = QSysInfo::machineHostName().toUtf8();
        return keyOf(id);
    }();
    return key;
}

QString userName()
{
    if (!ownDeviceName().isEmpty())
        return QString(); // the writer chose a name of their own: the system's user name stays out
    QString u = qEnvironmentVariable("USER");
    if (u.isEmpty())
        u = qEnvironmentVariable("USERNAME");
    return u;
}

QString instanceId()
{
    static const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return id;
}

Stamp stampOf(const QString &path)
{
    Stamp s;
    s.known = true;
    const QFileInfo fi(path);
    s.exists = fi.exists();
    if (s.exists) {
        s.size = fi.size();
        s.modifiedMs = fi.lastModified().toMSecsSinceEpoch();
    }
    return s;
}

bool changedSince(const Stamp &before, const QString &path)
{
    return before.known && !before.sameAs(stampOf(path));
}

QString keepCopy(const QString &file, const QString &settingsDir, const QString &what)
{
    if (!QFileInfo::exists(file))
        return QString();
    const QString dir = settingsDir + QStringLiteral("/conflicts");
    if (!QDir().mkpath(dir))
        return QString(); // nothing kept (0.64.1: the caller then does not write over the file)
    QString name = what;
    for (const QChar c : {QLatin1Char('/'), QLatin1Char('\\'), QLatin1Char(':'), QLatin1Char('*'), QLatin1Char('?'),
                          QLatin1Char('"'), QLatin1Char('<'), QLatin1Char('>'), QLatin1Char('|')})
        name.replace(c, QLatin1Char('-'));
    const QString ext = QFileInfo(file).suffix();
    const QString when = QFileInfo(file).lastModified().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
    QString target;
    for (int n = 1;; ++n) {
        target = dir + QLatin1Char('/') + name + QStringLiteral(" (other version %1%2)").arg(when, n > 1 ? QStringLiteral(" %1").arg(n) : QString())
                 + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
        if (!QFileInfo::exists(target))
            break;
    }
    return QFile::copy(file, target) ? target : QString();
}

// ---------------------------------------------------------------- the lock

namespace {
QString enc(const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }
QString dec(const QString &s) { return QUrl::fromPercentEncoding(s.toLatin1()); }
} // namespace

LockInfo readLock(const QString &file)
{
    LockInfo l;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return l;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    f.close();
    const QString first = lines.value(0).trimmed();
    if (first.isEmpty())
        return l;
    const int sp = int(first.lastIndexOf(QLatin1Char(' ')));
    l.device = sp > 0 ? first.left(sp) : first;
    l.pid = sp > 0 ? first.mid(sp + 1).toLongLong() : 0;
    for (int i = 1; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        const int eq = int(line.indexOf(QLatin1Char('=')));
        if (eq <= 0)
            continue;
        const QString k = line.left(eq), v = line.mid(eq + 1);
        if (k == QLatin1String("device"))
            l.device = dec(v);
        else if (k == QLatin1String("user"))
            l.user = dec(v);
        else if (k == QLatin1String("instance"))
            l.instance = v;
        else if (k == QLatin1String("program"))
            l.program = dec(v);
        else if (k == QLatin1String("key"))
            l.key = v;
        else if (k == QLatin1String("opened"))
            l.opened = QDateTime::fromString(v, Qt::ISODate);
        else if (k == QLatin1String("alive"))
            l.alive = QDateTime::fromString(v, Qt::ISODate);
    }
    if (!l.alive.isValid())
        l.alive = QFileInfo(file).lastModified().toUTC();
    l.exists = !l.device.isEmpty();
    return l;
}

bool writeLock(const QString &file, const QDateTime &opened)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    const QString text = QStringLiteral("%1 %2\n").arg(deviceName()).arg(QCoreApplication::applicationPid())
                         + QStringLiteral("device=") + enc(deviceName()) + QLatin1Char('\n')
                         + QStringLiteral("user=") + enc(userName()) + QLatin1Char('\n')
                         + QStringLiteral("instance=") + instanceId() + QLatin1Char('\n')
                         + QStringLiteral("key=") + machineKey() + QLatin1Char('\n')
                         + QStringLiteral("program=") + enc(QStringLiteral(QB_APP_NAME " " QB_APP_VERSION)) + QLatin1Char('\n')
                         + QStringLiteral("opened=") + opened.toUTC().toString(Qt::ISODate) + QLatin1Char('\n')
                         + QStringLiteral("alive=") + QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + QLatin1Char('\n');
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    safesave::write(f, text.toUtf8());
    return safesave::commit(f);
}

void removeOwnLock(const QString &file)
{
    const LockInfo l = readLock(file);
    if (!l.exists)
        return;
    // a lock without an instance (written before 0.64) is ours when it names this computer and process
    const bool ours = l.instance.isEmpty() ? (l.device == deviceName() && l.pid == QCoreApplication::applicationPid())
                                           : l.instance == instanceId();
    if (ours)
        QFile::remove(file);
}

LockState judge(const LockInfo &l, qint64 staleAfterSecs, const std::function<bool(qint64)> &processAlive)
{
    if (!l.exists)
        return LockState::None;
    if (!l.instance.isEmpty() && l.instance == instanceId())
        return LockState::Ours;
    // the same computer: by its key where the lock has one (two computers may carry the same name)
    if (l.key.isEmpty() ? l.device == deviceName() : l.key == machineKey()) {
        if (l.pid == QCoreApplication::applicationPid())
            return LockState::Ours;
        return processAlive && processAlive(l.pid) ? LockState::OtherWindow : LockState::LeftOverHere;
    }
    const qint64 age = l.alive.isValid() ? l.alive.secsTo(QDateTime::currentDateTimeUtc()) : staleAfterSecs + 1;
    return age <= staleAfterSecs ? LockState::OtherDevice : LockState::OtherDeviceStale;
}

int renewIntervalMs()
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("QB_LOCK_RENEW_MS", &ok);
    return ok && v > 0 ? v : 5 * 60 * 1000;
}

qint64 staleAfterSeconds()
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue("QB_LOCK_STALE_S", &ok);
    return ok && v > 0 ? v : 15 * 60;
}

} // namespace syncsafe
} // namespace qb
