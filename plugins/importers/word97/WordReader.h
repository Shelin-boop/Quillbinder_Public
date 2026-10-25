#pragma once
// Reader of Word 97–2003 documents (.doc, .dot; "Word binary file format",
// [MS-DOC]) into the content structs of the importer interface.
//
// Read: the text of the document with its paragraphs and characters
// (bold, italic, underline, strikethrough, small and all capitals,
// superscript and subscript, font, size, color, highlight), paragraph styles
// (headings, title, quotes … by name or outline level), alignment and
// indents, lists (bullets / numbers, levels), tables (a paragraph per row,
// cells separated by tabs), page and section breaks, line breaks, fields
// (their result; hyperlinks become links), footnotes and endnotes,
// comments with their author and the commented text, inline pictures (JPEG,
// PNG, BMP, TIFF), text boxes (added at the end). Hidden text and text
// deleted with tracked changes are left out, text inserted with tracked
// changes is kept.
//
// Not read: headers and footers, floating pictures and drawings, metafile
// pictures (WMF / EMF / PICT), embedded objects, Word 6.0 / 95 files and
// password-protected files (Failure says which).

#include "import/ImporterApi.h"

#include <QByteArray>
#include <QString>

namespace qbdoc {

enum class Failure { None, NotWord, OldWord, Encrypted, Damaged };

Failure readWord97(const QByteArray &file, qb::importers::Content *out, QString *error);

} // namespace qbdoc
