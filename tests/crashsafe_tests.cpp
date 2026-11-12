// The system test (crash safety and usability): every case of the procedure in one list
// (crashsafe/cs_list.cpp; stages 1 and 2 merged 2026-11-09). Runs only when the writer asks for it:
// not part of tools/run-all-tests.sh, not in tests/check-counts.txt, never in the 10 % sample.
// Specification: the project document claude/quillbinder-test-procedure-crashsafe.md.
// Started by tools/run-crashsafe.sh:
//
//   qb_crashsafe_tests --list                  the cases: id, quick and full estimate (s), needs, title
//   qb_crashsafe_tests --fixtures <dir>        builds the golden fixtures (FX-1, FX-2)
//   qb_crashsafe_tests --case <id> <dir>       runs one case (a child process of its own)
//   qb_crashsafe_tests --verify <dir>          a fresh process: G1, G2 (reports), G3, G4 (the project left)
//
// Environment: QB_CS_FIXTURES (the fixtures), QB_CS_PROFILE=quick|full|smoke, QB_CS_SEED=<n>.
// smoke (2026-11-09): the quick profile with the shortest typing runs and loops, for a shakedown of the harness.
#include "crashsafe/cs.h"

#include "core/Addons.h"
#include "core/CrashGuard.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QFileInfo>
#include <QJsonDocument>

#include <cstdio>

using namespace cs;

static void common(const QString &dir)
{
    Ctx &c = ctx();
    c.dir = dir;
    c.work = dir + QStringLiteral("/work");
    QDir().mkpath(c.work);
    c.fixtures = QString::fromLocal8Bit(qgetenv("QB_CS_FIXTURES"));
    c.buildDir = QStringLiteral(QB_TEST_BUILD_DIR);
    c.sourceDir = QStringLiteral(QB_TEST_SOURCE_DIR);
    c.quick = qgetenv("QB_CS_PROFILE") != "full";
    c.smoke = qgetenv("QB_CS_PROFILE") == "smoke";
}

int main(int argc, char **argv)
{
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode == QLatin1String("--list")) {
        for (const Case &k : cases())
            std::printf("%s %d %d %s %s\n", qPrintable(k.id), k.estQuickSec, k.estFullSec, k.needs.isEmpty() ? "-" : qPrintable(k.needs),
                        qPrintable(k.title));
        return 0;
    }
    const QString caseId = mode == QLatin1String("--case") && argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (caseId == QLatin1String("S-D01"))
        qputenv("QB_TIMING", "1");   // the save times while typing (read before the first save)
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    qputenv("QB_NO_DIALOGS", "1");
    CsApp app(argc, argv);

    if (mode == QLatin1String("--fixtures") && argc > 2) {
        common(QString::fromLocal8Bit(argv[2]));
        setUpProgram();
        const bool ok = buildFixtures(QString::fromLocal8Bit(argv[2]));
        std::printf("fixtures %s\n", ok ? "made" : "FAILED");
        return ok ? 0 : 1;
    }
    if (mode == QLatin1String("--menus") && argc > 2) {
        // every entry of the menu bar with its command name: the coverage check of the procedure
        common(QString::fromLocal8Bit(argv[2]));
        setUpProgram();
        MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
        std::function<void(QMenu *, const QString &)> walk = [&](QMenu *m, const QString &path) {
            emit m->aboutToShow();
            for (QAction *a : m->actions()) {
                if (a->isSeparator())
                    continue;
                const QString label = a->text().remove(QLatin1Char('&'));
                const QString here = path + QStringLiteral(" > ") + label;
                if (a->menu())
                    walk(a->menu(), here);
                else {
                    QObject *target = a->property("qbAliasOf").value<QObject *>();
                    std::printf("%s\t%s\n", qPrintable(here.mid(3)), qPrintable(target ? target->objectName() : a->objectName()));
                }
            }
        };
        for (QAction *top : w->menuBar()->actions())
            if (top->menu())
                walk(top->menu(), QStringLiteral(" > ") + top->text().remove(QLatin1Char('&')));
        delete w;
        return 0;
    }
    if (mode == QLatin1String("--verify") && argc > 2) {
        common(QString::fromLocal8Bit(argv[2]));
        const int f = verifyCase(ctx().dir);
        std::printf("\n%d failure(s)\nEND verify\n", f);
        return f;
    }
    if (caseId.isEmpty() || argc < 4) {
        std::printf("usage: %s --list | --fixtures <dir> | --menus <dir> | --case <id> <dir> | --verify <dir>\n", argv[0]);
        return 2;
    }
    const Case *k = nullptr;
    for (const Case &c : cases())
        if (c.id == caseId)
            k = &c;
    if (!k) {
        std::printf("no case %s\n", qPrintable(caseId));
        return 2;
    }
    common(QString::fromLocal8Bit(argv[3]));
    ctx().id = caseId;
    ctx().seed = qEnvironmentVariableIsSet("QB_CS_SEED") ? quint32(qEnvironmentVariableIntValue("QB_CS_SEED")) : quint32(qHash(caseId)) % 1000000u;
    std::printf("case %s: %s (seed %u, profile %s)\n", qPrintable(caseId), qPrintable(k->title), ctx().seed, ctx().smoke ? "smoke" : ctx().quick ? "quick" : "full");
    if (k->needs.contains(QLatin1Char('S'))) {
        // the add-on Speech Transcription, installed as the writer installs it
        const QString pkg = ctx().buildDir + QStringLiteral("/addons-dist/speech-transcription.qbpkg");
        QStringList log;
        QString err;
        bool restart = false;
        const bool ok = QFileInfo::exists(pkg) && addons::install(pkg, &log, &err, &restart);
        if (!ok) {
            std::printf("SKIP  [S] the add-on Speech Transcription is not built (%s)\n%d failure(s)\nEND %s\n", qPrintable(err.isEmpty() ? pkg : err), 0,
                        qPrintable(caseId));
            return 0;
        }
    }
    setUpProgram();
    crash::note(QStringLiteral("case %1").arg(caseId));
    k->run();
    finishMessages();
    writeText(ctx().dir + QStringLiteral("/facts.json"), QJsonDocument(ctx().facts).toJson(QJsonDocument::Indented));
    std::printf("\n%d failure(s)\nEND %s\n", ctx().failures, qPrintable(caseId));
    std::fflush(stdout);
    return ctx().failures;
}
