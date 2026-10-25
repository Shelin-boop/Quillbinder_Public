#pragma once
// RTF import / export for QTextDocument.
//
// The writer produces standard RTF that Word, LibreOffice and Scrivener can
// open. Notes are stored in small custom groups ({\qbannot ...} etc.) which
// other programs ignore while still showing the text.
//
// The reader understands the common RTF subset (fonts, colours, styles,
// paragraph formatting, fields/links, pictures, footnotes) plus the markers
// Scrivener uses for comments and document links.

#include "core/ProjectTypes.h"

#include <QByteArray>
#include <QHash>
#include <QString>

class QTextDocument;

namespace qb {
namespace rtf {

struct ReadOptions {
    // Keep fonts, sizes, colours and paragraph metrics (RTF projects).
    // When false only semantic formatting is kept (for conversion to Markdown).
    bool keepFormatting = true;
};

struct ReadResult {
    bool ok = false;
    QString error;
    // Scrivener linked comment ids (scrivcmt://ID) found in the text.
    QStringList linkedNoteIds;
    // statistics used to guess the project's body font
    QHash<QString, int> fontUsage;   // family -> characters
    QHash<int, int> sizeUsage;       // half-points -> characters
};

ReadResult read(const QByteArray &data, QTextDocument *doc, const Formatting &f,
                const ReadOptions &opt = ReadOptions());
QByteArray write(const QTextDocument *doc, const Formatting &f);

// Quick plain-text extraction (used for synopses stored as RTF, comments ...)
QString toPlainText(const QByteArray &data);

bool looksLikeRtf(const QByteArray &data);

} // namespace rtf
} // namespace qb
