#include "core/Addons.h"
#include "core/Launcher.h"
#include "core/Paths.h"
#include "core/Zip.h"
#include "text/TextUtil.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

#ifndef QB_RUNTIME_ID
#define QB_RUNTIME_ID "unknown"
#endif

namespace qb {
namespace addons {

// ------------------------------------------------------------------ helpers

QString addonsRoot() { return dataDir() + QLatin1String("/addons"); }
QString coreRoot() { return dataDir() + QLatin1String("/core"); }

int compareVersions(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.')), pb = b.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = pa.value(i).toInt(), y = pb.value(i).toInt();
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

namespace {

QString tr(const char *s) { return QObject::tr(s); }

bool safeRelativePath(const QString &p)
{
    if (p.isEmpty() || p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char('\\')) || p.endsWith(QLatin1Char('/')))
        return false;
    for (const QString &part : p.split(QLatin1Char('/')))
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    return true;
}

QStringList summarize(const QList<FileEntry> &files)
{
    int themes = 0, templates = 0, formats = 0, plugins = 0, dicts = 0, layouts = 0, scripts = 0, names = 0, tutorials = 0,
        other = 0;
    for (const FileEntry &f : files) {
        if (f.path.startsWith(QLatin1String("themes/")) && f.path.endsWith(QLatin1String(".json")))
            ++themes;
        else if (f.path.startsWith(QLatin1String("templates/")) && f.path.endsWith(QLatin1String(".zip")))
            ++templates;
        else if (f.path.startsWith(QLatin1String("compile-formats/")) && f.path.endsWith(QLatin1String(".json")))
            ++formats;
        else if (f.path.startsWith(QLatin1String("layouts/")) && f.path.endsWith(QLatin1String(".json")))
            ++layouts;
        else if (f.path.startsWith(QLatin1String("scripts/")) && f.path.endsWith(QLatin1String(".json")))
            ++scripts;
        else if (f.path.startsWith(QLatin1String("plugins/")))
            ++plugins;
        else if (f.path.startsWith(QLatin1String("dictionaries/")) && f.path.endsWith(QLatin1String(".dic")))
            ++dicts;
        else if (f.path.startsWith(QLatin1String("names/")) && f.path.endsWith(QLatin1String(".json")))
            ++names; // name lists of the name generator
        else if (f.path.startsWith(QLatin1String("tutorial/")) && f.path.endsWith(QLatin1String(".zip")))
            ++tutorials; // the interactive tutorial of 0.15 to 0.30, no longer used from 0.40 on
        else if (f.path.startsWith(QLatin1String("names/")))
            continue; // licence text
        else if (!f.path.startsWith(QLatin1String("dictionaries/")))
            ++other;
    }
    QStringList out;
    if (themes)
        out << text::plural(themes, tr("theme"), tr("themes"));
    if (templates)
        out << text::plural(templates, tr("project template"), tr("project templates"));
    if (formats)
        out << text::plural(formats, tr("compile format"), tr("compile formats"));
    if (layouts)
        out << text::plural(layouts, tr("layout"), tr("layouts"));
    if (scripts)
        out << text::plural(scripts, tr("script format"), tr("script formats"));
    if (names)
        out << text::plural(names, tr("name list"), tr("name lists"));
    if (plugins)
        out << text::plural(plugins, tr("plugin file"), tr("plugin files"));
    if (dicts)
        out << text::plural(dicts, tr("dictionary"), tr("dictionaries"));
    if (tutorials)
        out << tr("the old interactive tutorial (no longer used; this add-on can be removed)");
    Q_UNUSED(other);
    return out;
}

QJsonObject readAddonState()
{
    QFile f(addonsRoot() + QLatin1String("/state.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeAddonState(const QJsonObject &o)
{
    QDir().mkpath(addonsRoot());
    QSaveFile f(addonsRoot() + QLatin1String("/state.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}

QSet<QString> disabledIds()
{
    QSet<QString> s;
    for (const QJsonValue &v : readAddonState().value(QStringLiteral("disabled")).toArray())
        s.insert(v.toString());
    return s;
}

Manifest readManifestFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return Manifest::fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

// Checks a package; appVersion is the newest program version that will be
// available after the surrounding bundle is installed.
PackageInfo inspectData(const QByteArray &data, const QString &appVersion, int depth)
{
    PackageInfo info;
    ZipReader zr(data);
    if (!zr.isValid()) {
        info.error = tr("This file is not a package (or it is damaged).");
        return info;
    }
    const QByteArray mj = zr.fileData(QStringLiteral("manifest.json"));
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(mj, &pe);
    if (mj.isEmpty() || pe.error != QJsonParseError::NoError) {
        info.error = tr("The package has no readable description (manifest.json).");
        return info;
    }
    Manifest &m = info.manifest;
    m = Manifest::fromJson(doc.object());
    static const QRegularExpression idRe(QStringLiteral("^[a-z0-9][a-z0-9._-]{1,63}$"));
    static const QRegularExpression verRe(QStringLiteral("^\\d+(\\.\\d+){0,3}$"));
    if (m.format != 1) {
        info.error = tr("The package was made for a newer version of the program.");
        return info;
    }
    if (!idRe.match(m.id).hasMatch() || !verRe.match(m.version).hasMatch() || m.name.isEmpty()
        || !(m.kind == QLatin1String("addon") || m.kind == QLatin1String("core") || m.kind == QLatin1String("bundle"))) {
        info.error = tr("The package description is incomplete.");
        return info;
    }
    // payload files
    for (const FileEntry &fe : m.files) {
        if (!safeRelativePath(fe.path)) {
            info.error = tr("The package contains an invalid file name: %1").arg(fe.path);
            return info;
        }
        const QByteArray fd = zr.fileData(QLatin1String("payload/") + fe.path);
        if (fd.size() != fe.size
            || QCryptographicHash::hash(fd, QCryptographicHash::Sha256).toHex() != fe.sha256.toLower()) {
            info.error = tr("The package is damaged (%1 does not match its checksum). Download it again.").arg(fe.path);
            return info;
        }
    }
    info.provides = summarize(m.files);

    if (m.kind == QLatin1String("addon")) {
        if (!m.minApp.isEmpty() && compareVersions(m.minApp, appVersion) > 0) {
            info.error = tr("This add-on needs program version %1 or newer (you have %2).").arg(m.minApp, appVersion);
            return info;
        }
        const Manifest old = readManifestFile(addonsRoot() + QLatin1Char('/') + m.id + QLatin1String("/manifest.json"));
        if (!old.id.isEmpty()) {
            const int c = compareVersions(m.version, old.version);
            info.note = c > 0   ? tr("Replaces version %1.").arg(old.version)
                        : c < 0 ? tr("Older than the installed version %1; it will replace it.").arg(old.version)
                                : tr("This version is already installed; it will be installed again.");
        }
    } else if (m.kind == QLatin1String("core")) {
        if (m.runtime != QLatin1String(QB_RUNTIME_ID)) {
            info.error = tr("This program update does not fit your AppImage (it needs runtime \"%1\", you have \"%2\"). "
                            "A new AppImage is required.")
                             .arg(m.runtime, QLatin1String(QB_RUNTIME_ID));
            return info;
        }
        bool hasBinary = false;
        for (const FileEntry &fe : m.files)
            hasBinary = hasBinary || fe.path == (m.binary.isEmpty() ? QStringLiteral(QB_APP_ID) : m.binary);
        if (!hasBinary) {
            info.error = tr("The program update is incomplete.");
            return info;
        }
        const int c = compareVersions(m.version, QStringLiteral(QB_APP_VERSION));
        if (compareVersions(m.version, launcher::builtInVersion()) <= 0) {
            info.error = tr("Your AppImage already contains version %1 or newer.").arg(m.version);
            return info;
        }
        info.note = c > 0   ? tr("Updates the program from %1 to %2.").arg(QStringLiteral(QB_APP_VERSION), m.version)
                    : c == 0 ? tr("This version is already running; it will be installed again.")
                             : tr("Older than the running version %1.").arg(QStringLiteral(QB_APP_VERSION));
        info.provides = QStringList{tr("program version %1").arg(m.version)};
    } else { // bundle
        if (depth > 0) {
            info.error = tr("Bundles cannot contain other bundles.");
            return info;
        }
        if (m.packages.isEmpty()) {
            info.error = tr("The bundle is empty.");
            return info;
        }
        // a program update inside the bundle counts for the add-ons' requirements
        QString newest = appVersion;
        QList<QByteArray> parts;
        for (const QString &name : m.packages) {
            if (!safeRelativePath(name)) {
                info.error = tr("The package contains an invalid file name: %1").arg(name);
                return info;
            }
            const QByteArray pd = zr.fileData(QLatin1String("packages/") + name);
            parts << pd;
            ZipReader pz(pd);
            const Manifest pm = Manifest::fromJson(QJsonDocument::fromJson(pz.fileData(QStringLiteral("manifest.json"))).object());
            if (pm.kind == QLatin1String("core") && compareVersions(pm.version, newest) > 0)
                newest = pm.version;
        }
        for (int i = 0; i < parts.size(); ++i) {
            PackageInfo pi = inspectData(parts.at(i), newest, depth + 1);
            if (!pi.isValid()) {
                info.error = tr("%1: %2").arg(pi.manifest.name.isEmpty() ? m.packages.at(i) : pi.manifest.name, pi.error);
                return info;
            }
            info.nested << pi;
        }
    }
    return info;
}

bool writeFiles(const ZipReader &zr, const Manifest &m, const QString &target, QString *error)
{
    for (const FileEntry &fe : m.files) {
        const QString path = target + QLatin1Char('/') + fe.path;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        const QByteArray fd = zr.fileData(QLatin1String("payload/") + fe.path);
        if (!f.open(QIODevice::WriteOnly) || f.write(fd) != fd.size()) {
            if (error)
                *error = tr("Cannot write %1").arg(path);
            return false;
        }
        f.close();
        if (fe.executable)
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                            | QFileDevice::ReadUser | QFileDevice::ExeUser | QFileDevice::ReadGroup
                                            | QFileDevice::ReadOther);
    }
    QFile mf(target + QLatin1String("/manifest.json"));
    if (!mf.open(QIODevice::WriteOnly)) {
        if (error)
            *error = tr("Cannot write %1").arg(mf.fileName());
        return false;
    }
    mf.write(QJsonDocument(m.toJson()).toJson());
    return true;
}

bool installData(const QByteArray &data, const PackageInfo &info, QStringList *log, QString *error, bool *restart)
{
    const Manifest &m = info.manifest;
    ZipReader zr(data);
    if (m.kind == QLatin1String("bundle")) {
        // add-ons first, the program update last
        QList<int> order;
        for (int i = 0; i < info.nested.size(); ++i)
            if (info.nested.at(i).manifest.kind != QLatin1String("core"))
                order << i;
        for (int i = 0; i < info.nested.size(); ++i)
            if (info.nested.at(i).manifest.kind == QLatin1String("core"))
                order << i;
        for (int i : order) {
            const QByteArray pd = zr.fileData(QLatin1String("packages/") + m.packages.at(i));
            if (!installData(pd, info.nested.at(i), log, error, restart))
                return false;
        }
        return true;
    }
    const bool core = m.kind == QLatin1String("core");
    const QString root = core ? coreRoot() : addonsRoot();
    const QString name = core ? m.version : m.id;
    const QString target = root + QLatin1Char('/') + name;
    const QString tmp = root + QLatin1String("/.new-") + name;
    QDir(tmp).removeRecursively();
    if (!QDir().mkpath(tmp)) {
        if (error)
            *error = tr("Cannot create %1").arg(tmp);
        return false;
    }
    if (!writeFiles(zr, m, tmp, error)) {
        QDir(tmp).removeRecursively();
        return false;
    }
    if (core && !launcher::selfTest(tmp, m.binary.isEmpty() ? QStringLiteral(QB_APP_ID) : m.binary, error)) {
        QDir(tmp).removeRecursively();
        return false;
    }
    QDir(target).removeRecursively();
    if (!QDir().rename(tmp, target)) {
        if (error)
            *error = tr("Cannot create %1").arg(target);
        QDir(tmp).removeRecursively();
        return false;
    }
    if (core) {
        QJsonObject st = launcher::readState();
        // remember the version in use, to return to it if the new one fails
        const QString before = st.value(QStringLiteral("active")).toString();
        if (before != m.version) {
            if (before.isEmpty())
                st.remove(QStringLiteral("previous"));
            else
                st.insert(QStringLiteral("previous"), before);
        }
        st.insert(QStringLiteral("active"), m.version);
        st.insert(QStringLiteral("attempts"), 0);
        st.remove(QStringLiteral("notice"));
        launcher::writeState(st);
        if (restart)
            *restart = true;
        if (log)
            *log << tr("Program version %1 installed. It starts the next time you open the program.").arg(m.version);
    } else {
        setEnabled(m.id, true);
        bool plugins = false;
        for (const FileEntry &fe : m.files)
            plugins = plugins || fe.path.startsWith(QLatin1String("plugins/"))
                      || fe.path.startsWith(QLatin1String("dictionaries/"));
        if (plugins && restart)
            *restart = true;
        if (log)
            *log << tr("Add-on \"%1\" %2 installed (%3).")
                        .arg(m.name, m.version, info.provides.isEmpty() ? tr("no content") : info.provides.join(QLatin1String(", ")));
    }
    return true;
}

} // namespace

// ------------------------------------------------------------------ manifest

QJsonObject Manifest::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), format);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("version"), version);
    o.insert(QStringLiteral("kind"), kind);
    if (!description.isEmpty())
        o.insert(QStringLiteral("description"), description);
    if (!author.isEmpty())
        o.insert(QStringLiteral("author"), author);
    if (!whatsNew.isEmpty())
        o.insert(QStringLiteral("whatsNew"), QJsonArray::fromStringList(whatsNew));
    if (!minApp.isEmpty())
        o.insert(QStringLiteral("minApp"), minApp);
    if (!runtime.isEmpty())
        o.insert(QStringLiteral("runtime"), runtime);
    if (!binary.isEmpty())
        o.insert(QStringLiteral("binary"), binary);
    QJsonArray fa;
    for (const FileEntry &fe : files) {
        QJsonObject f;
        f.insert(QStringLiteral("path"), fe.path);
        f.insert(QStringLiteral("sha256"), QString::fromLatin1(fe.sha256));
        f.insert(QStringLiteral("size"), fe.size);
        if (fe.executable)
            f.insert(QStringLiteral("executable"), true);
        fa.append(f);
    }
    if (!fa.isEmpty())
        o.insert(QStringLiteral("files"), fa);
    if (!packages.isEmpty())
        o.insert(QStringLiteral("packages"), QJsonArray::fromStringList(packages));
    return o;
}

Manifest Manifest::fromJson(const QJsonObject &o)
{
    Manifest m;
    m.format = o.value(QStringLiteral("format")).toInt(0);
    m.id = o.value(QStringLiteral("id")).toString();
    m.name = o.value(QStringLiteral("name")).toString();
    m.version = o.value(QStringLiteral("version")).toString();
    m.kind = o.value(QStringLiteral("kind")).toString();
    m.description = o.value(QStringLiteral("description")).toString();
    m.author = o.value(QStringLiteral("author")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("whatsNew")).toArray())
        m.whatsNew << v.toString();
    m.minApp = o.value(QStringLiteral("minApp")).toString();
    m.runtime = o.value(QStringLiteral("runtime")).toString();
    m.binary = o.value(QStringLiteral("binary")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("files")).toArray()) {
        const QJsonObject f = v.toObject();
        FileEntry fe;
        fe.path = f.value(QStringLiteral("path")).toString();
        fe.sha256 = f.value(QStringLiteral("sha256")).toString().toLatin1();
        fe.size = qint64(f.value(QStringLiteral("size")).toDouble());
        fe.executable = f.value(QStringLiteral("executable")).toBool();
        m.files << fe;
    }
    for (const QJsonValue &v : o.value(QStringLiteral("packages")).toArray())
        m.packages << v.toString();
    return m;
}

// ------------------------------------------------------------------ inspect / install

PackageInfo inspect(const QByteArray &data)
{
    return inspectData(data, QStringLiteral(QB_APP_VERSION), 0);
}

PackageInfo inspect(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        PackageInfo info;
        info.error = tr("Cannot read %1").arg(file);
        return info;
    }
    if (f.size() > 400 * 1024 * 1024) {
        PackageInfo info;
        info.error = tr("The file is too large to be a package.");
        return info;
    }
    return inspect(f.readAll());
}

