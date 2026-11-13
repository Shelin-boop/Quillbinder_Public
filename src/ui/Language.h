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
// 0.90.1 (the usability review of 0.90.0, UR-18): numbers are written as the program's language writes them, with
// the digits grouped (English 44,825; German 44.825) unless the writer turned that off (File > Options > General >
// Startup, app/groupDigits). Sets Qt's default locale, which every count of the program goes through; before, an
// English program followed the system's setting (no separators at all under "C", German dots on a German desktop).
void applyNumbers(bool groupDigits);
// how 44825 looks now
QString numberExample();

} // namespace language
} // namespace qb
