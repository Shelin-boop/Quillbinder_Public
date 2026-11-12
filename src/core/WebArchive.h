#pragma once
// Web pages kept in one file (0.81.3): the MIME archive ("MHTML", RFC 2557) in which Scrivener for Windows saves a web
// page (content.mht: the page as text/html, then its pictures and style sheets, each with its address in
// Content-Location). This program's web import keeps a page as an HTML file with its pictures in folders beside it.
//
//  * pack: an HTML page and the files in the folders beside it into one archive (for a .scriv);
//  * unpack: an archive into a folder, as an HTML file whose pictures point to the files beside it (to show the page
//    here or in a browser: Firefox cannot open .mht files).

#include <QByteArray>
#include <QString>

namespace qb::webarchive {

// The archive of `htmlFile`; every file in the folders beside it (not the files beside it) goes in as a part whose
// address is its path read from the page's address `url` (a page without one gets file:///page.html).
QByteArray pack(const QString &htmlFile, const QString &url);

// Writes the archive's page as `<outDir>/index.html` (UTF-8, its links to the archive's other parts pointing to their
// files in outDir) and the other parts beside it. *page = that file; *url = the page's address in the archive.
bool unpack(const QByteArray &archive, const QString &outDir, QString *page, QString *url = nullptr, QString *error = nullptr);

// The page's address in an archive ("" = none).
QString addressOf(const QByteArray &archive);

} // namespace qb::webarchive
