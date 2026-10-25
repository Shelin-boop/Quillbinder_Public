#pragma once
// Finds the article of a web page (the text a reader came for, without menus,
// adverts, comment sections …) and turns it into importer content.
//
// The method follows the well-known approach of "readability" tools: every
// paragraph gives points to its enclosing elements (more for long text and
// commas, less for links), class and id names count for or against an
// element ("article", "content" / "sidebar", "comment" …), the element with
// the most points is the article, neighbors that look like text are added,
// and lists, tables and boxes that look like navigation are cleaned out.
// Metadata (title, author, site, date, description) comes from the page's
// meta tags and JSON-LD.

#include "import/ImporterApi.h"

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

namespace qbweb {

struct PageInfo {
    QString title, byline, siteName, published, excerpt, lang;
};

struct ImageRef {
    QUrl url;                  // to fetch
    QByteArray data;           // a data: address, already decoded
    QString alt;
};

struct ExtractOptions {
    bool articleOnly = true;   // false: the whole page (without scripts, forms, hidden parts)
    bool pictures = true;
    bool addTitle = true;      // a Title paragraph at the top (unless the text starts with it)
};

struct Extracted {
    qb::importers::Content content;  // content.images[i].data is empty until the pictures are fetched
    QVector<ImageRef> images;        // the same indices as content.images
    PageInfo info;
    int words = 0;
    bool fromScriptData = false;     // the text came from the page's JSON-LD (articleBody)
};

Extracted extract(const QString &html, const QUrl &pageUrl, const ExtractOptions &options);

// HTML of content (for the preview and for web page documents); pictures as
// <img src="names[i]">.
QString toHtml(const qb::importers::Content &c, const QStringList &imageNames, const QString &title,
               const QString &sourceLine);

int countWords(const qb::importers::Content &c);

} // namespace qbweb
