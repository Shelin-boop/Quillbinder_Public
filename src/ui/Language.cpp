#include "ui/Language.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace qb {
namespace language {

namespace {
QString &activeCode()
{
    static QString c = QStringLiteral("en");
    return c;
}
} // namespace

QStringList available() { return {QStringLiteral("en"), QStringLiteral("de")}; }

QString displayName(const QString &code)
{
    if (code == QLatin1String("de"))
        return QStringLiteral("Deutsch");
    return QStringLiteral("English");
}

QString configured()
{
    const QString env = qEnvironmentVariable("QB_LANGUAGE");
    if (available().contains(env))
        return env;
    QSettings s;
    const QString v = s.value(QStringLiteral("app/language"), QStringLiteral("en")).toString();
    return available().contains(v) ? v : QStringLiteral("en");
}

QString active() { return activeCode(); }

namespace {
bool &grouped()
{
    static bool g = true;
    return g;
}
} // namespace

void applyNumbers(bool groupDigits)
{
    grouped() = groupDigits;
    const bool german = activeCode() == QLatin1String("de");
    QLocale loc(german ? QLocale::German : QLocale::English, german ? QLocale::Germany : QLocale::UnitedStates);
    loc.setNumberOptions(groupDigits ? QLocale::DefaultNumberOptions : QLocale::OmitGroupSeparator);
    QLocale::setDefault(loc);
}

QString numberExample() { return QLocale().toString(44825); }

void install()
{
    const QString code = configured();
    if (code == QLatin1String("en")) {
        applyNumbers(grouped());
        return;
    }
    auto *t = new QTranslator(QCoreApplication::instance());
    if (t->load(QStringLiteral(":/translations/quillbinder_%1.qm").arg(code))) {
        QCoreApplication::installTranslator(t);
        activeCode() = code;
        applyNumbers(grouped());
        // the texts of Qt's own buttons and dialogs that the program shows (translations/qt_de.ts)
        auto *q = new QTranslator(QCoreApplication::instance());
        if (q->load(QStringLiteral(":/translations/qt_%1.qm").arg(code)))
            QCoreApplication::installTranslator(q);
        else
            delete q;
    } else {
        delete t;
    }
}

} // namespace language
} // namespace qb
