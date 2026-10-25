// qb_web_extract <page.html> [address] [--plain] [--whole]: what the article finder makes of a saved page.
#include "Article.h"
#include "Html.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2)
        return 2;
    QFile f(args.at(1));
    if (!f.open(QIODevice::ReadOnly))
        return 2;
    const QString html = qbweb::decodeHtml(f.readAll(), QString());
    const int find = args.indexOf(QStringLiteral("--path"));
    if (find > 0 && find + 1 < args.size()) {
        // the elements around the first text that holds the given words
        qbweb::Document d;
        d.parse(html);
        QTextStream out(stdout);
        bool done = false;
        d.root()->forEach([&](qbweb::Node *n) {
            if (done || n->type != qbweb::Node::Text || !n->text.contains(args.at(find + 1)))
                return;
            done = true;
            for (qbweb::Node *p = n->parent; p; p = p->parent)
                out << "<" << p->tag << " class=\"" << p->attr(QStringLiteral("class")) << "\" id=\"" << p->attr(QStringLiteral("id")) << "\">\n";
        });
        return 0;
    }
    QUrl url(args.size() > 2 && !args.at(2).startsWith(QLatin1String("--")) ? args.at(2) : QStringLiteral("http://example.com/"));
    qbweb::ExtractOptions o;
    o.articleOnly = !args.contains(QStringLiteral("--whole"));
    o.addTitle = !args.contains(QStringLiteral("--plain"));
    const qbweb::Extracted e = qbweb::extract(html, url, o);
    QTextStream out(stdout);
    const bool plain = args.contains(QStringLiteral("--plain"));
    if (!plain)
        out << "TITLE " << e.info.title << "\nBYLINE " << e.info.byline << "\nSITE " << e.info.siteName << "\nDATE "
            << e.info.published << "\nEXCERPT " << e.info.excerpt << "\nWORDS " << e.words << " IMAGES " << e.images.size()
            << (e.fromScriptData ? " (json-ld)" : "") << "\n----\n";
    for (const auto &p : e.content.paragraphs) {
        QString t;
        for (const auto &r : p.runs)
            t += r.image >= 0 ? (plain ? QString() : QStringLiteral("[img %1 %2]").arg(r.image).arg(e.images.value(r.image).url.toString()))
                              : r.text;
        t.replace(QChar::LineSeparator, QLatin1Char('\n'));
        if (!plain) {
            if (!p.style.isEmpty()) t = QStringLiteral("<") + p.style + QStringLiteral("> ") + t;
            if (p.headingLevel) t = QStringLiteral("<H%1> ").arg(p.headingLevel) + t;
            if (p.listLevel >= 0) t = QStringLiteral("<%1%2> ").arg(p.numbered ? QStringLiteral("#") : QStringLiteral("*")).arg(p.listLevel) + t;
            if (p.tableRow) t = QStringLiteral("<row> ") + t;
        }
        out << t << "\n";
    }
    return 0;
}
