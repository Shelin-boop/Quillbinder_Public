#include "compile/EpubExporter.h"

#include "compile/Look.h"
#include "core/Zip.h"

#include "AppInfo.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QUuid>
#include <functional>

namespace qb {
namespace compile {

namespace {

QString esc(const QString &s)
{
    QString o;
    o.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&': o += QLatin1String("&amp;"); break;
        case '<': o += QLatin1String("&lt;"); break;
        case '>': o += QLatin1String("&gt;"); break;
        case '"': o += QLatin1String("&quot;"); break;
        default:
            if (c.unicode() < 0x20 && c != QLatin1Char('\n') && c != QLatin1Char('\t'))
                o += QLatin1Char(' ');
            else if (c == QChar::LineSeparator)
                o += QLatin1String("<br/>");
            else
                o += c;
        }
    }
    return o;
}

QString slug(const QString &name)
{
    QString s = name.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    s.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return QStringLiteral("s-") + (s.isEmpty() ? QStringLiteral("style") : s);
}

QString num(double v) { return QString::number(v, 'f', 3).remove(QRegularExpression(QStringLiteral("\\.?0+$"))); }

QString alignCss(Qt::Alignment a)
{
    if (a & Qt::AlignHCenter)
        return QStringLiteral("center");
    if (a & Qt::AlignRight)
        return QStringLiteral("right");
    if (a & Qt::AlignJustify)
        return QStringLiteral("justify");
    return QStringLiteral("left");
}

struct Doc {
    QString file;     // "text/part0001.xhtml"
    QString title;
    QString body;
    QStringList notes; // footnotes (pop-up) of this file
};

struct TocItem {
    int level;
    QString text;
    QString href;
};

struct Writer {
    const Manuscript &m;
    QList<Doc> docs;
    QList<TocItem> toc;
    QHash<int, QString> noteHref;          // footnote -> "part0003.xhtml#fn7"
    QHash<int, int> noteNumber;
    QList<int> pending;                    // endnotes waiting
    QStringList allEndnotes;               // placement 2
    QList<QPair<QString, QByteArray>> images; // name, data
    QString imageType(const QString &name) const
    {
        return name.endsWith(QLatin1String(".jpg")) ? QStringLiteral("image/jpeg") : QStringLiteral("image/png");
    }
    int noteCounter = 0;
    QHash<QPair<int, int>, int> listCounters;

    explicit Writer(const Manuscript &m) : m(m) {}

    double em(double mm, double sizePt) const { return mm / 25.4 * 72.0 / qMax(1.0, sizePt); }

    QString imageFor(const QImage &img)
    {
        QByteArray data;
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        QString name;
        if (!img.hasAlphaChannel() && img.width() * img.height() > 300000) {
            name = QStringLiteral("img%1.jpg").arg(images.size() + 1);
            img.save(&buf, "JPEG", 88);
        } else {
            name = QStringLiteral("img%1.png").arg(images.size() + 1);
            img.save(&buf, "PNG");
        }
        images.append({name, data});
        return name;
    }