bool install(const QString &file, QStringList *log, QString *error, bool *restartNeeded)
{
    if (restartNeeded)
        *restartNeeded = false;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot read %1").arg(file);
        return false;
    }
    const QByteArray data = f.readAll();
    const PackageInfo info = inspect(data);
    if (!info.isValid()) {
        if (error)
            *error = info.error;
        return false;
    }
    return installData(data, info, log, error, restartNeeded);
}

// ------------------------------------------------------------------ add-ons

QList<Installed> installedAddons()
{
    QList<Installed> out;
    const QSet<QString> off = disabledIds();
    const QDir root(addonsRoot());
    for (const QFileInfo &fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (fi.fileName().startsWith(QLatin1Char('.')))
            continue;
        Installed in;
        in.manifest = readManifestFile(fi.absoluteFilePath() + QLatin1String("/manifest.json"));
        if (in.manifest.id.isEmpty())
            continue;
        in.dir = fi.absoluteFilePath();
        in.enabled = !off.contains(in.manifest.id);
        in.provides = summarize(in.manifest.files);
        out << in;
    }
    return out;
}

void setEnabled(const QString &id, bool on)
{
    QSet<QString> off = disabledIds();
    if (on == !off.contains(id))
        return;
    if (on)
        off.remove(id);
    else
        off.insert(id);
    QJsonObject st = readAddonState();
    QStringList l = off.values();
    l.sort();
    st.insert(QStringLiteral("disabled"), QJsonArray::fromStringList(l));
    writeAddonState(st);
}

