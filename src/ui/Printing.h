#pragma once
// File > Print Current Document and File > Print Preview: what the active
// editor shows, on paper. Texts go through the page maker of the built-in PDF
// output (notes at the bottom of the page, keeping headings with their text);
// index cards, the outliner, pictures, PDF files and web pages are drawn here.
// File > Print Settings decides the paper, header and footer and the options of
// each kind (core/PrintSettings.h).

#include "core/PrintSettings.h"

#include <QList>
#include <QPointer>
#include <QString>

class QPrinter;

namespace qb {

class BinderItem;
class EditorPane;
class Project;
class TextEditor;

namespace printing {

enum class Kind { None, Text, Cards, Outliner, Picture, Pdf, WebPage };

struct Column {
    int column = 0;     // BinderModel column
    double share = 1;   // of the width of the page (all columns together: 1)
};
struct Row {
    BinderItem *item = nullptr;
    int level = 0;      // depth below the outliner's top
};

struct Job {
    Kind kind = Kind::None;
    QPointer<Project> project;
    QString title;                    // <$title> in header and footer
    QList<BinderItem *> items;        // texts: the documents; cards: the cards; picture, PDF, web page: the document
    QList<int> levels;                // texts: 1 = the top level (the size of the title)
    // what "Selection" prints: the text selected in an editor (from..to in the text
    // of selectionItem), or the cards / outliner rows that are selected
    BinderItem *selectionItem = nullptr;
    int selectionStart = -1, selectionEnd = -1;
    QList<BinderItem *> selected;
    // outliner
    QList<Column> columns;
    QList<Row> rows;                  // as listed (open groups)
    QList<Row> allRows;               // with the rows inside closed groups
    bool synopses = true;             // the outliner shows synopses
    bool hasSelection() const { return selectionEnd > selectionStart || !selected.isEmpty(); }
};

// What Print Current Document prints now: what the editor `pane` shows; with the
// keyboard in a text of that editor or of its copyholder (focus), that text.
Job currentJob(EditorPane *pane, TextEditor *focus);
// "the text", "the index cards" ... (for messages and window titles)
QString describe(const Job &job);

// The paper before the print window opens: the paper and orientation of the
// print settings; for index cards one to a sheet, a sheet the size of a card.
void preparePrinter(QPrinter *printer, const Job &job, const PrintSettings &settings);
// Paints the job onto the printer: every page, or the printer's page range;
// with the printer's range "Selection" only what is selected. pages: how many
// pages the printout has (all of them, whatever the range).
bool render(const Job &job, const PrintSettings &settings, QPrinter *printer, QString *error, int *pages = nullptr);

// Header and footer texts with their placeholders filled in (<$p> and <$pages> as well).
QString fillPlaceholders(const QString &text, const Job &job, int page, int pages);

} // namespace printing
} // namespace qb