    QString runs(const Paragraph &p, const look::Para &pl, Doc &d)
    {
        QString o;
        for (const Run &r : p.runs) {
            QString t;
            switch (r.kind) {
            case Run::Text: t = esc(r.text); break;
            case Run::Tab: t = QStringLiteral(" "); break;
            case Run::LineBreak: t = QStringLiteral("<br/>"); break;
            case Run::Image: {
                if (r.image.isNull())
                    continue;
                const QString name = imageFor(r.image);
                const double wPt = r.imageSize.width() > 0 ? r.imageSize.width() : r.image.width() * 0.75;
                const double pct = qBound(5.0, wPt / 450.0 * 100.0, 100.0);
                o += QStringLiteral("<img src=\"../images/%1\" alt=\"\" style=\"width: %2%\"/>").arg(name, num(pct));
                continue;
            }
            case Run::FootnoteRef: {
                if (r.footnote < 0 || r.footnote >= m.footnotes.size())
                    continue;
                const int n = ++noteCounter;
                noteNumber.insert(r.footnote, n);
                const QString notesFile = m.options.footnotePlacement == 2 ? QStringLiteral("notes.xhtml") : QString();
                o += QStringLiteral("<a epub:type=\"noteref\" class=\"noteref\" id=\"fnref%1\" href=\"%2#fn%1\"><sup>%1</sup></a>")
                         .arg(n)
                         .arg(notesFile);
                const QString text = noteText(r.footnote);
                const QString back = QStringLiteral("%1#fnref%2").arg(notesFile.isEmpty() ? QString() : d.file.mid(5)).arg(n);
                const QString note = QStringLiteral("<aside epub:type=\"%4\" class=\"footnote\" id=\"fn%1\"><p><a href=\"%3\">%1.</a> %2</p></aside>")
                                         .arg(n)
                                         .arg(text, notesFile.isEmpty() ? QStringLiteral("#fnref%1").arg(n) : back,
                                              m.options.footnotePlacement == 0 ? QStringLiteral("footnote") : QStringLiteral("endnote"));
                if (m.options.footnotePlacement == 2)
                    allEndnotes << note;
                else
                    d.notes << note;
                continue;
            }
            }
            if (t.isEmpty())
                continue;
            QStringList css;
            if (!r.fontFamily.isEmpty())
                css << QStringLiteral("font-family: \"%1\"").arg(QString(r.fontFamily).remove(QLatin1Char('"')));
            if (r.fontSize > 0)
                css << QStringLiteral("font-size: %1em").arg(num(r.fontSize / qMax(1.0, pl.size)));
            if (r.color.isValid())
                css << QStringLiteral("color: %1").arg(r.color.name());
            if (r.highlight.isValid())
                css << QStringLiteral("background-color: %1").arg(r.highlight.name());
            if (r.capitals == 1)
                css << QStringLiteral("font-variant: small-caps");
            else if (r.capitals == 2)
                css << QStringLiteral("text-transform: uppercase");
            if (r.letterSpacingEm != 0)
                css << QStringLiteral("letter-spacing: %1em").arg(num(r.letterSpacingEm));
            if (r.fontWeight > 0 && r.fontWeight != 700 && r.fontWeight != 400)
                css << QStringLiteral("font-weight: %1").arg(r.fontWeight);
            if (!css.isEmpty() || !r.charStyle.isEmpty()) {
                QString cls = r.charStyle.isEmpty() ? QString() : QStringLiteral(" class=\"%1\"").arg(slug(r.charStyle));
                QString st = css.isEmpty() ? QString() : QStringLiteral(" style=\"%1\"").arg(esc(css.join(QLatin1String("; "))));
                t = QStringLiteral("<span%1%2>%3</span>").arg(cls, st, t);
            }
            if (r.superscript)
                t = QStringLiteral("<sup>") + t + QStringLiteral("</sup>");
            if (r.subscript)
                t = QStringLiteral("<sub>") + t + QStringLiteral("</sub>");
            if (r.strike)
                t = QStringLiteral("<s>") + t + QStringLiteral("</s>");
            if (r.underline)
                t = QStringLiteral("<u>") + t + QStringLiteral("</u>");
            if (r.italic)
                t = QStringLiteral("<i>") + t + QStringLiteral("</i>");
            if (r.bold || (r.fontWeight >= 600))
                t = QStringLiteral("<b>") + t + QStringLiteral("</b>");
            if (!r.link.isEmpty())
                t = QStringLiteral("<a href=\"%1\">%2</a>").arg(esc(r.link), t);
            o += t;
        }
        return o;
    }

    QString noteText(int index)
    {
        QStringList parts;
        Doc dummy;
        for (const Paragraph &p : m.footnotes[index].paragraphs) {
            const look::Para pl = look::paragraph(m, p);
            // notes inside notes are not a thing; runs only
            Paragraph q = p;
            for (int i = q.runs.size() - 1; i >= 0; --i)
                if (q.runs[i].kind == Run::FootnoteRef)
                    q.runs.removeAt(i);
            parts << runs(q, pl, dummy);
        }
        return parts.join(QStringLiteral("<br/>"));
    }

