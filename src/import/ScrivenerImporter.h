#pragma once
#include "core/ProjectTypes.h"

#include <QStringList>
#include <functional>
#include <memory>

class QTextDocument;

namespace qb {

class Project;

struct ScrivenerImportReport {
    int textDocuments = 0;
    int mediaFiles = 0;
    int snapshots = 0;
    int comments = 0;
    QString scrivenerVersion;
    QStringList warnings;
};

class ScrivenerImporter
{
public:
    using Progress = std::function<void(int done, int total, const QString &what)>;

    // path: the .scriv folder or the .scrivx file inside it
    static QString findScrivx(const QString &path);
    static std::unique_ptr<Project> importProject(const QString &path, const QString &targetParentDir,
                                                  const QString &name, StorageFormat storage,
                                                  ScrivenerImportReport *report, QString *error,
                                                  const Progress &progress = {});

    // The formatting an imported project starts with (0.21.2): size, line spacing, first
    // line indent, space after paragraphs and justification of File > Options > Editing >
    // Formatting ("main text style for new projects"; handed over by AppSettings). The font
    // family stays the one most used in the Scrivener project (the setting's family when the
    // project has none). Unset: the program's built-in formatting, as before 0.21.2.
    static void setNewProjectFormatting(const Formatting &f);
    static void clearNewProjectFormatting();

    // Scrivener 3 marks styled text inside its RTF: "<$Scr_Ps::0>" ... "<!$Scr_Ps::0>" around
    // paragraphs of a paragraph style (the number is the place of the style's id in the
    // document's content.styles, whose names are in Files/styles.xml), "<$Scr_H::2>" ...
    // "<!$Scr_H::2>" around a heading of level 2, "<$Scr_Cs::0>" around character styles,
    // "<$ScrKeepWithNext>" and the like. They are not text: this takes them out. A paragraph
    // inside a heading mark gets "Heading n"; one inside a paragraph style mark gets the style
    // of the project with that name (see scrivenerStyleName), unless it has a style already.
    // Marks may span several paragraphs. Returns the number of marks taken out.
    static int stripMarkers(QTextDocument *doc, const Formatting &f, const QStringList &paragraphStyles = {});
    // The project style for a style name of Scrivener ("Centered Text" -> "Centered"); "" = none.
    static QString scrivenerStyleName(const Formatting &f, const QString &scrivenerName);
    // A project imported by a version before 0.14.0 kept the marks as text (0.21.2: reported
    // by the writer). Takes them out of every text and notes of the project that has them
    // (looked for in the files first, so a project without them is only read). Not for
    // projects in Scrivener format (their working copies never had them). Returns how many
    // documents were changed.
    static int removeLeftoverMarkers(Project *p, QString *error = nullptr);
};

} // namespace qb
