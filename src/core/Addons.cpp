#include "core/PathSafety.h"
#include "core/SafeSave.h"
#include "core/Addons.h"
#include "core/Launcher.h"
#include "core/Platform.h"
#include "core/Paths.h"
#include "core/Zip.h"
#include "text/TextUtil.h"

#include <QCoreApplication>
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
    // 0.64.1: the shared check (it also refuses drive letters and control characters)
    return !p.endsWith(QLatin1Char('/')) && pathsafety::isSafeRelative(p);
}

QStringList summarize(const QList<FileEntry> &files)
{
    int themes = 0, templates = 0, formats = 0, plugins = 0, dicts = 0, layouts = 0, scripts = 0, names = 0, tutorials = 0,
        mediaExtra = 0, other = 0;
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
        else if (f.path.startsWith(QLatin1String("media-extra/lib/")))
            ++mediaExtra; // the full FFmpeg of "Extra Video and Audio Formats" (0.50)
        else if (f.path.startsWith(QLatin1String("names/")) || f.path.startsWith(QLatin1String("media-extra/")))
            continue; // licence texts
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
    if (mediaExtra)
        out << tr("video and audio formats licensed under patents (for Media Playback 1.1 or newer)");
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
        safesave::write(f, QJsonDocument(o).toJson());
        safesave::commit(f);
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
    // 0.65.0 (audit F12): the signature over this description (which lists every file's checksum); one that does not
    // match is never installed
    info.signature = signing::checkManifest(mj, zr.fileData(QStringLiteral("manifest.sig"))).state;
    if (info.signature == signing::State::Invalid) {
        info.error = tr("The package's signature does not match: it was changed after it was signed, or it is damaged. "
                        "It cannot be installed; download it again from the project's release page.");
        return info;
    }
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
        // libraries fit one system only (port, W2): a package names its system; Linux packages
        // made before that name none and carry .so files
        bool linuxLibs = false;
        for (const FileEntry &fe : m.files)
            linuxLibs = linuxLibs || fe.path.endsWith(QLatin1String(".so")) || fe.path.contains(QLatin1String(".so."));
        const QString made = !m.platform.isEmpty() ? m.platform : linuxLibs ? QStringLiteral("linux-x86_64") : QString();
        if (!made.isEmpty() && made != platform::packagePlatform()) {
            info.error = made.startsWith(QLatin1String("windows"))
                             ? tr("This add-on was made for Windows; it does not work on this system.")
                             : tr("This add-on was made for Linux; it does not work on this system. Use its Windows version.");
            return info;
        }
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
#ifdef Q_OS_WIN
            info.error = tr("This program update does not fit your installed program (it needs runtime \"%1\", you have "
                            "\"%2\"). A new download of the program is required.")
                             .arg(m.runtime, QLatin1String(QB_RUNTIME_ID));
#else
            info.error = tr("This program update does not fit your AppImage (it needs runtime \"%1\", you have \"%2\"). "
                            "A new AppImage is required.")
                             .arg(m.runtime, QLatin1String(QB_RUNTIME_ID));
#endif
            return info;
        }
        bool hasBinary = false;
        for (const FileEntry &fe : m.files)
            hasBinary = hasBinary || fe.path == (m.binary.isEmpty() ? platform::programFileName() : m.binary);
        if (!hasBinary) {
            info.error = tr("The program update is incomplete.");
            return info;
        }
        const int c = compareVersions(m.version, QStringLiteral(QB_APP_VERSION));
        if (depth > 0 && compareVersions(m.version, launcher::builtInVersion()) <= 0) {
            // in a bundle (an update with all add-ons, W3): the add-ons still go in, the program
            // update is left out, since the program already is this version or newer
            info.skip = true;
            info.note = tr("Program version %1 is already there; this part is left out.").arg(m.version);
            info.provides = QStringList{tr("program version %1 (already there)").arg(m.version)};
            return info;
        }
        if (compareVersions(m.version, launcher::builtInVersion()) <= 0) {
#ifdef Q_OS_WIN
            info.error = tr("Your installed program already is version %1 or newer.").arg(m.version);
#else
            info.error = tr("Your AppImage already contains version %1 or newer.").arg(m.version);
#endif
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

// 0.65.0 (audit F18): a package (or every member of a bundle) is first written into a folder of its own beside its
// place and, for the program, started once; only when all of them are ready are they put in place, and when one of
// them cannot be, the ones placed before it are put back. Before, the members of a bundle were installed one by
// one, and a failure halfway left the first ones installed.
struct Prepared {
    Manifest m;
    PackageInfo info;
    QString target, tmp;
    bool core = false;
};

bool prepare(const QByteArray &data, const PackageInfo &info, QList<Prepared> *out, QStringList *log, QString *error)
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
            if (info.nested.at(i).skip) {
                if (log)
                    log->append(info.nested.at(i).note);
                continue;
            }
            const QByteArray pd = zr.fileData(QLatin1String("packages/") + m.packages.at(i));
            if (!prepare(pd, info.nested.at(i), out, log, error))
                return false;
        }
        return true;
    }
    Prepared p;
    p.m = m;
    p.info = info;
    p.core = m.kind == QLatin1String("core");
    const QString root = p.core ? coreRoot() : addonsRoot();
    const QString name = p.core ? m.version : m.id;
    p.target = root + QLatin1Char('/') + name;
    p.tmp = root + QLatin1String("/.new-") + name;
    QDir(p.tmp).removeRecursively();
    if (!QDir().mkpath(p.tmp)) {
        if (error)
            *error = tr("Cannot create %1").arg(p.tmp);
        return false;
    }
    out->append(p); // its folder goes again when anything fails
    if (!writeFiles(zr, m, p.tmp, error))
        return false;
    if (p.core && !launcher::selfTest(p.tmp, m.binary.isEmpty() ? platform::programFileName() : m.binary, error))
        return false;
    return true;
}

