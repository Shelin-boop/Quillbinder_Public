// Scripted run for version 0.60: one source for the private and the public repository. The
// version; About names FFmpeg with its license; the built-in character and place sheets have
// fields of the program's own choice; the media package with all formats (a bundle of both
// media add-ons) installs both in one step; the release text and the README point to it; the
// release workflow runs in the public repository only; tools/legal-check.py passes on the source
// and finds planted problems; tools/publish-public.sh makes a public copy without the private
// files and blocks, commits it without pushing, and refuses a dirty tree or a failed check.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v060_tests /tmp/shots
//   (the media package checks need tools/get-ffmpeg.sh and packaging/build-addons.sh build build/addons-dist)
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "text/TextUtil.h"
#include "ui/MainWindow.h"
#include "AppInfo.h"
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTextDocument>
#include <cstdio>
#include <functional>

using namespace qb;

static int failures = 0;
static const QString kSource = QDir::cleanPath(QStringLiteral(QB_TEST_SOURCE_DIR "/.."));
static const QString kBuild = QStringLiteral(QB_TEST_BUILD_DIR);
// planted wording, in pieces so that the legal check of this source does not find it here
static const QByteArray kBadLine = QByteArray("Quillbinder is a Scrivener") + " clone.";
static const QByteArray kOldName = QByteArray("Scriv") + "enings";

static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}

static void info(const QString &what)
{
    std::printf("      %s\n", qPrintable(what));
    std::fflush(stdout);
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writeFile(const QString &path, const QByteArray &data, bool append = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(append ? QIODevice::Append : QIODevice::WriteOnly))
        return false;
    return f.write(data) == data.size();
}

// runs a program; returns stdout + stderr
static QString run(const QString &program, const QStringList &args, int *rc = nullptr, const QString &cwd = QString())
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    if (!cwd.isEmpty())
        p.setWorkingDirectory(cwd);
    p.start(program, args);
    p.waitForFinished(600000);
    if (rc)
        *rc = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return QString::fromUtf8(p.readAll());
}

static QString runIn(const QString &program, const QStringList &args, const QProcessEnvironment &env, int *rc)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setProcessEnvironment(env);
    p.start(program, args);
    p.waitForFinished(600000);
    *rc = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return QString::fromUtf8(p.readAll());
}

static QString git(const QString &dir, const QStringList &args, int *rc = nullptr)
{
    return run(QStringLiteral("git"), QStringList{QStringLiteral("-C"), dir, QStringLiteral("-c"), QStringLiteral("user.name=Test"),
                                                  QStringLiteral("-c"), QStringLiteral("user.email=test@example.org")} + args,
               rc);
}

static BinderItem *findTitle(BinderItem *it, const QString &title)
{
    if (!it)
        return nullptr;
    if (it->title == title)
        return it;
    for (BinderItem *c : it->children)
        if (BinderItem *f = findTitle(c, title))
            return f;
    return nullptr;
}

