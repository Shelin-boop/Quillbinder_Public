#pragma once
// Settings per project (0.62): every option of File > Options can have a value of the
// project's own, kept in the project (settings/options.ini). While a project with such values
// is open and "project settings first" is chosen (the program-wide switch app/projectFirst),
// the program reads a merged copy of its settings file (its own values with the project's on
// top): QSettings' default path points at that copy, so every part of the program that reads
// QSettings sees the project's values without knowing about projects.
//
// What is written into the copy meanwhile (a toolbar arranged, a view option switched, the
// recent list) goes back at sync(): keys the project holds into the project's file, all other
// keys into the program's own file. sync() runs after every AppSettings::save(), before the
// layer changes, and when the program ends.
//
// Keys that belong to the computer or must be known before a project opens (window places,
// the recent list, reopening the last project, the crash watchdog, this switch itself) never
// become the project's: programOnly().
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

namespace qb {
namespace settingslayer {

// The project's file of own values ("<project>/settings/options.ini").
QString fileFor(const QString &projectSettingsDir);

bool programOnly(const QString &key);
// 0.64.1: a setting that names a program the program starts, a folder, a file or an address (the
// citations program, the backup folder, the folder of voice recordings ...): never taken from a
// project, and taken from an options file only after the writer has seen it and agreed
bool namesProgramOrPlace(const QString &key);

// Project settings first (default) or the program's settings always.
bool projectFirst();
void setProjectFirst(bool on);

// Uses the project's values (file "" = no project). Calls the reloader when what the program
// reads changes.
void activate(const QString &projectFile);
QString projectFile();      // the open project's file, also when it is not used ("" = none)
bool active();              // the merged copy is what the program reads now

// The values the project holds (key -> value) and the program's own values.
QVariantMap projectValues();
QVariantMap programValues();

// Writes changes made since the last sync to the project's or the program's file.
void sync();

// File > Options in "All projects" while a project layer is active: the program's own values
// are read until resume() (which puts the project's values on top again).
void suspend();
void resume();
bool suspended();

// File > Options in "This project": from begin to end nothing is synced (the dialog writes every
// option into the merged copy; takeProjectValues() sorts them out).
void beginProjectEdit();
void endProjectEdit();
// File > Options in "This project", after OK / Apply: every option whose value now differs from
// the program's own becomes the project's (and only those); returns their number.
int takeProjectValues();
// Use Program Settings: the project keeps no values of its own (keys "" = all).
void clearProjectValues(const QStringList &keys = {});

// Called when the values the program reads have changed (reload AppSettings and the others).
void setReloader(std::function<void()> reload);

// For tests: the program's own settings file.
QString programFile();

} // namespace settingslayer
} // namespace qb