void finish(const Prepared &p, QStringList *log, bool *restart)
{
    const Manifest &m = p.m;
    if (p.core) {
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
                        .arg(m.name, m.version, p.info.provides.isEmpty() ? tr("no content") : p.info.provides.join(QLatin1String(", ")));
    }
}

bool installData(const QByteArray &data, const PackageInfo &info, QStringList *log, QString *error, bool *restart)
{
    QList<Prepared> prepared;
    auto cleanTmp = [&prepared] {
        for (const Prepared &p : std::as_const(prepared))
            QDir(p.tmp).removeRecursively();
    };
    if (!prepare(data, info, &prepared, log, error)) {
        cleanTmp();
        return false;
    }
    // 0.64.1 (audit S-4): an installed version moves aside first and goes only once the new one is in place
    struct Placed { QString target, aside; };
    QList<Placed> placed;
    auto putBack = [&placed] {
        for (int i = int(placed.size()) - 1; i >= 0; --i) {
            QDir(placed.at(i).target).removeRecursively();
            if (!placed.at(i).aside.isEmpty())
                QDir().rename(placed.at(i).aside, placed.at(i).target);
        }
    };
    for (const Prepared &p : std::as_const(prepared)) {
        const QString aside = p.target + QStringLiteral(".old-") + QString::number(QCoreApplication::applicationPid());
        QDir(aside).removeRecursively();
        const bool hadOld = QFileInfo::exists(p.target);
        if (hadOld && !QDir().rename(p.target, aside)) {
            if (error)
                *error = tr("Cannot replace %1").arg(p.target);
            putBack();
            cleanTmp();
            return false;
        }
        if (!QDir().rename(p.tmp, p.target)) {
            if (error)
                *error = tr("Cannot create %1").arg(p.target);
            if (hadOld)
                QDir().rename(aside, p.target);
            putBack();
            cleanTmp();
            return false;
        }
        placed << Placed{p.target, hadOld ? aside : QString()};
    }
    for (const Placed &pl : std::as_const(placed))
        if (!pl.aside.isEmpty())
            QDir(pl.aside).removeRecursively();
    for (const Prepared &p : std::as_const(prepared))
        finish(p, log, restart);
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
    if (!platform.isEmpty())
        o.insert(QStringLiteral("platform"), platform);
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
    m.platform = o.value(QStringLiteral("platform")).toString();
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

bool PackageInfo::trusted() const
{
    if (manifest.kind != QLatin1String("bundle"))
        return signature == signing::State::Valid;
    bool any = false;
    for (const PackageInfo &n : nested) {
        if (n.skip)
            continue;
        if (!n.trusted())
            return false;
        any = true;
    }
    return any;
}

bool PackageInfo::hasProgramUpdate() const
{
    if (manifest.kind == QLatin1String("core"))
        return !skip;
    for (const PackageInfo &n : nested)
        if (n.hasProgramUpdate())
            return true;
    return false;
}

QString trustProblem(const PackageInfo &info, Unsigned policy)
{
#ifdef QB_TEST_SWITCHES
    // builds with the tests: the suites install packages they make themselves (QB_REQUIRE_SIGNED checks the rule)
    if (!qEnvironmentVariableIsSet("QB_REQUIRE_SIGNED"))
        policy = Unsigned::AllowAll;
#endif
    if (policy == Unsigned::AllowAll)
        return QString();
    // the parts to look at: the package itself, or the parts of a bundle that are installed
    QList<PackageInfo> parts;
    if (info.manifest.kind == QLatin1String("bundle")) {
        for (const PackageInfo &n : info.nested)
            if (!n.skip)
                parts << n;
    } else {
        parts << info;
    }
    for (const PackageInfo &p : std::as_const(parts)) {
        if (p.trusted())
            continue;
        if (p.manifest.kind == QLatin1String("core"))
            return tr("This program update is not signed by the Quillbinder project, so it is not installed. Program "
                      "updates are installed only with the project's signature; download the update from the project's "
                      "release page.");
        if (policy == Unsigned::Refuse)
            return tr("\"%1\" is not signed by the Quillbinder project. To install it anyway from the command line, add "
                      "--allow-unsigned.")
                .arg(p.manifest.name);
    }
    return QString();
}

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

bool installBundled(const QString &dir, QStringList *log, QString *error)
{
    const QFileInfoList packages = QDir(dir).entryInfoList({QStringLiteral("*.qbpkg")}, QDir::Files, QDir::Name);
    const QString marker = dataDir() + QStringLiteral("/full-addons-installed-" QB_APP_VERSION);
    if (packages.isEmpty() || QFile::exists(marker))
        return true;
    bool ok = true;
    for (const QFileInfo &fi : packages) {
        QString err;
        bool restart = false;
        if (!install(fi.absoluteFilePath(), log, &err, &restart, Unsigned::AllowAll)) {
            ok = false;
            if (error)
                *error += (error->isEmpty() ? QString() : QStringLiteral("\n")) + fi.fileName() + QStringLiteral(": ") + err;
        }
    }
    if (ok) {
        QDir().mkpath(dataDir());
        QFile m(marker);
        if (m.open(QIODevice::WriteOnly))
            m.write(QB_APP_VERSION "\n");
    }
    return ok;
}

bool install(const QString &file, QStringList *log, QString *error, bool *restartNeeded, Unsigned policy)
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
    const QString why = trustProblem(info, policy);
    if (!why.isEmpty()) {
        if (error)
            *error = why;
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