bool removeAddon(const QString &id, QString *error)
{
    static const QRegularExpression idRe(QStringLiteral("^[a-z0-9][a-z0-9._-]{1,63}$"));
    const QString dir = addonsRoot() + QLatin1Char('/') + id;
    if (!idRe.match(id).hasMatch() || !QFile::exists(dir + QLatin1String("/manifest.json"))) {
        if (error)
            *error = tr("The add-on is not installed.");
        return false;
    }
    if (!QDir(dir).removeRecursively()) {
        if (error)
            *error = tr("Cannot remove %1").arg(dir);
        return false;
    }
    setEnabled(id, true); // forget the "disabled" entry
    return true;
}

QStringList contentDirs(const QString &kind)
{
    QStringList out;
    for (const Installed &in : installedAddons()) {
        if (!in.enabled)
            continue;
        const QString d = in.dir + QLatin1Char('/') + kind;
        if (QDir(d).exists())
            out << d;
    }
    return out;
}

// ------------------------------------------------------------------ program versions

QList<CoreVersion> coreVersions()
{
    QList<CoreVersion> out;
    const QString running = QStringLiteral(QB_APP_VERSION);
    QString active = launcher::readState().value(QStringLiteral("active")).toString();
    const QString builtIn = launcher::builtInVersion();

    QList<CoreVersion> installed;
    const QDir root(coreRoot());
    for (const QFileInfo &fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.fileName().startsWith(QLatin1Char('.')))
            continue;
        const Manifest m = readManifestFile(fi.absoluteFilePath() + QLatin1String("/manifest.json"));
        if (m.kind != QLatin1String("core") || m.version != fi.fileName())
            continue;
        CoreVersion cv;
        cv.version = m.version;
        cv.dir = fi.absoluteFilePath();
        cv.whatsNew = m.whatsNew;
        cv.usable = m.runtime == QLatin1String(QB_RUNTIME_ID) && compareVersions(m.version, builtIn) > 0;
        cv.running = launcher::isOverlay() && m.version == running;
        installed << cv;
    }
    std::sort(installed.begin(), installed.end(),
              [](const CoreVersion &a, const CoreVersion &b) { return compareVersions(a.version, b.version) > 0; });
    bool activeFound = false;
    for (CoreVersion &cv : installed) {
        cv.active = cv.usable && cv.version == active;
        activeFound = activeFound || cv.active;
    }
    CoreVersion bi;
    bi.version = builtIn;
    bi.builtIn = true;
    bi.running = !launcher::isOverlay();
    bi.active = !activeFound;
    out << installed << bi;
    return out;
}