    QString paraStyle(const Paragraph &p, const look::Para &pl)
    {
        QStringList css;
        if (p.direct & Paragraph::DAlign)
            css << QStringLiteral("text-align: %1").arg(alignCss(pl.align));
        if (p.direct & Paragraph::DLeft)
            css << QStringLiteral("margin-left: %1em").arg(num(em(pl.leftMm, pl.size)));
        if (p.direct & Paragraph::DRight)
            css << QStringLiteral("margin-right: %1em").arg(num(em(pl.rightMm, pl.size)));
        if (p.direct & Paragraph::DFirst)
            css << QStringLiteral("text-indent: %1em").arg(num(em(pl.firstMm, pl.size)));
        if (p.direct & Paragraph::DBefore)
            css << QStringLiteral("margin-top: %1em").arg(num(pl.beforePt / qMax(1.0, pl.size)));
        if (p.direct & Paragraph::DAfter)
            css << QStringLiteral("margin-bottom: %1em").arg(num(pl.afterPt / qMax(1.0, pl.size)));
        if ((p.direct & Paragraph::DLine) && pl.lineSpacing > 0)
            css << QStringLiteral("line-height: %1").arg(num(pl.lineSpacing));
        if (!(p.direct & Paragraph::DAlign) && p.align != Qt::Alignment() && p.style == Paragraph::Normal)
            css << QStringLiteral("text-align: %1").arg(alignCss(p.align));
        return css.isEmpty() ? QString() : QStringLiteral(" style=\"%1\"").arg(esc(css.join(QLatin1String("; "))));
    }

    void endnotesSection(Doc &d)
    {
        if (d.notes.isEmpty() || m.options.footnotePlacement != 1)
            return;
        d.body += QStringLiteral("<section epub:type=\"endnotes\" class=\"endnotes\"><h3>%1</h3>\n").arg(esc(m.options.endnotesTitle));
        d.body += d.notes.join(QLatin1Char('\n'));
        d.body += QStringLiteral("\n</section>\n");
        d.notes.clear();
    }

