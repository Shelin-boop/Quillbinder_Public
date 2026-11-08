// Scripted run for version 0.62: settings per project (File > Options with "Settings for: All
// projects / This project", what changes goes where, which settings come first, Use Program
// Settings, the project settings file, shortcuts per project, the look following the switch), the
// questions of the first start, and (part B) tables and pictures: tables with their design and
// commands, pictures with text flowing around them, captions and descriptions, in the editor, in
// both storage formats, in Scrivener format and in compile.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v062_tests /tmp/shots
#include "v062_common.h"

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qunsetenv("QB_SKIP_FIRST_START");
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV062Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV062Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.62.0")) >= 0,
          QStringLiteral("version 0.62.0 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.quoteStyle = QStringLiteral("en");
    as.save();
    theme::apply();
    // the plugins of this build (the Markdown exporter for the compile checks)
    if (qEnvironmentVariableIsEmpty("QB_PLUGIN_PATH"))
        qputenv("QB_PLUGIN_PATH", QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../plugins")).toUtf8());
    compile::PluginManager::instance().load();

    settingsChecks();
    tableChecks();
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
