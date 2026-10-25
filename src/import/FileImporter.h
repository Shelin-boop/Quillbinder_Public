#pragma once
#include "core/ProjectTypes.h"
#include "import/ImporterApi.h"

#include <QString>
#include <QStringList>

class QTextDocument;

namespace qb {

class Project;
class BinderItem;

namespace importer {

enum class FileKind { Text, Image, Pdf, Media, WebPage, Other };

// File > Options > Sharing > Import (0.21). The window side (AppSettings) hands
// them over; the standard values are what the program did before 0.21.
struct ImportOptions {
    bool commentsInline = false;   // comments of Word, LibreOffice and RTF files become inline annotations
    bool footnotesInline = true;   // footnotes become inline footnotes (else linked footnotes)
    bool ignoreStyles = false;     // the files' paragraph and character styles are left out
    QString encoding = QStringLiteral("auto"); // plain text: auto | UTF-8 | UTF-16 | windows-1252 | ISO-8859-1 | ISO-8859-15
    bool htmlAsText = false;       // .html files become text documents (else web page documents)
    QString docConverter = QStringLiteral("builtin"); // .doc files: builtin (the add-on) | libreoffice (when installed)
};
void setOptions(const ImportOptions &o);
const ImportOptions &options();
// While one of these exists, readTextFile reads files as they are, without the import
// options (File > Compare and Transfer compares the file itself).
struct RawReading {
    RawReading();
    ~RawReading();
};
// Plain text in the given encoding; "auto": UTF-8 (or UTF-16 with its mark), else Windows Western.
QString decodeText(const QByteArray &data, const QString &encoding);
FileKind classify(const QString &path);
QStringList textFileFilters();   // for file dialogs

// Read any supported text file (txt, md, rtf, docx, odt, html, fountain)
// into a document. keepFormatting: keep fonts/sizes (RTF projects).
// Scripts (.fountain, .fdx) get the elements of the project's script format;
// titlePage receives their title page as "Key: value" lines.
// Files of other kinds go to the add-on importer for their suffix (.doc);
// its warnings are added to `warnings`.
bool readTextFile(const QString &path, QTextDocument *doc, const Formatting &f, bool keepFormatting,
                  QString *error, QString *titlePage = nullptr, QStringList *warnings = nullptr);
// .doc / .dot: Word 97–2003 (read by the add-on "Word 97–2003 Import")
bool isLegacyWordSuffix(const QString &suffix);
// A script of another program (.fountain, .fdx): imported in script mode.
bool isScriptFile(const QString &path);
// Separator for importAndSplit: a document per scene (scripts).
inline QString splitAtScenes() { return QStringLiteral("\x01scenes"); }

bool readDocx(const QString &path, QTextDocument *doc, const Formatting &f, QString *error);
bool readOdt(const QString &path, QTextDocument *doc, const Formatting &f, QString *error);

// Import files into the binder. Text files become editable documents,
// everything else is copied into the project. The Draft only accepts text.
QList<BinderItem *> importFiles(Project *p, const QStringList &paths, BinderItem *parent, int row,
                                QStringList *warnings);

// Import one text file and split it into several documents at `separator`
// (a line containing only the separator text; "\n\n" = empty line;
// splitAtScenes() = at the scene and act headings of a script).
QList<BinderItem *> importAndSplit(Project *p, const QString &path, const QString &separator, bool firstLineIsTitle,
                                   BinderItem *parent, int row, QString *error, QStringList *warnings = nullptr);

// A fetched web page (add-on "Web Page Import"): a text document, or a web
// page document with its pictures; the page's address becomes a document
// bookmark, its description the synopsis, address, author and date the notes.
BinderItem *addWebPage(Project *p, const importers::WebPage &page, BinderItem *parent, int row, QStringList *warnings);

// Create a media item from a file (copies it into the project).
BinderItem *importMedia(Project *p, const QString &path, BinderItem *parent, int row);

} // namespace importer
} // namespace qb