    void build()
    {
        Doc cur;
        auto startDoc = [&]() {
            if (!cur.body.trimmed().isEmpty()) {
                endnotesSection(cur);
                docs << cur;
            }
            cur = Doc();
            cur.file = QStringLiteral("text/part%1.xhtml").arg(docs.size() + 1, 4, 10, QLatin1Char('0'));
        };
        startDoc();
        QStringList listStack; // "ul" / "ol" of the open lists
        auto closeLists = [&](int toDepth) {
            while (listStack.size() > toDepth) {
                cur.body += QStringLiteral("</li></%1>\n").arg(listStack.takeLast());
            }
        };
        bool inQuote = false;
        for (int i = 0; i < m.paragraphs.size(); ++i) {
            const Paragraph &p = m.paragraphs[i];
            if (p.pageBreakBefore && m.options.splitAtPageBreaks && !cur.body.trimmed().isEmpty()) {
                closeLists(0);
                if (inQuote) {
                    cur.body += QStringLiteral("</blockquote>\n");
                    inQuote = false;
                }
                startDoc();
            }
            const look::Para pl = look::paragraph(m, p);
            const bool isList = p.style == Paragraph::ListBullet || p.style == Paragraph::ListNumber;
            if (!isList)
                closeLists(0);
            if (p.style != Paragraph::BlockQuote && inQuote) {
                cur.body += QStringLiteral("</blockquote>\n");
                inQuote = false;
            }
            const QString content = runs(p, pl, cur);
            const QString idAttr = p.anchor.isEmpty() ? QString() : QStringLiteral(" id=\"%1\"").arg(p.anchor);
            QStringList classes;
            if (!p.styleName.isEmpty() && p.style == Paragraph::Normal)
                classes << slug(p.styleName);
            if (p.noIndent)
                classes << QStringLiteral("noindent");
            if (p.pageBreakBefore && !m.options.splitAtPageBreaks)
                classes << QStringLiteral("pagebreak");
            const QString style = paraStyle(p, pl);
            auto cls = [&](const QStringList &extra) {
                const QStringList all = extra + classes;
                return all.isEmpty() ? QString() : QStringLiteral(" class=\"%1\"").arg(all.join(QLatin1Char(' ')));
            };
            if (cur.title.isEmpty() && p.outlineLevel > 0)
                cur.title = p.tocText.isEmpty() ? p.plainText().simplified() : p.tocText;
            if (p.outlineLevel > 0 && p.outlineLevel <= qMax(1, m.options.tocLevels)) {
                const QString text = p.tocText.isEmpty() ? p.plainText().simplified() : p.tocText.simplified();
                if (!text.isEmpty())
                    toc.append({p.outlineLevel, text, cur.file.mid(5) + QLatin1Char('#') + p.anchor});
            }
            const bool heading = p.style == Paragraph::Heading1 || p.style == Paragraph::Heading2 || p.style == Paragraph::Heading3
                                 || (p.style == Paragraph::Title && p.outlineLevel > 0);
            if (heading && p.outlineLevel > 0) {
                const int h = qBound(1, p.outlineLevel, 6);
                cur.body += QStringLiteral("<h%1%2%3%4>%5</h%1>\n").arg(h).arg(idAttr, cls({}), style, content);
                continue;
            }
            if (isList) {
                const int depth = p.listLevel + 1;
                const QString tag = p.style == Paragraph::ListNumber ? QStringLiteral("ol") : QStringLiteral("ul");
                if (listStack.size() >= depth && listStack.size() > 0) {
                    closeLists(depth);
                    cur.body += QStringLiteral("</li>\n");
                }
                while (listStack.size() < depth) {
                    cur.body += QStringLiteral("<%1>\n").arg(tag);
                    listStack << tag;
                }
                cur.body += QStringLiteral("<li%1>%2").arg(idAttr, content);
                continue;
            }
            QString tagClass;
            switch (p.style) {
            case Paragraph::Title: tagClass = QStringLiteral("title"); break;
            case Paragraph::Subtitle: tagClass = QStringLiteral("subtitle"); break;
            case Paragraph::Heading1: tagClass = QStringLiteral("heading1"); break;
            case Paragraph::Heading2: tagClass = QStringLiteral("heading2"); break;
            case Paragraph::Heading3: tagClass = QStringLiteral("heading3"); break;
            case Paragraph::Centered: tagClass = QStringLiteral("centered"); break;
            case Paragraph::Separator: tagClass = QStringLiteral("separator"); break;
            case Paragraph::Caption: tagClass = QStringLiteral("caption"); break;
            case Paragraph::Synopsis: tagClass = QStringLiteral("synopsis"); break;
            default: break;
            }
            if (p.style == Paragraph::BlockQuote && !inQuote) {
                cur.body += QStringLiteral("<blockquote>\n");
                inQuote = true;
            }
            const bool empty = p.runs.isEmpty() || p.plainText().trimmed().isEmpty() && !content.contains(QLatin1String("<img"));
            cur.body += QStringLiteral("<p%1%2%3>%4</p>\n")
                            .arg(idAttr, cls(tagClass.isEmpty() ? (empty ? QStringList{QStringLiteral("blank")} : QStringList())
                                                                : QStringList{tagClass}),
                                 style, empty ? QStringLiteral("&#160;") : content);
        }
        closeLists(0);
        if (inQuote)
            cur.body += QStringLiteral("</blockquote>\n");
        if (!cur.body.trimmed().isEmpty()) {
            endnotesSection(cur);
            docs << cur;
        }
        if (docs.isEmpty()) {
            Doc d;
            d.file = QStringLiteral("text/part0001.xhtml");
            d.body = QStringLiteral("<p class=\"blank\">&#160;</p>\n");
            docs << d;
        }
        for (Doc &d : docs)
            if (d.title.isEmpty())
                d.title = m.title;
        if (toc.isEmpty())
            for (const Doc &d : std::as_const(docs))
                toc.append({1, d.title.isEmpty() ? QObject::tr("Start") : d.title, d.file.mid(5)});
    }

