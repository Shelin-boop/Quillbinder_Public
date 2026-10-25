#pragma once
// Packages (.qbpkg), add-ons and installed program versions.
//
// A package is a ZIP file:
//
//   manifest.json          what it is (see Manifest)
//   payload/...            the files, listed with SHA-256 sums in the manifest
//   packages/*.qbpkg       only in bundles: the packages to install together
//
// Kinds:
//   "addon"   content and plugins, installed to <data>/addons/<id>/
//             payload sub-folders by convention:
//               themes/*.json  layouts/*.json  scripts/*.json  templates/*.zip  compile-formats/*.json
//               plugins/*.json|*.so  dictionaries/*.aff|*.dic
//   "core"    a newer program binary that runs on the libraries of the
//             AppImage, installed to <data>/core/<version>/ (see Launcher.h)
//   "bundle"  several packages installed in one step
//
// <data> is ~/.local/share/<AppName>.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace qb {
namespace addons {

struct FileEntry {
    QString path;        // relative to payload/
    QByteArray sha256;   // hex
    qint64 size = 0;
    bool executable = false;
};

struct Manifest {
    int format = 1;
    QString id;          // lower case, e.g. "themes-nordic"; core packages use "core"
    QString name;
    QString version;     // 1.2 or 1.2.3
    QString kind;        // addon | core | bundle
    QString description;
    QString author;
    QStringList whatsNew;
    QString minApp;      // add-on: lowest program version that can use it
    QString runtime;     // core: runtime (library set) it was built for
    QString binary;      // core: name of the program file inside payload/
    QList<FileEntry> files;
    QStringList packages; // bundle: file names inside packages/

    QJsonObject toJson() const;
    static Manifest fromJson(const QJsonObject &o);
};

// Result of looking at a package file before installing it.
struct PackageInfo {
    Manifest manifest;
    QList<PackageInfo> nested;  // bundle contents
    QStringList provides;       // readable summary: "2 themes", "1 project template"
    QString error;              // not empty: the package cannot be installed
    QString note;               // e.g. "replaces version 1.0"
    bool isValid() const { return error.isEmpty(); }
};

PackageInfo inspect(const QString &file);
PackageInfo inspect(const QByteArray &data);

// Installs a package (add-on, program update or bundle). log receives one
// line per installed part.
bool install(const QString &file, QStringList *log, QString *error, bool *restartNeeded);

// ---- add-ons
struct Installed {
    Manifest manifest;
    QString dir;
    bool enabled = true;
    QStringList provides;
};
QList<Installed> installedAddons();
void setEnabled(const QString &id, bool on);
bool removeAddon(const QString &id, QString *error);
QString addonsRoot();

// Folders named <kind> ("themes", "layouts", "scripts", "templates", "compile-formats",
// "plugins", "dictionaries") of all enabled add-ons.
QStringList contentDirs(const QString &kind);

// ---- program versions
struct CoreVersion {
    QString version;
    QString dir;           // empty for the built-in version
    bool builtIn = false;
    bool running = false;  // this process
    bool active = false;   // starts next time
    bool usable = true;    // false: built for another runtime
    QStringList whatsNew;
};
QList<CoreVersion> coreVersions();
bool setActiveCore(const QString &version, QString *error); // empty = built-in
bool removeCore(const QString &version, QString *error);
QString coreRoot();

// Message left by the start-up loader when it switched an update off
// (cleared by reading).
QString takeStartNotice();

// -1, 0, 1
int compareVersions(const QString &a, const QString &b);

} // namespace addons
} // namespace qb
