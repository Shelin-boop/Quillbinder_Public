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

    // The width of an index card in pixels (0.90.0, handed over by AppSettings; 210 when unset). Scrivener keeps the
    // place of a card on a freely arranged corkboard in card widths (a card 330 pixels wide at 3.0, 1.0 is 990, 330
    // across); this program keeps pixels: a place is turned into the card width of this program, so the cards keep their
    // arrangement whatever size each program shows them in.
    static void setCardWidth(int pixels);
    static int cardWidth();

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
    // Scrivener 3.1.6.0 for Windows keeps inline notes as text inside the RTF (0.81.2):
    // "{\Scrv_fn=" the footnote "\end_Scrv_fn}" and "{\Scrv_annot \color={\R=0.68\G=0.11\B=0.06} \text=" the
    // annotation "\end_Scrv_annot}" (the braces and backslashes are characters of the text, not RTF). This turns
    // them into inline footnotes and annotations and takes the marks out. Returns how many notes it found.
    static int convertInlineNotes(QTextDocument *doc);
    // 0.81.2: a project imported by an earlier version kept Scrivener's custom metadata values as Scrivener writes
    // them: a checkbox "Yes" / "No" (shown unticked), a date with its time ("2026-03-14 00:00:00 +0100", shown as
    // none). Puts them into this program's form ("1" / "0", "2026-03-14"). A list's choice was kept as the number of
    // the option, whose text is not known any more: left as it is. Not for projects in Scrivener format (they are
    // read again). Returns how many documents were changed.
    static int fixOldCustomValues(Project *p);
    // A project imported by a version before 0.14.0 kept the marks as text (0.21.2: reported
    // by the writer). Takes them out of every text and notes of the project that has them
    // (looked for in the files first, so a project without them is only read). Not for
    // projects in Scrivener format (their working copies never had them). Returns how many
    // documents were changed.
    static int removeLeftoverMarkers(Project *p, QString *error = nullptr);
};

} // namespace qb