    QString xhtml(const QString &title, const QString &body, const QString &cssPath, const QString &bodyClass = QString()) const
    {
        return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n"
                              "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" "
                              "xml:lang=\"%1\" lang=\"%1\">\n<head>\n<meta charset=\"UTF-8\"/>\n<title>%2</title>\n"
                              "<link rel=\"stylesheet\" type=\"text/css\" href=\"%3\"/>\n</head>\n<body%4>\n%5</body>\n</html>\n")
            .arg(esc(m.language), esc(title), cssPath,
                 bodyClass.isEmpty() ? QString() : QStringLiteral(" class=\"%1\"").arg(bodyClass), body);
    }

    QString css() const
    {
        const double hs = m.headingScale;
        const QString hfont = m.headingFontFamily.isEmpty() ? m.fontFamily : m.headingFontFamily;
        QString c;
        c += QStringLiteral("@charset \"UTF-8\";\n");
        c += QStringLiteral("body { font-family: \"%1\", serif; margin: 0 2%; }\n").arg(QString(m.fontFamily).remove(QLatin1Char('"')));
        c += QStringLiteral("p { margin: 0 0 %1em 0; text-indent: %2em; line-height: %3; text-align: %4; }\n")
                 .arg(num(m.paragraphSpacingPt / qMax(1.0, m.fontSize)))
                 .arg(num(em(m.firstLineIndentMm, m.fontSize)))
                 .arg(num(qMax(1.0, m.lineSpacing)))
                 .arg(m.justify ? QStringLiteral("justify") : QStringLiteral("left"));
        c += QStringLiteral("p.noindent, p.title, p.subtitle, p.centered, p.separator, p.caption, p.synopsis, p.blank, li, "
                            "blockquote p { text-indent: 0; }\n");
        auto hcss = [&](const QString &sel, double f, bool italic) {
            return QStringLiteral("%1 { font-family: \"%2\", serif; font-size: %3em; font-weight: %4; font-style: %5; "
                                  "text-align: center; margin: 1.5em 0 0.75em 0; page-break-after: avoid; }\n")
                .arg(sel, QString(hfont).remove(QLatin1Char('"')), num(f), m.headingBold ? QStringLiteral("bold") : QStringLiteral("normal"),
                     italic ? QStringLiteral("italic") : QStringLiteral("normal"));
        };
        c += hcss(QStringLiteral("h1, p.title"), qMax(1.2, hs * 1.3), false);
        c += hcss(QStringLiteral("h2, p.heading1"), qMax(1.1, hs), false);
        c += hcss(QStringLiteral("h3, p.heading2"), 1 + (qMax(1.0, hs) - 1) * 0.6, false);
        c += hcss(QStringLiteral("h4, h5, h6, p.heading3"), 1 + (qMax(1.0, hs) - 1) * 0.3, hs <= 1.01);
        c += QStringLiteral("p.subtitle { font-style: italic; text-align: center; font-size: %1em; }\n").arg(num(qMax(1.1, hs * 0.9)));
        c += QStringLiteral("p.centered, p.separator, p.caption { text-align: center; }\n");
        c += QStringLiteral("p.separator { margin: 1em 0; }\np.caption, p.synopsis { font-style: italic; }\n");
        c += QStringLiteral("p.pagebreak { page-break-before: always; }\n");
        c += QStringLiteral("blockquote { margin: 0.5em 2em; }\nimg { max-width: 100%; }\n");
        c += QStringLiteral("a.noteref { text-decoration: none; }\naside.footnote, section.endnotes { font-size: 0.85em; }\n");
        c += QStringLiteral("aside.footnote p, section.endnotes p { text-indent: 0; }\n");
        c += QStringLiteral("nav ol { list-style-type: none; padding-left: 1em; }\nbody.cover { margin: 0; text-align: center; }\n");
        c += QStringLiteral("body.cover img { max-height: 100%; max-width: 100%; }\n");
        // the own styles of the text
        for (const NamedStyle &s : m.styles) {
            QStringList css;
            if (!s.fontFamily.isEmpty())
                css << QStringLiteral("font-family: \"%1\"").arg(QString(s.fontFamily).remove(QLatin1Char('"')));
            if (s.fontSize > 0)
                css << QStringLiteral("font-size: %1em").arg(num(s.fontSize / qMax(1.0, m.fontSize)));
            if (s.bold >= 0)
                css << QStringLiteral("font-weight: %1").arg(s.bold ? QStringLiteral("bold") : QStringLiteral("normal"));
            if (s.italic >= 0)
                css << QStringLiteral("font-style: %1").arg(s.italic ? QStringLiteral("italic") : QStringLiteral("normal"));
            if (s.underline > 0)
                css << QStringLiteral("text-decoration: underline");
            if (s.color.isValid())
                css << QStringLiteral("color: %1").arg(s.color.name());
            if (s.capitals == 1)
                css << QStringLiteral("font-variant: small-caps");
            else if (s.capitals == 2)
                css << QStringLiteral("text-transform: uppercase");
            if (!s.character) {
                if (s.align >= 0)
                    css << QStringLiteral("text-align: %1").arg(QStringList{QStringLiteral("left"), QStringLiteral("center"),
                                                                            QStringLiteral("right"), QStringLiteral("justify")}
                                                                    .value(s.align));
                if (s.leftIndentMm >= 0)
                    css << QStringLiteral("margin-left: %1em").arg(num(em(s.leftIndentMm, m.fontSize)));
                if (s.rightIndentMm >= 0)
                    css << QStringLiteral("margin-right: %1em").arg(num(em(s.rightIndentMm, m.fontSize)));
                if (s.firstLineMm > -999)
                    css << QStringLiteral("text-indent: %1em").arg(num(em(s.firstLineMm, m.fontSize)));
                if (s.spaceBeforePt >= 0)
                    css << QStringLiteral("margin-top: %1em").arg(num(s.spaceBeforePt / qMax(1.0, m.fontSize)));
                if (s.spaceAfterPt >= 0)
                    css << QStringLiteral("margin-bottom: %1em").arg(num(s.spaceAfterPt / qMax(1.0, m.fontSize)));
                if (s.lineSpacing > 0)
                    css << QStringLiteral("line-height: %1").arg(num(s.lineSpacing));
            }
            if (!css.isEmpty())
                c += QStringLiteral("%1.%2 { %3; }\n").arg(s.character ? QStringLiteral("span") : QStringLiteral("p"), slug(s.name),
                                                         css.join(QLatin1String("; ")));
        }
        return c;
    }
};