bool setActiveCore(const QString &version, QString *error)
{
    if (!version.isEmpty()) {
        bool ok = false;
        for (const CoreVersion &cv : coreVersions())
            ok = ok || (!cv.builtIn && cv.version == version && cv.usable);
        if (!ok) {
            if (error)
                *error = tr("Program version %1 is not installed or does not fit this AppImage.").arg(version);
            return false;
        }
    }
    QJsonObject st = launcher::readState();
    st.insert(QStringLiteral("active"), version);
    st.insert(QStringLiteral("attempts"), 0);
    st.remove(QStringLiteral("notice"));
    st.remove(QStringLiteral("previous")); // a deliberate choice: no automatic return
    launcher::writeState(st);
    return true;
}

bool removeCore(const QString &version, QString *error)
{
    static const QRegularExpression verRe(QStringLiteral("^\\d+(\\.\\d+){0,3}$"));
    const QString dir = coreRoot() + QLatin1Char('/') + version;
    if (!verRe.match(version).hasMatch() || !QFile::exists(dir + QLatin1String("/manifest.json"))) {
        if (error)
            *error = tr("Program version %1 is not installed.").arg(version);
        return false;
    }
    if (launcher::isOverlay() && version == QLatin1String(QB_APP_VERSION)) {
        if (error)
            *error = tr("This version is running. Switch to another version and restart first.");
        return false;
    }
    QJsonObject st = launcher::readState();
    if (st.value(QStringLiteral("active")).toString() == version)
        st.insert(QStringLiteral("active"), QString());
    if (st.value(QStringLiteral("previous")).toString() == version)
        st.remove(QStringLiteral("previous"));
    launcher::writeState(st);
    if (!QDir(dir).removeRecursively()) {
        if (error)
            *error = tr("Cannot remove %1").arg(dir);
        return false;
    }
    return true;
}

QString takeStartNotice()
{
    QJsonObject st = launcher::readState();
    const QString n = st.value(QStringLiteral("notice")).toString();
    if (!n.isEmpty()) {
        st.remove(QStringLiteral("notice"));
        launcher::writeState(st);
    }
    return n;
}

} // namespace addons
} // namespace qb
