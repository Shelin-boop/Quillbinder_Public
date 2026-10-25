#pragma once
// Reading and writing the project's Markdown dialect.
//
// Rules (documented for users in the manual):
//  * Every line is one paragraph (like pressing Enter in a word processor).
//  * Block prefixes: "# " .. "###### " headings, "> " block quote,
//    "- " / "1. " lists (two spaces per nesting level), "->text<-" centred,
//    "::Style Name:: " any other paragraph style.
//  * Inline: **bold**, *italic*, ~~strike~~, ==highlight==, <u>underline</u>,
//    <sup>..</sup>, <sub>..</sub>, [link](url), ![image](path){width=300},
//    <span class="Character Style">..</span>, <span style="font-size:14pt">..</span>,
//    <br> soft line break, \ escapes.
//  * Notes (CriticMarkup based):
//      {>>inline annotation<<}
//      ^[inline footnote]
//      {==anchor text==}{>>linked comment<<}
//      {==anchor text==}^[linked footnote]
//    A comment / annotation may start with "@#rrggbb " to set its colour.

#include "core/ProjectTypes.h"

#include <QImage>
#include <QString>
#include <QTextImageFormat>
#include <functional>

class QTextDocument;

namespace qb {
namespace md {

// Called for every image while writing. Returns the path to put into the
// Markdown (usually relative to the document folder).
using ImageSaver = std::function<QString(const QTextImageFormat &fmt)>;

void read(const QString &markdown, QTextDocument *doc, const Formatting &f);
QString write(const QTextDocument *doc, const Formatting &f, const ImageSaver &saver = {});
// With the built-in styles (tests, text outside of a project).
inline QString write(const QTextDocument *doc) { return write(doc, Formatting()); }

// Plain text of a Markdown string (no QTextDocument needed by callers).
QString toPlainText(const QString &markdown, const Formatting &f = {});

// Conversions between standard Markdown (blank line between paragraphs,
// soft-wrapped lines) and the one-line-per-paragraph dialect.
QString fromStandardMarkdown(const QString &standard);
QString toStandardMarkdown(const QString &dialect);

// Escaping helpers (exposed for tests and the compile exporters)
QString escapeInline(const QString &text, bool escapeBrackets);
// Plain text (one paragraph per line) as dialect Markdown.
QString fromPlainText(const QString &plain);

} // namespace md
} // namespace qb