// nested lists from the levels (a level never jumps more than one step)
QString navList(const QList<TocItem> &items, const QString &prefix)
{
    struct Node {
        int item;
        QList<int> kids;
    };
    QList<Node> nodes;
    QList<int> top, stack;
    QList<int> levels;
    for (int i = 0; i < items.size(); ++i) {
        int l = qMax(1, items[i].level);
        if (stack.isEmpty())
            l = 1;
        else
            l = qMin(l, levels.value(stack.last()) + 1);
        while (!stack.isEmpty() && levels.value(stack.last()) >= l)
            stack.removeLast();
        nodes.append(Node{i, QList<int>()});
        levels << l;
        if (stack.isEmpty())
            top << i;
        else
            nodes[stack.last()].kids << i;
        stack << i;
    }
    std::function<QString(const QList<int> &)> emitList = [&](const QList<int> &list) {
        QString o = QStringLiteral("<ol>\n");
        for (int i : list) {
            o += QStringLiteral("<li><a href=\"%1%2\">%3</a>").arg(prefix, esc(items[i].href), esc(items[i].text));
            if (!nodes[i].kids.isEmpty())
                o += QLatin1Char('\n') + emitList(nodes[i].kids);
            o += QStringLiteral("</li>\n");
        }
        return o + QStringLiteral("</ol>\n");
    };
    return emitList(top);
}

bool validDate(const QString &d)
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}(-\\d{2}(-\\d{2})?)?$"));
    return re.match(d).hasMatch();
}

} // namespace

