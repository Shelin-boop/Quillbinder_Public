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

} // namespace language
} // namespace qb
