#pragma once
// 0.70.0 (the writer's decision of 2026-11-10, audit A S-06): the program speaks English or German.
// The language is a program-wide setting (app/language: "en" or "de"; File > Options > General > Startup) and
// takes effect at the next start; QB_LANGUAGE overrides it (the checks). The German texts are
// translations/quillbinder_de.ts (made with lupdate; compiled with lrelease into quillbinder_de.qm, a resource).
// They also hold the few texts of Qt's own windows and buttons that the program shows (Yes, Cancel, the
// context menu of text fields, the file dialog, key names in menus), translated here.
#include <QString>
#include <QStringList>

namespace qb {
namespace language {

QStringList available();                 // "en", "de"
QString displayName(const QString &code); // "English", "Deutsch"
QString configured();                    // what the settings (or QB_LANGUAGE) ask for
QString active();                        // what install() put in place
// Installs the translator of the configured language (call once, right after the QApplication is made).
void install();
// 0.90.1 (the usability review of 0.90.0, UR-18): large numbers with their digits grouped as the locale writes them
// (the German program: 44.825; else the system's setting, 44,825 under English or "C", 44.825 on a German desktop),
// unless the writer turned that off (File > Options > General > Startup, app/groupDigits). Sets Qt's default locale
// (the system's, or German for the German program, as before) with only its number options changed: every count of
// the program goes through it. Before, a "C" setting (test machines) wrote 44825.
void applyNumbers(bool groupDigits);
// how 44825 looks with the digits grouped
QString numberExample();

} // namespace language
} // namespace qb