// a copy of the source's tracked files (as they are in the working tree) as a fresh repository
static bool copySource(const QString &to, QString *why)
{
    int rc = 0;
    const QString list = run(QStringLiteral("git"), {QStringLiteral("-C"), kSource, QStringLiteral("ls-files")}, &rc);
    if (rc != 0) {
        *why = list;
        return false;
    }
    int n = 0;
    for (const QString &rel : list.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString src = kSource + QLatin1Char('/') + rel;
        if (!QFileInfo(src).isFile())
            continue;  // deleted in the working tree
        const QString dst = to + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (!QFile::copy(src, dst)) {
            *why = QStringLiteral("cannot copy ") + rel;
            return false;
        }
        QFile::setPermissions(dst, QFile::permissions(src));
        ++n;
    }
    git(to, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main")});
    git(to, {QStringLiteral("add"), QStringLiteral("-A")});
    git(to, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("private source")}, &rc);
    *why = QStringLiteral("%1 files").arg(n);
    return rc == 0 && n > 1000;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_DATA_HOME", (home.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QStringLiteral("/config")).toUtf8());
    qputenv("QB_NO_WELCOME", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    const QString tools = kSource + QStringLiteral("/tools/");

    // ================================================================ version
    check(QLatin1String(QB_APP_VERSION) == QLatin1String("0.60.0") && QLatin1String(QB_APP_VERSION_SHOWN) == QLatin1String("0.60.0 alpha"),
          QStringLiteral("the version is 0.60.0, shown as \"0.60.0 alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));

    // ================================================================ About names FFmpeg with its license
    {
        const QString about = MainWindow::aboutText();
        check(about.contains(QLatin1String("FFmpeg")) && about.contains(QLatin1String("LGPL 2.1"))
                  && about.contains(QLatin1String("Third-party source code")) && about.contains(QLatin1String(QB_APP_HOMEPAGE)),
              QStringLiteral("About: the audio and video add-ons use FFmpeg under the LGPL 2.1, with where its source is"), about);
        check(about.contains(QLatin1String("not made by or connected with Literature &amp; Latte"))
                  && about.contains(QLatin1String("Main Developer: Claude.AI w. Opus 5.5 Extra")),
              QStringLiteral("About still says it is independent of Literature & Latte, with the credit"));
    }

    // ================================================================ character and place sheets of the novel template
    {
        QTemporaryDir tmp;
        QString err;
        templates::Info novel;
        for (const auto &i : templates::all())
            if (i.id == QLatin1String("novel"))
                novel = i;
        auto p = templates::createProject(novel, tmp.path(), QStringLiteral("Sheets"), &err);
        check(p != nullptr, QStringLiteral("a project from the novel template"), err);
        if (p) {
            BinderItem *ch = findTitle(p->root(), QStringLiteral("Character Sheet"));
            BinderItem *pl = findTitle(p->root(), QStringLiteral("Setting Sheet"));
            const QString c = ch ? text::plainText(p->docs()->text(ch->id)) : QString();
            const QString s = pl ? text::plainText(p->docs()->text(pl->id)) : QString();
            const QStringList newC = {QStringLiteral("In one sentence:"), QStringLiteral("Part in the plot:"),
                                      QStringLiteral("Voice (how they talk):"), QStringLiteral("Ties to other characters:"),
                                      QStringLiteral("From the first scene to the last:"), QStringLiteral("Open questions:")};
            const QStringList oldC = {QStringLiteral("Role in story"), QStringLiteral("Physical description"),
                                      QStringLiteral("Habits and mannerisms"), QStringLiteral("Internal conflicts"),
                                      QStringLiteral("Personality:")};
            bool cOk = ch != nullptr;
            for (const QString &f : newC)
                cOk = cOk && c.contains(f);
            for (const QString &f : oldC)
                cOk = cOk && !c.contains(f);
            check(cOk, QStringLiteral("the character sheet has the fields of 0.60 and none of the list it had before"), c);
            const bool sOk = pl && s.contains(QLatin1String("By day and by night:")) && s.contains(QLatin1String("Who comes here:"))
                             && !s.contains(QLatin1String("Sights:")) && !s.contains(QLatin1String("Smells:"))
                             && !s.contains(QLatin1String("Role in story"));
            check(sOk, QStringLiteral("the place sheet has the fields of 0.60 and none of the list it had before"), s);
            check(p->templatesFolderId.isEmpty() == false && findTitle(p->root(), QStringLiteral("Characters"))
                      && findTitle(p->root(), QStringLiteral("Characters"))->defaultTemplate == (ch ? ch->id : QString()),
                  QStringLiteral("new documents in Characters still start from the character sheet"));
        }
    }

    // ================================================================ the media package with all formats
    {
        const QString all = kBuild + QStringLiteral("/addons-dist/media-all-formats.qbpkg");
        if (!QFile::exists(all)) {
            info(QStringLiteral("(media packages not built: tools/get-ffmpeg.sh, then packaging/build-addons.sh <build> <build>/addons-dist)"));
        } else {
            const addons::PackageInfo pi = addons::inspect(all);
            QStringList nested;
            for (const auto &n : pi.nested)
                nested << n.manifest.id + QLatin1Char(' ') + n.manifest.version;
            check(pi.isValid() && pi.manifest.kind == QLatin1String("bundle") && pi.manifest.id == QLatin1String("media-all-formats")
                      && pi.manifest.name == QLatin1String("Media Playback, All Formats")
                      && pi.manifest.packages == QStringList({QStringLiteral("media-playback.qbpkg"), QStringLiteral("media-extra-formats.qbpkg")}),
                  QStringLiteral("media-all-formats.qbpkg is one package holding Media Playback and Extra Video and Audio Formats"),
                  pi.error + QLatin1String(" / ") + nested.join(QLatin1String(", ")));
            check(pi.manifest.description.contains(QLatin1String("H.264")) && pi.manifest.description.contains(QLatin1String("patent")),
                  QStringLiteral("... and says that it brings the formats licensed under patents"), pi.manifest.description);
            QStringList log;
            QString err;
            bool restart = false;
            const bool ok = addons::install(all, &log, &err, &restart);
            QStringList ids;
            bool allOn = true;
            for (const auto &a : addons::installedAddons()) {
                ids << a.manifest.id;
                allOn = allOn && a.enabled;
            }
            check(ok && ids.contains(QLatin1String("media-playback")) && ids.contains(QLatin1String("media-extra-formats")) && allOn,
                  QStringLiteral("installing it in one step installs both media add-ons, switched on"),
                  err + QLatin1String(" / ") + ids.join(QLatin1String(", ")) + QLatin1String(" / ") + log.join(QLatin1String("; ")));
            const QString root = addons::addonsRoot();
            check(QFileInfo::exists(root + QStringLiteral("/media-playback/plugins/libqb-media.so"))
                      && QFileInfo::exists(root + QStringLiteral("/media-extra-formats/media-extra/lib/libavcodec.so.60"))
                      && readAll(root + QStringLiteral("/media-extra-formats/media-extra/licenses/README.txt")).contains("LGPL"),
                  QStringLiteral("... with the player, the full FFmpeg the player then uses, and its license note"), root);
        }
    }

    // ================================================================ the full version: the AppImage with every add-on inside
    {
        const QString shown = QString::fromLatin1(QB_APP_VERSION_SHOWN).replace(QLatin1Char(' '), QLatin1Char('-'));
        const QString appImage = kSource + QStringLiteral("/build-appimage/Quillbinder-%1-x86_64.AppImage").arg(shown);
        const QString dist = kBuild + QStringLiteral("/addons-dist");
        const QStringList ids = QDir(kSource + QStringLiteral("/addons")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        bool havePackages = !ids.isEmpty();
        for (const QString &id : ids)
            havePackages = havePackages && QFile::exists(dist + QLatin1Char('/') + id + QStringLiteral(".qbpkg"));
        if (!QFile::exists(appImage) || !havePackages
            || !QFile::exists(kSource + QStringLiteral("/packaging/tools/appimagetool-x86_64.AppImage"))) {
            info(QStringLiteral("(full version not checked: needs %1 (packaging/build-appimage.sh) and the add-on packages)").arg(appImage));
        } else {
            QTemporaryDir work;
            const QString bdir = work.path() + QStringLiteral("/b");
            QDir().mkpath(bdir);
            QFile::copy(appImage, bdir + QLatin1Char('/') + QFileInfo(appImage).fileName());
            QFile::setPermissions(bdir + QLatin1Char('/') + QFileInfo(appImage).fileName(), QFile::permissions(appImage));
            int rc = 0;
            const QString out = run(QStringLiteral("bash"), {kSource + QStringLiteral("/packaging/build-full-appimage.sh"), bdir, dist}, &rc);
            const QString full = bdir + QStringLiteral("/Quillbinder-%1-full-%2-x86_64.AppImage")
                                            .arg(QLatin1String(QB_APP_VERSION), shown.mid(int(qstrlen(QB_APP_VERSION)) + 1));
            check(rc == 0 && QFile::exists(full) && out.contains(QStringLiteral("(%1 add-ons inside)").arg(ids.size()))
                      && QFileInfo(full).size() > QFileInfo(appImage).size() + 10 * 1000 * 1000,
                  QStringLiteral("build-full-appimage.sh makes the full version: the AppImage with all %1 add-ons inside").arg(ids.size()),
                  out.right(1500));
            // its first start, with an empty home folder: every add-on installed, then the program runs
            QDir().mkpath(work.path() + QStringLiteral("/home"));
            QProcessEnvironment env;
            env.insert(QStringLiteral("HOME"), work.path() + QStringLiteral("/home"));
            env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
            const QString addonsDir = work.path() + QStringLiteral("/home/.local/share/Quillbinder/addons");
            const QString first = runIn(full, {QStringLiteral("--appimage-extract-and-run"), QStringLiteral("--version")}, env, &rc);
            const QStringList installed = QDir(addonsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            check(rc == 0 && first.contains(QStringLiteral("Quillbinder %1").arg(QLatin1String(QB_APP_VERSION))) && installed == ids
                      && QFile::exists(work.path() + QStringLiteral("/home/.local/share/Quillbinder/full-addons-installed-" QB_APP_VERSION)),
                  QStringLiteral("its first start installs every add-on, then the program runs"),
                  first + QLatin1String(" / ") + installed.join(QLatin1String(", ")));
            // an add-on removed afterwards stays removed
            QDir(addonsDir + QStringLiteral("/themes-extra")).removeRecursively();
            runIn(full, {QStringLiteral("--appimage-extract-and-run"), QStringLiteral("--version")}, env, &rc);
            check(rc == 0 && !QFileInfo::exists(addonsDir + QStringLiteral("/themes-extra"))
                      && QFileInfo::exists(addonsDir + QStringLiteral("/media-extra-formats/media-extra/lib/libavcodec.so.60")),
                  QStringLiteral("later starts install nothing again (an add-on removed by the writer stays removed)"));
            // what is inside: the packages and a text that says what the full version is
            const QString ex = work.path() + QStringLiteral("/x");
            QDir().mkpath(ex);
            QProcess xp;
            xp.setWorkingDirectory(ex);
            xp.start(full, {QStringLiteral("--appimage-extract")});
            xp.waitForFinished(300000);
            const QString readme = QString::fromUtf8(readAll(ex + QStringLiteral("/squashfs-root/usr/share/quillbinder/full-addons/README.txt")));
            const QString appRun = QString::fromUtf8(readAll(ex + QStringLiteral("/squashfs-root/AppRun")));
            check(readme.contains(QLatin1String("full version")) && readme.contains(QLatin1String("all its add-ons and the libraries they need"))
                      && readme.contains(QLatin1String("Add-on Manager")) && appRun.contains(QLatin1String("--install"))
                      && appRun.contains(QLatin1String("exec ")),
                  QStringLiteral("inside: a text that explains the full version, and the start script"), readme.left(600));
        }
    }

    // ================================================================ release text, README, workflow, exclusions
    {
        QTemporaryDir dist;
        const QString v = QStringLiteral(QB_APP_VERSION);
        for (const QString &n : {QStringLiteral("Quillbinder-%1-alpha-x86_64.AppImage").arg(v), QStringLiteral("quillbinder-%1-update.qbpkg").arg(v),
                                 QStringLiteral("SHA256SUMS.txt")})
            writeFile(dist.path() + QLatin1Char('/') + n, QByteArray());
        writeFile(dist.path() + QStringLiteral("/Quillbinder-%1-full-alpha-x86_64.AppImage").arg(v), QByteArray());
        for (const QString &n : {QStringLiteral("media-playback.qbpkg"), QStringLiteral("media-extra-formats.qbpkg"),
                                 QStringLiteral("media-all-formats.qbpkg"), QStringLiteral("themes-extra.qbpkg")}) {
            const QString built = kBuild + QStringLiteral("/addons-dist/") + n;
            if (!QFile::exists(built) || !QFile::copy(built, dist.path() + QLatin1Char('/') + n))
                writeFile(dist.path() + QLatin1Char('/') + n, QByteArray());
        }
        int rc = 0;
        const QString notes = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("release-notes.py"), dist.path()}, &rc);
        const int at = notes.indexOf(QLatin1String("| media-all-formats.qbpkg"));
        check(rc == 0 && notes.contains(QLatin1String("download **media-all-formats.qbpkg**")) && at > 0
                  && at < notes.indexOf(QLatin1String("| media-extra-formats.qbpkg")) && at < notes.indexOf(QLatin1String("| media-playback.qbpkg"))
                  && notes.contains(QLatin1String("one package, every format"))
                  && notes.contains(QStringLiteral("| Quillbinder-%1-full-alpha-x86_64.AppImage | **The full version**").arg(v))
                  && notes.contains(QLatin1String("the full version, with all add-ons and the libraries they need included")),
              QStringLiteral("the release text names the one media package in the steps and first among the add-ons"), notes.left(2500));
        const QString news = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("release-notes.py"), QStringLiteral("--whats-new")}, &rc);
        check(rc == 0 && news.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size() >= 3 && news.contains(QLatin1String("one package")),
              QStringLiteral("the what's-new lines of 0.60 come from docs/CHANGES.md"), news);
        const QString readme = QString::fromUtf8(readAll(kSource + QStringLiteral("/README.md")));
        check(readme.contains(QLatin1String("media-all-formats.qbpkg")) && !readme.contains(QLatin1String("Known issues in 0.50"))
                  && readme.contains(QLatin1String("not made by or connected with Literature & Latte"))
                  && readme.contains(QLatin1String("contains no code, files or pictures of those programs")),
              QStringLiteral("README: the one media package, no old known issues, independent of Literature & Latte"));
        // download links at the top of the README (the public main page): this version's files, the newest release
        const QString tag = QStringLiteral("v%1").arg(QString::fromLatin1(QB_APP_VERSION_SHOWN).replace(QLatin1Char(' '), QLatin1Char('-')));
        const QString dl = QStringLiteral("https://github.com/Shelin-boop/Quillbinder_Public/releases/download/") + tag + QLatin1Char('/');
        const int download = readme.indexOf(QLatin1String("## Download"));
        check(download > 0 && download < 1500
                  && readme.contains(QStringLiteral("(%1Quillbinder-%2-x86_64.AppImage)").arg(dl, tag.mid(1)))
                  && readme.contains(QStringLiteral("(%1media-all-formats.qbpkg)").arg(dl))
                  && readme.contains(QStringLiteral("(%1Quillbinder-%2-full-%3-x86_64.AppImage)").arg(dl, QLatin1String(QB_APP_VERSION), tag.mid(int(qstrlen(QB_APP_VERSION)) + 2)))
                  && readme.contains(QLatin1String("**the full version**"))
                  && readme.contains(QLatin1String("(https://github.com/Shelin-boop/Quillbinder_Public/releases/latest)")),
              QStringLiteral("README: near the top, direct links to this version's AppImage and media package, and to the newest release"),
              readme.mid(download, 900));
        const QString pubRel = QString::fromUtf8(readAll(kSource + QStringLiteral("/tools/publish-release.sh")));
        check(pubRel.contains(QLatin1String("PRE=(--prerelease=false --latest)")) && !pubRel.contains(QLatin1String("PRE=(--prerelease)")),
              QStringLiteral("program releases are published as full releases marked Latest, so .../releases/latest leads to the program"));
        const QString wf = QString::fromUtf8(readAll(kSource + QStringLiteral("/.github/workflows/release.yml")));
        check(wf.contains(QLatin1String("if: github.repository == 'Shelin-boop/Quillbinder_Public'")),
              QStringLiteral("the release workflow runs in the public repository only"));
        const QString excl = QString::fromUtf8(readAll(kSource + QStringLiteral("/tools/public-exclude.txt")));
        check(excl.contains(QLatin1String("\ndocs/status.md\n")) && excl.contains(QLatin1String("\ntools/put-asset.sh\n")),
              QStringLiteral("tools/public-exclude.txt keeps the development log and the asset tool private"));
        check(QFileInfo::exists(kSource + QStringLiteral("/tests/media/README.txt")) && QFileInfo::exists(kSource + QStringLiteral("/docs/publishing.md")),
              QStringLiteral("the test media say where they come from; docs/publishing.md describes the method"));
    }

    // ================================================================ the legal check
    {
        int rc = 0;
        const QString out = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("legal-check.py"), kSource}, &rc);
        check(rc == 0 && out.contains(QLatin1String("\n0 failure(s)")) && out.count(QLatin1String("\nok ")) + out.startsWith(QLatin1String("ok ")) >= 9,
              QStringLiteral("tools/legal-check.py passes on the source"), out);
    }

    // ================================================================ publishing into a public clone
    {
        QTemporaryDir priv, pub, plant;
        QString why;
        const bool copied = copySource(priv.path(), &why);
        check(copied, QStringLiteral("(a copy of the source as a private test repository)"), why);
        int rc = 0;
        git(pub.path(), {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main")});
        writeFile(pub.path() + QStringLiteral("/OLD.txt"), "a file of the last public version\n");
        writeFile(pub.path() + QStringLiteral("/README.md"), "old README\n");
        git(pub.path(), {QStringLiteral("add"), QStringLiteral("-A")});
        git(pub.path(), {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("public 0.50")});
        const QString script = priv.path() + QStringLiteral("/tools/publish-public.sh");
        if (copied) {
            const QString out = run(QStringLiteral("bash"), {script, pub.path(), QStringLiteral("--commit")}, &rc, priv.path());
            QFile out1(QDir(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath()).filePath(QStringLiteral("v060_publish.txt")));
            QDir().mkpath(QFileInfo(out1).absolutePath());
            if (out1.open(QIODevice::WriteOnly))
                out1.write(out.toUtf8());
            check(rc == 0 && out.contains(QLatin1String("== legal check of the public copy")) && out.contains(QLatin1String("\n0 failure(s)"))
                      && out.contains(QLatin1String("private blocks taken out of CLAUDE.md")),
                  QStringLiteral("publish-public.sh: export, private blocks out, legal check passed"), out.right(2500));
            const QString P = pub.path();
            check(!QFileInfo::exists(P + QStringLiteral("/docs/status.md")) && !QFileInfo::exists(P + QStringLiteral("/tools/put-asset.sh"))
                      && QFileInfo::exists(P + QStringLiteral("/tools/publish-public.sh")) && QFileInfo::exists(P + QStringLiteral("/LICENSE"))
                      && QFileInfo::exists(P + QStringLiteral("/.github/workflows/release.yml")) && !QFileInfo::exists(P + QStringLiteral("/OLD.txt")),
                  QStringLiteral("... the public copy: without the private files, with the workflow, files of the last version gone"));
            const QString claude = QString::fromUtf8(readAll(P + QStringLiteral("/CLAUDE.md")));
            check(!claude.contains(QLatin1String("New Tool For Writing")) && !claude.contains(QLatin1String("\n<!-- private:"))
                      && !claude.contains(QLatin1String("put-asset")) && claude.contains(QLatin1String("## Publishing, legal check (0.60)")),
                  QStringLiteral("... CLAUDE.md without its private parts, the rest kept"));
            check(readAll(P + QStringLiteral("/README.md")) == readAll(priv.path() + QStringLiteral("/README.md"))
                      && readAll(P + QStringLiteral("/src/ui/MainWindow.cpp")) == readAll(priv.path() + QStringLiteral("/src/ui/MainWindow.cpp")),
                  QStringLiteral("... other files exactly as in the private repository"));
            const QString sha = git(priv.path(), {QStringLiteral("rev-parse"), QStringLiteral("--short=12"), QStringLiteral("HEAD")}).trimmed();
            const QString msg = git(P, {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")});
            const QString count = git(P, {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}).trimmed();
            check(msg.startsWith(QStringLiteral("Quillbinder %1").arg(QLatin1String(QB_APP_VERSION_SHOWN))) && msg.contains(sha) && count == QLatin1String("2")
                      && git(P, {QStringLiteral("status"), QStringLiteral("--porcelain")}).trimmed().isEmpty(),
                  QStringLiteral("... one commit \"Quillbinder <version>\" naming the private commit; nothing left uncommitted"), msg + count);
            check(out.contains(QLatin1String("Committed, not pushed")) && git(P, {QStringLiteral("remote")}).trimmed().isEmpty(),
                  QStringLiteral("... and nothing is pushed"));
            // the same source again: nothing to commit
            const QString again = run(QStringLiteral("bash"), {script, P, QStringLiteral("--commit")}, &rc, priv.path());
            check(rc == 0 && again.contains(QLatin1String("\n0 file(s) changed"))
                      && git(P, {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}).trimmed() == QLatin1String("2"),
                  QStringLiteral("publishing the same source again changes nothing"), again.right(800));
            // a file not committed: refused
            writeFile(priv.path() + QStringLiteral("/notes-not-committed.txt"), "x\n");
            const QString dirty = run(QStringLiteral("bash"), {script, P, QStringLiteral("--commit")}, &rc, priv.path());
            check(rc == 2 && dirty.contains(QLatin1String("only committed files are published")),
                  QStringLiteral("a private tree with files not committed is refused"), dirty.right(800));
            QFile::remove(priv.path() + QStringLiteral("/notes-not-committed.txt"));
            // download links of another version in the README: refused
            {
                const QByteArray readme = readAll(priv.path() + QStringLiteral("/README.md"));
                QByteArray stale = readme;
                stale.replace("releases/download/v" QB_APP_VERSION, "releases/download/v0.59.0");
                writeFile(priv.path() + QStringLiteral("/README.md"), stale);
                git(priv.path(), {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-a"), QStringLiteral("-m"), QStringLiteral("old links")});
                const QString old = run(QStringLiteral("bash"), {script, P, QStringLiteral("--commit")}, &rc, priv.path());
                check(rc == 2 && old.contains(QLatin1String("README.md: the download links must point to"))
                          && git(P, {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}).trimmed() == QLatin1String("2"),
                      QStringLiteral("a README whose download links name another version is refused"), old.right(600));
                writeFile(priv.path() + QStringLiteral("/README.md"), readme);
                git(priv.path(), {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-a"), QStringLiteral("-m"), QStringLiteral("links again")});
            }
            // a failed legal check: nothing reaches the public copy
            writeFile(priv.path() + QStringLiteral("/README.md"), "\n" + kBadLine + "\n", true);
            git(priv.path(), {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-a"), QStringLiteral("-m"), QStringLiteral("bad")});
            const QString bad = run(QStringLiteral("bash"), {script, P, QStringLiteral("--commit")}, &rc, priv.path());
            check(rc != 0 && bad.contains(QLatin1String("FAIL  no wording that suggests a copy of"))
                      && !readAll(P + QStringLiteral("/README.md")).contains(kBadLine)
                      && git(P, {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}).trimmed() == QLatin1String("2"),
                  QStringLiteral("a failed legal check stops the publishing: the public copy stays as it was"), bad.right(1200));
        }

        // planted problems: each is found
        if (copied) {
            const QString T = plant.path() + QStringLiteral("/tree");
            run(QStringLiteral("cp"), {QStringLiteral("-a"), priv.path(), T}, &rc);
            QDir(T + QStringLiteral("/.git")).removeRecursively();
            // the README without its note on Literature & Latte, the manual with an old name
            QByteArray readme = readAll(T + QStringLiteral("/README.md"));
            readme.replace("not made by or connected with Literature & Latte", "independent");
            readme.replace(kBadLine, "");
            writeFile(T + QStringLiteral("/README.md"), readme);
            writeFile(T + QStringLiteral("/docs/manual.md"), "\nThe " + kOldName + " view.\n", true);
            writeFile(T + QStringLiteral("/tests/screens/scrivener-binder.png"), "\x89PNG\r\n\x1a\n");
            writeFile(T + QStringLiteral("/tests/old/sample.scrivx"), "<ScrivenerProject/>\n");
            writeFile(T + QStringLiteral("/addons/x/payload/fonts/Some.ttf"), QByteArray("\0\1\0\0", 4));
            writeFile(T + QStringLiteral("/tools/build-ffmpeg.sh"), "\n    --enable-gpl \\\n", true);
            writeFile(T + QStringLiteral("/claude/notes.md"), "private notes\n");
            const QString out = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("legal-check.py"), QStringLiteral("--public"), T}, &rc);
            QFile f(QDir(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::tempPath()).filePath(QStringLiteral("v060_planted.txt")));
            if (f.open(QIODevice::WriteOnly))
                f.write(out.toUtf8());
            check(rc == 1 && out.contains(QLatin1String("FAIL  README, AppStream text")) && out.contains(QLatin1String("        README.md")),
                  QStringLiteral("planted: the README without its note on Literature & Latte is found"), out);
            check(out.contains(QLatin1String("docs/manual.md:")) && out.contains(QLatin1String(": " + kOldName)),
                  QStringLiteral("planted: an old name of Scrivener's in the manual is found"));
            check(out.contains(QLatin1String("FAIL  no Scrivener or Scapple files")) && out.contains(QLatin1String("tests/old/sample.scrivx"))
                      && out.contains(QLatin1String("tests/screens/scrivener-binder.png")),
                  QStringLiteral("planted: a Scrivener project file and a picture named after Scrivener are found"));
            check(out.contains(QLatin1String("FAIL  every picture, sound, video, font and document file has a note"))
                      && out.contains(QLatin1String("FAIL  every font has its license file beside it")),
                  QStringLiteral("planted: a picture and a font without a note on where they come from are found"));
            check(out.contains(QLatin1String("FAIL  FFmpeg is built without GPL")),
                  QStringLiteral("planted: FFmpeg built with GPL parts is found"));
            check(out.contains(QLatin1String("FAIL  no private files or private blocks")) && out.contains(QLatin1String("docs/status.md"))
                      && out.contains(QLatin1String("claude/notes.md")) && out.contains(QLatin1String("CLAUDE.md")),
                  QStringLiteral("planted (public copy): the development log, private notes and private blocks are found"));
        }
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
