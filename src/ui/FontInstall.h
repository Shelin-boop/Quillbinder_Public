#pragma once
// Navigate > Fonts (0.17): adding fonts without leaving the program.
//
// Fonts are installed for the current user, as KDE's font manager does it for
// "Personal" fonts: copied into ~/.local/share/fonts, which fontconfig reads at
// every start of a program. The program registers the new files with Qt at
// once, so the font lists show them without a restart, and asks fontconfig
// (fc-cache) to refresh its cache for the other programs.

#include <QString>
#include <QStringList>

namespace qb {
namespace fontinstall {

// ~/.local/share/fonts (made when missing).
QString userFontDir();
// The suffixes of font files the program installs (ttf, otf, ...).
QStringList fontSuffixes();
bool isFontFile(const QString &path);

struct Result {
    QStringList installed;   // file names copied
    QStringList families;    // font families that came with them
    QStringList skipped;     // "name: reason"
};

// Copies font files (and the font files inside .zip archives) into the user's
// font folder and registers them with Qt. A file of the same name and content
// is not copied again; one of the same name and other content gets a number.
Result install(const QStringList &files, const QString &targetDir = QString());

// Registers the font files of the user's font folders (~/.local/share/fonts,
// ~/.fonts) that this process does not know yet: fonts installed by another
// program while this one runs. Returns the families that are new.
QStringList reload();

// Opens the desktop's font manager (KDE: System Settings > Fonts > Font
// Management; others: font-manager, gnome-font-viewer). False when none is
// installed. "which" names the program that was started.
bool openFontManager(QString *which = nullptr);
// The command line that would be used (empty when none is found).
QStringList fontManagerCommand();

// Refreshes fontconfig's cache for the folder (fc-cache, in the background).
void refreshCache(const QString &dir);

} // namespace fontinstall
} // namespace qb
