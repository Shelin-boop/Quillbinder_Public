#pragma once
#include "core/ProjectTypes.h"
#include "import/ImporterApi.h"

#include <QString>
#include <QStringList>

class QTextDocument;

namespace qb {
namespace importer {

// Fills doc (cleared first) with what an importer read: styles of the
// project, lists, notes, links, pictures (as image resources of doc, stored
// with the document by DocumentStore::setContent). keepFormatting (RTF
// projects): fonts, sizes, colors and indents as well.
void fromContent(const importers::Content &content, QTextDocument *doc, const Formatting &f, bool keepFormatting);

// Reads a file with the add-on importer for its suffix. False with an error
// if there is none or it cannot read the file. Warnings of the importer are
// added to `warnings`.
bool readWithImporter(const QString &path, QTextDocument *doc, const Formatting &f, bool keepFormatting,
                      QString *error, QStringList *warnings);

// What the start of a file shows it to be, whatever its suffix says
// ("rtf", "docx", "odt", "html" or empty). Old Word users often saved RTF or
// web pages under ".doc".
QString sniffFormat(const QByteArray &head);

} // namespace importer
} // namespace qb
