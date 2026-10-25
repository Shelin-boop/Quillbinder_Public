#pragma once
// Lays a text document out on pages (page view) and back.
//
// The pages are a property of the document's layout, so every editor that
// shows the document sees the same pages. Nothing here touches the text,
// the undo history or the "modified" state of the document.

#include "core/PageSetup.h"

class QTextDocument;

namespace qb {
namespace pagelayout {

// Pages of m.paperW x m.step() pixels; the page margins (and half the gap
// between two sheets above and below) become the margins of the text.
// Returns true if the layout changed.
bool apply(QTextDocument *doc, const PageMetrics &m);
// Back to one endless column of the given width.
bool clear(QTextDocument *doc, int width);
bool isPaged(const QTextDocument *doc);
// True if the document is laid out on exactly these pages.
bool matches(const QTextDocument *doc, const PageMetrics &m);
// The metrics the document was laid out with (valid while isPaged()).
PageMetrics metrics(const QTextDocument *doc);
int pageCount(const QTextDocument *doc);
// Bottom edge of the last line in layout coordinates.
int textBottom(const QTextDocument *doc);

} // namespace pagelayout
} // namespace qb
