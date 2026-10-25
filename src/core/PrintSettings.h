#pragma once
// File > Print Settings: how File > Print Current Document puts texts, index
// cards and the outliner on paper. Kept with the project (uiState
// "printSettings"); the settings of new projects come from the program's
// settings ("Use these settings for new projects").

#include "core/PageSetup.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace qb {

class Project;

struct PrintSettings {
    // ---- page: paper, margins, header and footer (placeholders <$title>,
    // <$projecttitle>, <$author>, <$date>, <$p> page, <$pages> pages)
    PageSetup page;
    QString headerLeft = QStringLiteral("<$title>");
    QString headerCenter;
    QString headerRight = QStringLiteral("<$projecttitle>");
    QString footerLeft;
    QString footerCenter = QStringLiteral("<$p>");
    QString footerRight;
    bool headerOnFirstPage = true;

    // ---- text documents
    bool titles = true;          // the title of each document above its text
    bool synopses = false;       // the synopsis below the title
    bool notes = false;          // the document notes after the text
    bool comments = false;       // comments and inline annotations as notes at the bottom of the page
    bool endnotes = false;       // footnotes at the end instead of the bottom of each page
    bool highlights = false;     // keep highlighted text highlighted
    bool editorFont = true;      // the fonts of the editor; otherwise fontFamily and fontSize for the text
    QString fontFamily;          // empty = the project's font
    double fontSize = 12;        // points
    double lineSpacing = 0;      // 0 = as in the editor; 1, 1.5, 2

    // ---- index cards (the cards of the corkboard)
    QString cardSize = QStringLiteral("3 x 5 in"); // a name of cardSizes() or "Custom"
    double cardWidth = 127, cardHeight = 76.2;     // mm
    bool cardPerPage = false;    // one card on each sheet, the paper as large as the card
    bool cardTitle = true;
    bool cardSynopsis = true;
    bool cardTextIfEmpty = true; // the first lines of the text when there is no synopsis
    bool cardLabel = true;       // label color as a stripe
    bool cardStatus = true;
    bool cardKeywords = true;
    bool cardPictures = true;    // picture documents and pictures of index cards
    bool cardRuled = true;       // the lines of an index card
    bool cardBorder = true;      // a cutting line around each card
    QString cardFont;            // empty = the font of the interface
    double cardFontSize = 10;

    // ---- outliner
    int outlinerSynopses = 0;    // 0 = as the outliner shows them, 1 = always, 2 = never
    bool outlinerAllRows = false; // also the rows of closed groups
    bool outlinerHeader = true;  // the column titles at the top of every page
    bool outlinerLines = true;   // a line between rows
    bool outlinerLabels = true;  // a dot of the label color
    QString outlinerFont;        // empty = the font of the interface
    double outlinerFontSize = 9;

    bool operator==(const PrintSettings &o) const { return toVariant() == o.toVariant(); }
    bool operator!=(const PrintSettings &o) const { return !(*this == o); }
    QVariantMap toVariant() const;
    static PrintSettings fromVariant(const QVariantMap &m, const PrintSettings &fallback);

    struct CardSize {
        QString name;
        double w, h; // mm, as the card lies (wider than high)
    };
    static QList<CardSize> cardSizes();

    // The settings of new projects (empty map = the program's defaults with the page of the page view).
    static QVariantMap standardMap();
    static void setStandardMap(const QVariantMap &m);
    // The settings of a project: its own, else those of new projects; the paper of a
    // project without print settings is the page of its page view.
    static PrintSettings ofProject(const Project *p);
    static void storeInProject(Project *p, const PrintSettings &s);
};

} // namespace qb