bool writeEpub(const Manuscript &m, const QString &path, QString *error, EpubReport *report)
{
    Writer w(m);
    w.build();

    const QString uid = m.options.identifier.isEmpty() ? QStringLiteral("urn:uuid:") + QUuid::createUuid().toString(QUuid::WithoutBraces)
                        : (m.options.identifier.contains(QLatin1Char(':')) ? m.options.identifier
                                                                            : QStringLiteral("urn:uuid:") + m.options.identifier);
    QFile::remove(path);
    ZipWriter zip(path);
    if (!zip.isOpen()) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    // the first entry, stored as it is (the EPUB rules)
    zip.addFile(QStringLiteral("mimetype"), "application/epub+zip", false);
    zip.addFile(QStringLiteral("META-INF/container.xml"),
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<container version=\"1.0\" "
                "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n<rootfiles>\n"
                "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
                "</rootfiles>\n</container>\n");
    zip.addFile(QStringLiteral("OEBPS/styles/book.css"), w.css().toUtf8());

    QString manifest, spine;
    const bool cover = !m.options.cover.isNull();
    if (cover) {
        QByteArray data;
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        m.options.cover.save(&buf, "JPEG", 90);
        zip.addFile(QStringLiteral("OEBPS/images/cover.jpg"), data);
        manifest += QStringLiteral("<item id=\"cover-image\" href=\"images/cover.jpg\" media-type=\"image/jpeg\" properties=\"cover-image\"/>\n");
        zip.addFile(QStringLiteral("OEBPS/text/cover.xhtml"),
                    w.xhtml(QObject::tr("Cover"),
                            QStringLiteral("<div><img src=\"../images/cover.jpg\" alt=\"%1\"/></div>\n").arg(esc(m.title)),
                            QStringLiteral("../styles/book.css"), QStringLiteral("cover"))
                        .toUtf8());
        manifest += QStringLiteral("<item id=\"cover\" href=\"text/cover.xhtml\" media-type=\"application/xhtml+xml\"/>\n");
        spine += QStringLiteral("<itemref idref=\"cover\" linear=\"no\"/>\n");
    }
    // contents of the reader (EPUB 3) and of older readers (NCX)
    const QString nav = QStringLiteral("<nav epub:type=\"toc\" id=\"toc\">\n<h1>%1</h1>\n%2</nav>\n")
                            .arg(esc(m.options.tocTitle), navList(w.toc, QStringLiteral("text/")));
    zip.addFile(QStringLiteral("OEBPS/nav.xhtml"), w.xhtml(m.options.tocTitle, nav, QStringLiteral("styles/book.css")).toUtf8());
    manifest += QStringLiteral("<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n");
    if (m.options.toc && m.options.tocPage)
        spine += QStringLiteral("<itemref idref=\"nav\"/>\n");
    for (int i = 0; i < w.docs.size(); ++i) {
        Doc &d = w.docs[i];
        QString body = d.body;
        if (!d.notes.isEmpty())
            body += d.notes.join(QLatin1Char('\n')) + QLatin1Char('\n');
        zip.addFile(QStringLiteral("OEBPS/") + d.file, w.xhtml(d.title, body, QStringLiteral("../styles/book.css")).toUtf8());
        manifest += QStringLiteral("<item id=\"part%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>\n").arg(i + 1).arg(d.file);
        spine += QStringLiteral("<itemref idref=\"part%1\"/>\n").arg(i + 1);
    }
    if (!w.allEndnotes.isEmpty()) {
        const QString body = QStringLiteral("<section epub:type=\"endnotes\" class=\"endnotes\">\n<h2>%1</h2>\n%2\n</section>\n")
                                 .arg(esc(m.options.endnotesTitle), w.allEndnotes.join(QLatin1Char('\n')));
        zip.addFile(QStringLiteral("OEBPS/text/notes.xhtml"), w.xhtml(m.options.endnotesTitle, body, QStringLiteral("../styles/book.css")).toUtf8());
        manifest += QStringLiteral("<item id=\"notes\" href=\"text/notes.xhtml\" media-type=\"application/xhtml+xml\"/>\n");
        spine += QStringLiteral("<itemref idref=\"notes\"/>\n");
    }
    for (int i = 0; i < w.images.size(); ++i) {
        zip.addFile(QStringLiteral("OEBPS/images/") + w.images[i].first, w.images[i].second, false);
        manifest += QStringLiteral("<item id=\"img%1\" href=\"images/%2\" media-type=\"%3\"/>\n")
                        .arg(i + 1)
                        .arg(w.images[i].first, w.imageType(w.images[i].first));
    }
    manifest += QStringLiteral("<item id=\"css\" href=\"styles/book.css\" media-type=\"text/css\"/>\n");
    // NCX
    {
        QString points;
        for (int i = 0; i < w.toc.size(); ++i)
            points += QStringLiteral("<navPoint id=\"np%1\" playOrder=\"%1\"><navLabel><text>%2</text></navLabel><content src=\"text/%3\"/></navPoint>\n")
                          .arg(i + 1)
                          .arg(esc(w.toc[i].text), esc(w.toc[i].href));
        const QString ncx = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n"
                                           "<head><meta name=\"dtb:uid\" content=\"%1\"/><meta name=\"dtb:depth\" content=\"1\"/>"
                                           "<meta name=\"dtb:totalPageCount\" content=\"0\"/><meta name=\"dtb:maxPageNumber\" content=\"0\"/></head>\n"
                                           "<docTitle><text>%2</text></docTitle>\n<navMap>\n%3</navMap>\n</ncx>\n")
                                .arg(esc(uid), esc(m.title), points);
        zip.addFile(QStringLiteral("OEBPS/toc.ncx"), ncx.toUtf8());
        manifest += QStringLiteral("<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n");
    }
    QString meta;
    meta += QStringLiteral("<dc:identifier id=\"bookid\">%1</dc:identifier>\n").arg(esc(uid));
    meta += QStringLiteral("<dc:title>%1</dc:title>\n").arg(esc(m.title.isEmpty() ? QObject::tr("Untitled") : m.title));
    if (!m.author.isEmpty())
        meta += QStringLiteral("<dc:creator id=\"creator\">%1</dc:creator>\n").arg(esc(m.author));
    meta += QStringLiteral("<dc:language>%1</dc:language>\n").arg(esc(m.language.isEmpty() ? QStringLiteral("en") : m.language));
    if (validDate(m.options.date))
        meta += QStringLiteral("<dc:date>%1</dc:date>\n").arg(m.options.date);
    if (!m.options.publisher.isEmpty())
        meta += QStringLiteral("<dc:publisher>%1</dc:publisher>\n").arg(esc(m.options.publisher));
    if (!m.options.description.isEmpty())
        meta += QStringLiteral("<dc:description>%1</dc:description>\n").arg(esc(m.options.description));
    if (!m.options.subject.isEmpty())
        meta += QStringLiteral("<dc:subject>%1</dc:subject>\n").arg(esc(m.options.subject));
    meta += QStringLiteral("<meta property=\"dcterms:modified\">%1</meta>\n")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ")));
    if (cover)
        meta += QStringLiteral("<meta name=\"cover\" content=\"cover-image\"/>\n");
    const QString opf = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                       "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"bookid\" xml:lang=\"%1\">\n"
                                       "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n%2</metadata>\n"
                                       "<manifest>\n%3</manifest>\n<spine toc=\"ncx\">\n%4</spine>\n</package>\n")
                            .arg(esc(m.language.isEmpty() ? QStringLiteral("en") : m.language), meta, manifest, spine);
    zip.addFile(QStringLiteral("OEBPS/content.opf"), opf.toUtf8());
    if (!zip.close()) {
        if (error)
            *error = zip.errorString();
        return false;
    }
    if (report) {
        report->files = w.docs.size();
        report->images = w.images.size();
        report->tocEntries = w.toc.size();
        report->notes = w.noteCounter;
    }
    return true;
}

} // namespace compile
} // namespace qb
