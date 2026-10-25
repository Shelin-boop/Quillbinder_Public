#include "compile/DocxWriter.h"
#include "core/Zip.h"
#include "text/MarkdownIO.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QSet>

namespace qb {
namespace compile {

namespace {

const char *kW = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
const char *kR = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

QString xmlEscape(const QString &s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        const ushort u = c.unicode();
        switch (u) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        default:
            if (u < 0x20 && u != '\t' && u != '\n' && u != '\r')
                break;
            if (u == 0xFFFE || u == 0xFFFF)
                break;
            out += c;
        }
    }
    return out;
}

int twipsFromMm(double mm) { return qRound(mm / 25.4 * 1440.0); }
int twipsFromPt(double pt) { return qRound(pt * 20.0); }
qint64 emuFromPt(double pt) { return qint64(pt * 12700.0); }

class Builder
{
public:
    explicit Builder(const Manuscript &m) : m(m) {}

    QByteArray document();
    QByteArray styles();
    QByteArray settings();
    QByteArray footnotes();
    QByteArray comments();
    QByteArray numbering();
    QByteArray header();
    QByteArray footer();
    QByteArray documentRels();
    QByteArray core();
    QByteArray contentTypes();
    QList<QPair<QString, QByteArray>> media() const { return m_media; }
    bool hasHeader() const
    {
        return !m.page.headerLeft.isEmpty() || !m.page.headerCenter.isEmpty() || !m.page.headerRight.isEmpty();
    }
    bool hasFooter() const
    {
        return !m.page.footerCenter.isEmpty() || !m.page.footerLeft.isEmpty() || !m.page.footerRight.isEmpty();
    }
    // facing pages: the even (left-hand) pages have header and footer of their own
    bool facing() const { return m.page.mirrorHeaders; }
    QByteArray header(bool even);
    QByteArray footer(bool even);

private:
    const Manuscript &m;
    QList<QPair<QString, QByteArray>> m_media;  // name, data
    QList<QPair<QString, QString>> m_imageRels; // rId, target
    QList<QPair<QString, QString>> m_linkRels;  // rId, url
    QMap<int, int> m_listNum;                   // listId -> numId
    QList<QPair<int, bool>> m_nums;             // numId -> numbered
    int m_openComment = -1;
    int m_drawingId = 1;

    QString runProps(const Run &r) const;
    QString paragraph(const Paragraph &p, bool inFootnote);
    QString runs(const QList<Run> &runs, bool inFootnote);
    QString textRun(const Run &r, const QString &text);
    QString imageRun(const Run &r);
    QString styleId(Paragraph::Style s) const;
    QString ownStyleId(const QString &name) const;
    bool hasOwnStyle(const QString &name, bool character) const;
    QString tocField() const;
    QString headerFooterParagraph(const QString &left, const QString &center, const QString &right);
    QString textWithPageField(const QString &t, const QString &rpr);
};

QString Builder::styleId(Paragraph::Style s) const
{
    switch (s) {
    case Paragraph::Title: return QStringLiteral("Title");
    case Paragraph::Subtitle: return QStringLiteral("Subtitle");
    case Paragraph::Heading1: return QStringLiteral("Heading1");
    case Paragraph::Heading2: return QStringLiteral("Heading2");
    case Paragraph::Heading3: return QStringLiteral("Heading3");
    case Paragraph::BlockQuote: return QStringLiteral("Quote");
    case Paragraph::Centered: return QStringLiteral("Centered");
    case Paragraph::Separator: return QStringLiteral("Separator");
    case Paragraph::Caption: return QStringLiteral("Caption");
    case Paragraph::Synopsis: return QStringLiteral("Synopsis");
    case Paragraph::ListBullet:
    case Paragraph::ListNumber: return QStringLiteral("ListParagraph");
    case Paragraph::Normal: break;
    }
    return QString();
}

QString Builder::ownStyleId(const QString &name) const
{
    QString id;
    for (const QChar c : name)
        if (c.isLetterOrNumber())
            id += c;
    return QStringLiteral("Own") + (id.isEmpty() ? QStringLiteral("Style") : id);
}

bool Builder::hasOwnStyle(const QString &name, bool character) const
{
    for (const NamedStyle &s : m.styles)
        if (s.name == name && s.character == character)
            return true;
    return false;
}

// The table of contents as a Word field (Word fills it in when the file is opened)
QString Builder::tocField() const
{
    return QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"TOCHeading\"/></w:pPr><w:r><w:t xml:space=\"preserve\">%1</w:t></w:r></w:p>"
                          "<w:p><w:r><w:fldChar w:fldCharType=\"begin\" w:dirty=\"true\"/></w:r>"
                          "<w:r><w:instrText xml:space=\"preserve\"> TOC \\o \"1-%2\" \\h \\z \\u </w:instrText></w:r>"
                          "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
                          "<w:r><w:t xml:space=\"preserve\">%3</w:t></w:r>"
                          "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>")
        .arg(xmlEscape(m.options.tocTitle))
        .arg(qBound(1, m.options.tocLevels, 6))
        .arg(xmlEscape(QObject::tr("The table of contents is filled in by Word (or: right-click, Update Field).")));
}

QString Builder::runProps(const Run &r) const
{
    // the order of the elements is fixed by the file format
    QString p;
    if (!r.fontFamily.isEmpty()) {
        const QString fam = r.fontFamily.toHtmlEscaped();
        p += QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:eastAsia=\"%1\" w:cs=\"%1\"/>").arg(fam);
    }
    if (r.bold)
        p += QLatin1String("<w:b/><w:bCs/>");
    if (r.italic)
        p += QLatin1String("<w:i/><w:iCs/>");
    if (r.capitals == 2)
        p += QLatin1String("<w:caps/>");
    else if (r.capitals == 1)
        p += QLatin1String("<w:smallCaps/>");
    if (r.strike)
        p += QLatin1String("<w:strike/>");
    if (r.color.isValid())
        p += QStringLiteral("<w:color w:val=\"%1\"/>").arg(r.color.name().mid(1).toUpper());
    if (!qFuzzyIsNull(r.letterSpacingEm)) {
        const double pt = r.fontSize > 0 ? r.fontSize : m.fontSize;
        const int twips = qRound(r.letterSpacingEm * pt * 20);
        if (twips)
            p += QStringLiteral("<w:spacing w:val=\"%1\"/>").arg(twips);
    }
    if (r.fontSize > 0)
        p += QStringLiteral("<w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/>").arg(qRound(r.fontSize * 2));
    if (r.underline)
        p += QLatin1String("<w:u w:val=\"single\"/>");
    if (r.highlight.isValid())
        p += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%1\"/>").arg(r.highlight.name().mid(1).toUpper());
    if (r.superscript)
        p += QLatin1String("<w:vertAlign w:val=\"superscript\"/>");
    else if (r.subscript)
        p += QLatin1String("<w:vertAlign w:val=\"subscript\"/>");
    if (!r.link.isEmpty())
        p.prepend(QLatin1String("<w:rStyle w:val=\"Hyperlink\"/>"));
    else if (!r.charStyle.isEmpty() && hasOwnStyle(r.charStyle, true))
        p.prepend(QStringLiteral("<w:rStyle w:val=\"%1\"/>").arg(ownStyleId(r.charStyle)));
    return p.isEmpty() ? QString() : QLatin1String("<w:rPr>") + p + QLatin1String("</w:rPr>");
}

QString Builder::textRun(const Run &r, const QString &text)
{
    return QLatin1String("<w:r>") + runProps(r) + QLatin1String("<w:t xml:space=\"preserve\">") + xmlEscape(text)
           + QLatin1String("</w:t></w:r>");
}

QString Builder::imageRun(const Run &r)
{
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    r.image.save(&buf, "PNG");
    const int n = m_media.size() + 1;
    const QString name = QStringLiteral("image%1.png").arg(n);
    m_media.append({name, png});
    const QString rid = QStringLiteral("rIdImg%1").arg(n);
    m_imageRels.append({rid, QStringLiteral("media/") + name});
    // limit to the text area width
    double w = r.imageSize.width() > 0 ? r.imageSize.width() : r.image.width() * 0.75;
    double h = r.imageSize.height() > 0 ? r.imageSize.height() : r.image.height() * 0.75;
    const double maxW = (m.page.widthMm - m.page.marginLeftMm - m.page.marginRightMm) / 25.4 * 72.0;
    if (w > maxW && w > 0) {
        h = h * maxW / w;
        w = maxW;
    }
    const qint64 cx = emuFromPt(w), cy = emuFromPt(h);
    const int id = m_drawingId++;
    return QStringLiteral(
               "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
               "<wp:extent cx=\"%1\" cy=\"%2\"/><wp:docPr id=\"%3\" name=\"Picture %3\"/>"
               "<wp:cNvGraphicFramePr><a:graphicFrameLocks xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
               "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
               "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
               "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
               "<pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"%4\"/><pic:cNvPicPr/></pic:nvPicPr>"
               "<pic:blipFill><a:blip r:embed=\"%5\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
               "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
               "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
               "</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>")
        .arg(cx)
        .arg(cy)
        .arg(id)
        .arg(name, rid);
}

QString Builder::runs(const QList<Run> &list, bool inFootnote)
{
    QString out;
    int i = 0;
    while (i < list.size()) {
        const Run &r = list[i];
        // comment ranges (not inside footnotes)
        if (!inFootnote && r.comment != m_openComment) {
            if (m_openComment >= 0) {
                out += QStringLiteral("<w:commentRangeEnd w:id=\"%1\"/><w:r><w:rPr><w:rStyle w:val=\"CommentReference\"/></w:rPr>"
                                      "<w:commentReference w:id=\"%1\"/></w:r>")
                           .arg(m_openComment);
            }
            if (r.comment >= 0)
                out += QStringLiteral("<w:commentRangeStart w:id=\"%1\"/>").arg(r.comment);
            m_openComment = r.comment;
        }
        // hyperlink groups
        if (!r.link.isEmpty() && r.kind == Run::Text) {
            int j = i;
            QString inner;
            while (j < list.size() && list[j].link == r.link && list[j].kind == Run::Text
                   && list[j].comment == r.comment) {
                inner += textRun(list[j], list[j].text);
                ++j;
            }
            const QString rid = QStringLiteral("rIdLink%1").arg(m_linkRels.size() + 1);
            m_linkRels.append({rid, r.link});
            out += QStringLiteral("<w:hyperlink r:id=\"%1\">").arg(rid) + inner + QLatin1String("</w:hyperlink>");
            i = j;
            continue;
        }
        switch (r.kind) {
        case Run::Text:
            if (!r.text.isEmpty())
                out += textRun(r, r.text);
            break;
        case Run::Tab:
            out += QLatin1String("<w:r>") + runProps(r) + QLatin1String("<w:tab/></w:r>");
            break;
        case Run::LineBreak:
            out += QLatin1String("<w:r>") + runProps(r) + QLatin1String("<w:br/></w:r>");
            break;
        case Run::FootnoteRef:
            if (!inFootnote && r.footnote >= 0)
                out += QStringLiteral("<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr>"
                                      "<w:footnoteReference w:id=\"%1\"/></w:r>")
                           .arg(r.footnote + 1);
            break;
        case Run::Image:
            if (!r.image.isNull())
                out += imageRun(r);
            break;
        }
        ++i;
    }
    return out;
}

QString Builder::paragraph(const Paragraph &p, bool inFootnote)
{
    QString ppr;
    QString sid = inFootnote ? QStringLiteral("FootnoteText") : styleId(p.style);
    if (!inFootnote && sid.isEmpty() && !p.styleName.isEmpty() && hasOwnStyle(p.styleName, false))
        sid = ownStyleId(p.styleName); // the writer's own style, named in Word
    if (!sid.isEmpty())
        ppr += QStringLiteral("<w:pStyle w:val=\"%1\"/>").arg(sid);
    if (p.keepWithNext)
        ppr += QLatin1String("<w:keepNext/>");
    if (p.pageBreakBefore && !inFootnote)
        ppr += QLatin1String("<w:pageBreakBefore/>");
    if ((p.style == Paragraph::ListBullet || p.style == Paragraph::ListNumber) && !inFootnote) {
        int numId = m_listNum.value(p.listId, 0);
        if (!numId) {
            numId = m_nums.size() + 1;
            m_nums.append({numId, p.style == Paragraph::ListNumber});
            m_listNum.insert(p.listId, numId);
        }
        ppr += QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>").arg(qBound(0, p.listLevel, 8)).arg(numId);
    }
    // formatting set by hand (tabs, spacing, indents: in this order)
    const auto twips = [](double mm) { return qRound(mm * 1440.0 / 25.4); };
    if ((p.direct & Paragraph::DTabs) && !inFootnote) {
        QString tabs;
        for (const TabStop &t : p.tabs)
            tabs += QStringLiteral("<w:tab w:val=\"%1\" w:pos=\"%2\"/>")
                        .arg(t.type == TabStop::Right ? QStringLiteral("right")
                             : t.type == TabStop::Center ? QStringLiteral("center")
                             : t.type == TabStop::Decimal ? QStringLiteral("decimal")
                                                          : QStringLiteral("left"))
                        .arg(twips(t.positionMm));
        if (!tabs.isEmpty())
            ppr += QLatin1String("<w:tabs>") + tabs + QLatin1String("</w:tabs>");
    }
    if (p.direct & (Paragraph::DBefore | Paragraph::DAfter | Paragraph::DLine)) {
        QString sp;
        if (p.direct & Paragraph::DBefore)
            sp += QStringLiteral(" w:before=\"%1\"").arg(qRound(p.spaceBeforePt * 20));
        if (p.direct & Paragraph::DAfter)
            sp += QStringLiteral(" w:after=\"%1\"").arg(qRound(p.spaceAfterPt * 20));
        if (p.direct & Paragraph::DLine) {
            if (p.lineSpacing > 0)
                sp += QStringLiteral(" w:line=\"%1\" w:lineRule=\"auto\"").arg(qRound(240 * p.lineSpacing));
            else if (p.lineHeightPt > 0)
                sp += QStringLiteral(" w:line=\"%1\" w:lineRule=\"%2\"")
                          .arg(qRound(p.lineHeightPt * 20))
                          .arg(p.lineAtLeast ? QStringLiteral("atLeast") : QStringLiteral("exact"));
        }
        ppr += QLatin1String("<w:spacing") + sp + QLatin1String("/>");
    }
    {
        QString ind;
        if (p.direct & Paragraph::DLeft)
            ind += QStringLiteral(" w:left=\"%1\"").arg(twips(p.leftIndentMm));
        if (p.direct & Paragraph::DRight)
            ind += QStringLiteral(" w:right=\"%1\"").arg(twips(p.rightIndentMm));
        if (p.direct & Paragraph::DFirst) {
            if (p.firstLineMm >= 0)
                ind += QStringLiteral(" w:firstLine=\"%1\"").arg(twips(p.firstLineMm));
            else
                ind += QStringLiteral(" w:hanging=\"%1\"").arg(twips(-p.firstLineMm));
        } else if ((p.noIndent && p.style == Paragraph::Normal)
                   || ((p.direct & Paragraph::DAlign) && (p.align & (Qt::AlignHCenter | Qt::AlignRight)))) {
            ind += QLatin1String(" w:firstLine=\"0\"");
        }
        if (!ind.isEmpty())
            ppr += QLatin1String("<w:ind") + ind + QLatin1String("/>");
    }
    if (p.align & Qt::AlignHCenter)
        ppr += QLatin1String("<w:jc w:val=\"center\"/>");
    else if (p.align & Qt::AlignRight)
        ppr += QLatin1String("<w:jc w:val=\"right\"/>");
    else if (p.align & Qt::AlignJustify)
        ppr += QLatin1String("<w:jc w:val=\"both\"/>");
    else if ((p.align & Qt::AlignLeft) && (p.style != Paragraph::Normal || (p.direct & Paragraph::DAlign)))
        ppr += QLatin1String("<w:jc w:val=\"left\"/>");
    QString out = QLatin1String("<w:p>");
    if (!ppr.isEmpty())
        out += QLatin1String("<w:pPr>") + ppr + QLatin1String("</w:pPr>");
    out += runs(p.runs, inFootnote);
    out += QLatin1String("</w:p>");
    return out;
}

QString Builder::textWithPageField(const QString &t, const QString &rpr)
{
    QString out;
    const QStringList parts = t.split(QStringLiteral("<$p>"));
    for (int i = 0; i < parts.size(); ++i) {
        if (!parts[i].isEmpty())
            out += QLatin1String("<w:r>") + rpr + QLatin1String("<w:t xml:space=\"preserve\">") + xmlEscape(parts[i])
                   + QLatin1String("</w:t></w:r>");
        if (i + 1 < parts.size())
            out += QLatin1String("<w:r>") + rpr + QLatin1String("<w:fldChar w:fldCharType=\"begin\"/></w:r>")
                   + QLatin1String("<w:r>") + rpr + QLatin1String("<w:instrText xml:space=\"preserve\"> PAGE </w:instrText></w:r>")
                   + QLatin1String("<w:r>") + rpr + QLatin1String("<w:fldChar w:fldCharType=\"separate\"/></w:r>")
                   + QLatin1String("<w:r>") + rpr + QLatin1String("<w:t>1</w:t></w:r>")
                   + QLatin1String("<w:r>") + rpr + QLatin1String("<w:fldChar w:fldCharType=\"end\"/></w:r>");
    }
    return out;
}

QString Builder::headerFooterParagraph(const QString &left, const QString &center, const QString &right)
{
    // one paragraph with a centre and a right tab stop
    const int textWidth = twipsFromMm(m.page.widthMm - m.page.marginLeftMm - m.page.marginRightMm);
    QString out = QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"Header\"/><w:tabs>"
                                 "<w:tab w:val=\"center\" w:pos=\"%1\"/><w:tab w:val=\"right\" w:pos=\"%2\"/>"
                                 "</w:tabs></w:pPr>")
                      .arg(textWidth / 2)
                      .arg(textWidth);
    out += textWithPageField(left, QString());
    if (!center.isEmpty() || !right.isEmpty())
        out += QLatin1String("<w:r><w:tab/></w:r>") + textWithPageField(center, QString());
    if (!right.isEmpty())
        out += QLatin1String("<w:r><w:tab/></w:r>") + textWithPageField(right, QString());
    out += QLatin1String("</w:p>");
    return out;
}

QByteArray Builder::document()
{
    QString body;
    body.reserve(m.paragraphs.size() * 200);
    bool tocDone = !m.options.toc;
    for (const Paragraph &p : m.paragraphs) {
        if (!tocDone && p.outlineLevel == 1) {
            body += tocField();
            tocDone = true;
        }
        body += paragraph(p, false);
    }
    if (!tocDone)
        body += tocField();
    if (m_openComment >= 0) {
        body += QStringLiteral("<w:p><w:commentRangeEnd w:id=\"%1\"/><w:r><w:rPr><w:rStyle w:val=\"CommentReference\"/></w:rPr>"
                               "<w:commentReference w:id=\"%1\"/></w:r></w:p>")
                    .arg(m_openComment);
        m_openComment = -1;
    }
    QString sect = QLatin1String("<w:sectPr>");
    if (hasHeader()) {
        sect += QLatin1String("<w:headerReference w:type=\"default\" r:id=\"rIdHeader\"/>");
        if (facing())
            sect += QLatin1String("<w:headerReference w:type=\"even\" r:id=\"rIdHeaderEven\"/>");
    }
    if (hasFooter()) {
        sect += QLatin1String("<w:footerReference w:type=\"default\" r:id=\"rIdFooter\"/>");
        if (facing())
            sect += QLatin1String("<w:footerReference w:type=\"even\" r:id=\"rIdFooterEven\"/>");
    }
    sect += QStringLiteral("<w:pgSz w:w=\"%1\" w:h=\"%2\"/>").arg(twipsFromMm(m.page.widthMm)).arg(twipsFromMm(m.page.heightMm));
    sect += QStringLiteral("<w:pgMar w:top=\"%1\" w:right=\"%2\" w:bottom=\"%3\" w:left=\"%4\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/>")
                .arg(twipsFromMm(m.page.marginTopMm))
                .arg(twipsFromMm(m.page.marginRightMm))
                .arg(twipsFromMm(m.page.marginBottomMm))
                .arg(twipsFromMm(m.page.marginLeftMm));
    if (m.page.pageNumberStyle != 0 || m.page.firstPageNumber != 1)
        sect += QStringLiteral("<w:pgNumType%1 w:start=\"%2\"/>")
                    .arg(m.page.pageNumberStyle == 1   ? QStringLiteral(" w:fmt=\"lowerRoman\"")
                         : m.page.pageNumberStyle == 2 ? QStringLiteral(" w:fmt=\"upperRoman\"")
                                                       : QString())
                    .arg(m.page.firstPageNumber);
    if ((hasHeader() || hasFooter()) && !m.page.headerOnFirstPage)
        sect += QLatin1String("<w:titlePg/>");
    sect += QLatin1String("</w:sectPr>");

    const QString xml = QStringLiteral(
                            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                            "<w:document xmlns:w=\"%1\" xmlns:r=\"%2\" "
                            "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
                            "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                            "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                            "<w:body>")
                            .arg(QLatin1String(kW), QLatin1String(kR))
                        + body + sect + QLatin1String("</w:body></w:document>");
    return xml.toUtf8();
}

QByteArray Builder::styles()
{
    const QString font = xmlEscape(m.fontFamily);
    const QString hfont = xmlEscape(m.headingFontFamily.isEmpty() ? m.fontFamily : m.headingFontFamily);
    const int sz = qRound(m.fontSize * 2);
    const int line = qRound(240 * m.lineSpacing);
    const int after = twipsFromPt(m.paragraphSpacingPt);
    const int indent = twipsFromMm(m.firstLineIndentMm);
    const double hs = m.headingScale;
    auto hsz = [&](double f) { return qRound(m.fontSize * f * 2); };
    const QString bold = m.headingBold ? QStringLiteral("<w:b/><w:bCs/>") : QString();
    const int headingBefore = hs > 1.01 ? 360 : 0;
    const int headingAfter = hs > 1.01 ? 180 : 0;

    QString x;
    x += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:styles xmlns:w=\"%1\">").arg(QLatin1String(kW));
    x += QStringLiteral("<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:eastAsia=\"%1\" w:cs=\"%1\"/>"
                        "<w:sz w:val=\"%2\"/><w:szCs w:val=\"%2\"/><w:lang w:val=\"%3\"/></w:rPr></w:rPrDefault>"
                        "<w:pPrDefault><w:pPr><w:spacing w:before=\"0\" w:after=\"%4\" w:line=\"%5\" w:lineRule=\"auto\"/></w:pPr></w:pPrDefault>"
                        "</w:docDefaults>")
             .arg(font)
             .arg(sz)
             .arg(xmlEscape(m.language))
             .arg(after)
             .arg(line);
    x += QStringLiteral("<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/>"
                        "<w:pPr><w:ind w:firstLine=\"%1\"/>%2</w:pPr></w:style>")
             .arg(indent)
             .arg(m.justify ? QStringLiteral("<w:jc w:val=\"both\"/>") : QString());
    auto heading = [&](const QString &id, const QString &name, int size, int lvl, bool italic) {
        return QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"%1\"><w:name w:val=\"%2\"/><w:basedOn w:val=\"Normal\"/>"
                              "<w:next w:val=\"Normal\"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:before=\"%3\" w:after=\"%4\"/>"
                              "<w:ind w:firstLine=\"0\"/><w:jc w:val=\"center\"/><w:outlineLvl w:val=\"%5\"/></w:pPr>"
                              "<w:rPr><w:rFonts w:ascii=\"%6\" w:hAnsi=\"%6\" w:cs=\"%6\"/>%7%8<w:sz w:val=\"%9\"/><w:szCs w:val=\"%9\"/></w:rPr></w:style>")
            .arg(id, name)
            .arg(headingBefore)
            .arg(headingAfter)
            .arg(lvl)
            .arg(hfont, bold, italic ? QStringLiteral("<w:i/>") : QString())
            .arg(size);
    };
    x += heading(QStringLiteral("Heading1"), QStringLiteral("heading 1"), hsz(hs), 0, false);
    x += heading(QStringLiteral("Heading2"), QStringLiteral("heading 2"), hsz(1 + (hs - 1) * 0.6), 1, false);
    x += heading(QStringLiteral("Heading3"), QStringLiteral("heading 3"), hsz(1 + (hs - 1) * 0.3), 2, hs <= 1.01);
    x += heading(QStringLiteral("Title"), QStringLiteral("Title"), hsz(qMax(1.2, hs * 1.3)), 0, false);
    x += QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"Subtitle\"><w:name w:val=\"Subtitle\"/><w:basedOn w:val=\"Normal\"/>"
                        "<w:next w:val=\"Normal\"/><w:pPr><w:ind w:firstLine=\"0\"/><w:jc w:val=\"center\"/></w:pPr>"
                        "<w:rPr><w:i/><w:sz w:val=\"%1\"/></w:rPr></w:style>")
             .arg(hsz(qMax(1.1, hs * 0.9)));
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/>"
                       "<w:pPr><w:ind w:left=\"720\" w:right=\"720\" w:firstLine=\"0\"/></w:pPr></w:style>");
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"Centered\"><w:name w:val=\"Centered\"/><w:basedOn w:val=\"Normal\"/>"
                       "<w:pPr><w:ind w:firstLine=\"0\"/><w:jc w:val=\"center\"/></w:pPr></w:style>");
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"Separator\"><w:name w:val=\"Scene Separator\"/><w:basedOn w:val=\"Normal\"/>"
                       "<w:next w:val=\"Normal\"/><w:pPr><w:keepNext/><w:ind w:firstLine=\"0\"/><w:jc w:val=\"center\"/></w:pPr></w:style>");
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"Caption\"><w:name w:val=\"caption\"/><w:basedOn w:val=\"Normal\"/>"
                       "<w:pPr><w:ind w:firstLine=\"0\"/><w:jc w:val=\"center\"/></w:pPr><w:rPr><w:i/></w:rPr></w:style>");
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"Synopsis\"><w:name w:val=\"Synopsis\"/><w:basedOn w:val=\"Normal\"/>"
                       "<w:pPr><w:ind w:firstLine=\"0\"/></w:pPr><w:rPr><w:i/></w:rPr></w:style>");
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List Paragraph\"/><w:basedOn w:val=\"Normal\"/>"
                       "<w:pPr><w:ind w:firstLine=\"0\"/></w:pPr></w:style>");
    x += QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"FootnoteText\"><w:name w:val=\"footnote text\"/><w:basedOn w:val=\"Normal\"/>"
                        "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/><w:ind w:firstLine=\"0\"/></w:pPr>"
                        "<w:rPr><w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/></w:rPr></w:style>")
             .arg(qMax(16, sz - 4));
    x += QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"CommentText\"><w:name w:val=\"annotation text\"/><w:basedOn w:val=\"Normal\"/>"
                        "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/><w:ind w:firstLine=\"0\"/></w:pPr>"
                        "<w:rPr><w:sz w:val=\"%1\"/></w:rPr></w:style>")
             .arg(qMax(16, sz - 4));
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"Header\"><w:name w:val=\"header\"/><w:basedOn w:val=\"Normal\"/>"
                       "<w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/><w:ind w:firstLine=\"0\"/></w:pPr></w:style>");
    x += QLatin1String("<w:style w:type=\"character\" w:styleId=\"FootnoteReference\"><w:name w:val=\"footnote reference\"/>"
                       "<w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr></w:style>");
    x += QLatin1String("<w:style w:type=\"character\" w:styleId=\"CommentReference\"><w:name w:val=\"annotation reference\"/>"
                       "<w:rPr><w:sz w:val=\"16\"/></w:rPr></w:style>");
    x += QLatin1String("<w:style w:type=\"character\" w:styleId=\"Hyperlink\"><w:name w:val=\"Hyperlink\"/>"
                       "<w:rPr><w:color w:val=\"0563C1\"/><w:u w:val=\"single\"/></w:rPr></w:style>");
    x += QLatin1String("<w:style w:type=\"paragraph\" w:styleId=\"TOCHeading\"><w:name w:val=\"TOC Heading\"/><w:basedOn w:val=\"Heading1\"/>"
                       "<w:next w:val=\"Normal\"/><w:qFormat/><w:pPr><w:outlineLvl w:val=\"9\"/></w:pPr></w:style>");
    // the writer's own styles, with their look (Word shows them by name)
    for (const NamedStyle &st : m.styles) {
        QString ppr, rpr;
        if (!st.character) {
            QString sp;
            if (st.spaceBeforePt >= 0)
                sp += QStringLiteral(" w:before=\"%1\"").arg(twipsFromPt(st.spaceBeforePt));
            if (st.spaceAfterPt >= 0)
                sp += QStringLiteral(" w:after=\"%1\"").arg(twipsFromPt(st.spaceAfterPt));
            if (st.lineSpacing > 0)
                sp += QStringLiteral(" w:line=\"%1\" w:lineRule=\"auto\"").arg(qRound(240 * st.lineSpacing));
            if (!sp.isEmpty())
                ppr += QLatin1String("<w:spacing") + sp + QLatin1String("/>");
            QString ind;
            if (st.leftIndentMm >= 0)
                ind += QStringLiteral(" w:left=\"%1\"").arg(twipsFromMm(st.leftIndentMm));
            if (st.rightIndentMm >= 0)
                ind += QStringLiteral(" w:right=\"%1\"").arg(twipsFromMm(st.rightIndentMm));
            if (st.firstLineMm > -999)
                ind += st.firstLineMm >= 0 ? QStringLiteral(" w:firstLine=\"%1\"").arg(twipsFromMm(st.firstLineMm))
                                           : QStringLiteral(" w:hanging=\"%1\"").arg(twipsFromMm(-st.firstLineMm));
            if (!ind.isEmpty())
                ppr += QLatin1String("<w:ind") + ind + QLatin1String("/>");
            if (st.align >= 0)
                ppr += QStringLiteral("<w:jc w:val=\"%1\"/>")
                           .arg(QStringList{QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right"), QStringLiteral("both")}
                                    .value(st.align));
        }
        if (!st.fontFamily.isEmpty())
            rpr += QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:cs=\"%1\"/>").arg(xmlEscape(st.fontFamily));
        if (st.bold >= 0)
            rpr += st.bold ? QStringLiteral("<w:b/><w:bCs/>") : QStringLiteral("<w:b w:val=\"0\"/>");
        if (st.italic >= 0)
            rpr += st.italic ? QStringLiteral("<w:i/><w:iCs/>") : QStringLiteral("<w:i w:val=\"0\"/>");
        if (st.capitals == 2)
            rpr += QLatin1String("<w:caps/>");
        else if (st.capitals == 1)
            rpr += QLatin1String("<w:smallCaps/>");
        if (st.color.isValid())
            rpr += QStringLiteral("<w:color w:val=\"%1\"/>").arg(st.color.name().mid(1).toUpper());
        if (st.fontSize > 0)
            rpr += QStringLiteral("<w:sz w:val=\"%1\"/><w:szCs w:val=\"%1\"/>").arg(qRound(st.fontSize * 2));
        if (st.underline > 0)
            rpr += QLatin1String("<w:u w:val=\"single\"/>");
        x += QStringLiteral("<w:style w:type=\"%1\" w:customStyle=\"1\" w:styleId=\"%2\"><w:name w:val=\"%3\"/>%4<w:qFormat/>%5%6</w:style>")
                 .arg(st.character ? QStringLiteral("character") : QStringLiteral("paragraph"), ownStyleId(st.name), xmlEscape(st.name),
                      st.character ? QString() : QStringLiteral("<w:basedOn w:val=\"Normal\"/>"),
                      ppr.isEmpty() ? QString() : QLatin1String("<w:pPr>") + ppr + QLatin1String("</w:pPr>"),
                      rpr.isEmpty() ? QString() : QLatin1String("<w:rPr>") + rpr + QLatin1String("</w:rPr>"));
    }
    x += QLatin1String("</w:styles>");
    return x.toUtf8();
}

QByteArray Builder::settings()
{
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:settings xmlns:w=\"%1\">").arg(QLatin1String(kW));
    if (m.page.mirrorMargins)
        x += QLatin1String("<w:mirrorMargins/>");
    x += QLatin1String("<w:defaultTabStop w:val=\"720\"/>");
    if (facing() && (hasHeader() || hasFooter()))
        x += QLatin1String("<w:evenAndOddHeaders/>");
    x += QLatin1String("<w:characterSpacingControl w:val=\"doNotCompress\"/>");
    if (m.options.toc)
        x += QLatin1String("<w:updateFields w:val=\"true\"/>");
    x += QLatin1String("<w:footnotePr><w:footnote w:id=\"-1\"/><w:footnote w:id=\"0\"/></w:footnotePr>"
                       "<w:compat><w:compatSetting w:name=\"compatibilityMode\" w:uri=\"http://schemas.microsoft.com/office/word\" w:val=\"15\"/></w:compat>"
                       "</w:settings>");
    return x.toUtf8();
}

QByteArray Builder::footnotes()
{
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                               "<w:footnotes xmlns:w=\"%1\" xmlns:r=\"%2\">"
                               "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/><w:ind w:firstLine=\"0\"/></w:pPr><w:r><w:separator/></w:r></w:p></w:footnote>"
                               "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:pPr><w:spacing w:after=\"0\" w:line=\"240\" w:lineRule=\"auto\"/><w:ind w:firstLine=\"0\"/></w:pPr><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>")
                    .arg(QLatin1String(kW), QLatin1String(kR));
    for (int i = 0; i < m.footnotes.size(); ++i) {
        x += QStringLiteral("<w:footnote w:id=\"%1\">").arg(i + 1);
        const QList<Paragraph> &ps = m.footnotes[i].paragraphs;
        for (int j = 0; j < ps.size(); ++j) {
            QString p = QLatin1String("<w:p><w:pPr><w:pStyle w:val=\"FootnoteText\"/></w:pPr>");
            if (j == 0)
                p += QLatin1String("<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr><w:footnoteRef/></w:r>"
                                   "<w:r><w:t xml:space=\"preserve\"> </w:t></w:r>");
            QList<Run> rs = ps[j].runs;
            // strip a leading space, we already added one
            if (j == 0 && !rs.isEmpty() && rs.first().kind == Run::Text)
                rs.first().text = rs.first().text.trimmed().isEmpty() ? rs.first().text : QString(rs.first().text).remove(QRegularExpression(QStringLiteral("^\\s+")));
            p += runs(rs, true);
            p += QLatin1String("</w:p>");
            x += p;
        }
        if (ps.isEmpty())
            x += QLatin1String("<w:p><w:pPr><w:pStyle w:val=\"FootnoteText\"/></w:pPr><w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr><w:footnoteRef/></w:r></w:p>");
        x += QLatin1String("</w:footnote>");
    }
    x += QLatin1String("</w:footnotes>");
    return x.toUtf8();
}

QByteArray Builder::comments()
{
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:comments xmlns:w=\"%1\">").arg(QLatin1String(kW));
    const QString date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (int i = 0; i < m.comments.size(); ++i) {
        const Comment &c = m.comments[i];
        QString initials;
        for (const QString &part : c.author.split(QLatin1Char(' '), Qt::SkipEmptyParts))
            initials += part.left(1);
        x += QStringLiteral("<w:comment w:id=\"%1\" w:author=\"%2\" w:date=\"%3\" w:initials=\"%4\">")
                 .arg(i)
                 .arg(xmlEscape(c.author.isEmpty() ? QStringLiteral("Author") : c.author), date, xmlEscape(initials));
        const QStringList paras = c.text.split(QLatin1Char('\n'));
        for (int j = 0; j < paras.size(); ++j) {
            x += QLatin1String("<w:p><w:pPr><w:pStyle w:val=\"CommentText\"/></w:pPr>");
            if (j == 0)
                x += QLatin1String("<w:r><w:rPr><w:rStyle w:val=\"CommentReference\"/></w:rPr><w:annotationRef/></w:r>");
            x += QLatin1String("<w:r><w:t xml:space=\"preserve\">") + xmlEscape(paras[j]) + QLatin1String("</w:t></w:r></w:p>");
        }
        x += QLatin1String("</w:comment>");
    }
    x += QLatin1String("</w:comments>");
    return x.toUtf8();
}

QByteArray Builder::numbering()
{
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:numbering xmlns:w=\"%1\">").arg(QLatin1String(kW));
    static const char *bullets[] = {"\xE2\x80\xA2", "\xE2\x97\xA6", "\xE2\x96\xAA"};
    x += QLatin1String("<w:abstractNum w:abstractNumId=\"0\"><w:multiLevelType w:val=\"hybridMultilevel\"/>");
    for (int l = 0; l < 9; ++l)
        x += QStringLiteral("<w:lvl w:ilvl=\"%1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"%2\"/>"
                            "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"%3\" w:hanging=\"360\"/></w:pPr></w:lvl>")
                 .arg(l)
                 .arg(QString::fromUtf8(bullets[l % 3]))
                 .arg(720 * (l + 1));
    x += QLatin1String("</w:abstractNum>");
    x += QLatin1String("<w:abstractNum w:abstractNumId=\"1\"><w:multiLevelType w:val=\"hybridMultilevel\"/>");
    static const char *fmts[] = {"decimal", "lowerLetter", "lowerRoman"};
    for (int l = 0; l < 9; ++l)
        x += QStringLiteral("<w:lvl w:ilvl=\"%1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"%2\"/><w:lvlText w:val=\"%%3.\"/>"
                            "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"%4\" w:hanging=\"360\"/></w:pPr></w:lvl>")
                 .arg(l)
                 .arg(QLatin1String(fmts[l % 3]))
                 .arg(l + 1)
                 .arg(720 * (l + 1));
    x += QLatin1String("</w:abstractNum>");
    for (const auto &n : m_nums) {
        x += QStringLiteral("<w:num w:numId=\"%1\"><w:abstractNumId w:val=\"%2\"/>").arg(n.first).arg(n.second ? 1 : 0);
        if (n.second)
            x += QLatin1String("<w:lvlOverride w:ilvl=\"0\"><w:startOverride w:val=\"1\"/></w:lvlOverride>");
        x += QLatin1String("</w:num>");
    }
    x += QLatin1String("</w:numbering>");
    return x.toUtf8();
}

QByteArray Builder::header() { return header(false); }
QByteArray Builder::footer() { return footer(false); }

QByteArray Builder::header(bool even)
{
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:hdr xmlns:w=\"%1\" xmlns:r=\"%2\">")
                    .arg(QLatin1String(kW), QLatin1String(kR));
    // facing pages: left texts on even pages, right texts on odd pages
    const QString l = facing() && !even ? QString() : m.page.headerLeft;
    const QString r = facing() && even ? QString() : m.page.headerRight;
    x += headerFooterParagraph(l, m.page.headerCenter, r);
    x += QLatin1String("</w:hdr>");
    return x.toUtf8();
}

QByteArray Builder::footer(bool even)
{
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:ftr xmlns:w=\"%1\" xmlns:r=\"%2\">")
                    .arg(QLatin1String(kW), QLatin1String(kR));
    const QString l = facing() && !even ? QString() : m.page.footerLeft;
    const QString r = facing() && even ? QString() : m.page.footerRight;
    if (l.isEmpty() && r.isEmpty())
        x += QLatin1String("<w:p><w:pPr><w:pStyle w:val=\"Header\"/><w:jc w:val=\"center\"/></w:pPr>")
             + textWithPageField(m.page.footerCenter, QString()) + QLatin1String("</w:p>");
    else
        x += headerFooterParagraph(l, m.page.footerCenter, r);
    x += QLatin1String("</w:ftr>");
    return x.toUtf8();
}

QByteArray Builder::documentRels()
{
    const QString base = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    auto rel = [&](const QString &id, const QString &type, const QString &target) {
        x += QStringLiteral("<Relationship Id=\"%1\" Type=\"%2%3\" Target=\"%4\"/>").arg(id, base, type, xmlEscape(target));
    };
    rel(QStringLiteral("rIdStyles"), QStringLiteral("styles"), QStringLiteral("styles.xml"));
    rel(QStringLiteral("rIdSettings"), QStringLiteral("settings"), QStringLiteral("settings.xml"));
    rel(QStringLiteral("rIdFootnotes"), QStringLiteral("footnotes"), QStringLiteral("footnotes.xml"));
    rel(QStringLiteral("rIdComments"), QStringLiteral("comments"), QStringLiteral("comments.xml"));
    rel(QStringLiteral("rIdNumbering"), QStringLiteral("numbering"), QStringLiteral("numbering.xml"));
    if (hasHeader())
        rel(QStringLiteral("rIdHeader"), QStringLiteral("header"), QStringLiteral("header1.xml"));
    if (hasFooter())
        rel(QStringLiteral("rIdFooter"), QStringLiteral("footer"), QStringLiteral("footer1.xml"));
    if (hasHeader() && facing())
        rel(QStringLiteral("rIdHeaderEven"), QStringLiteral("header"), QStringLiteral("header2.xml"));
    if (hasFooter() && facing())
        rel(QStringLiteral("rIdFooterEven"), QStringLiteral("footer"), QStringLiteral("footer2.xml"));
    for (const auto &r : m_imageRels)
        rel(r.first, QStringLiteral("image"), r.second);
    for (const auto &r : m_linkRels)
        x += QStringLiteral("<Relationship Id=\"%1\" Type=\"%2hyperlink\" Target=\"%3\" TargetMode=\"External\"/>")
                 .arg(r.first, base, xmlEscape(r.second));
    x += QLatin1String("</Relationships>");
    return x.toUtf8();
}

QByteArray Builder::core()
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
               "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
               "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
               "<dc:title>%1</dc:title>%4<dc:creator>%2</dc:creator>%5%6"
               "<dcterms:created xsi:type=\"dcterms:W3CDTF\">%3</dcterms:created>"
               "<dcterms:modified xsi:type=\"dcterms:W3CDTF\">%3</dcterms:modified>"
               "</cp:coreProperties>")
        .arg(xmlEscape(m.title), xmlEscape(m.author), now,
             m.metadata.value(QStringLiteral("subject")).isEmpty()
                 ? QString()
                 : QStringLiteral("<dc:subject>%1</dc:subject>").arg(xmlEscape(m.metadata.value(QStringLiteral("subject")))),
             m.metadata.value(QStringLiteral("keywords")).isEmpty()
                 ? QString()
                 : QStringLiteral("<cp:keywords>%1</cp:keywords>").arg(xmlEscape(m.metadata.value(QStringLiteral("keywords")))),
             m.options.description.isEmpty()
                 ? QString()
                 : QStringLiteral("<dc:description>%1</dc:description>").arg(xmlEscape(m.options.description)))
        .toUtf8();
}

QByteArray Builder::contentTypes()
{
    QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>"
        "<Override PartName=\"/word/footnotes.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>"
        "<Override PartName=\"/word/comments.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml\"/>"
        "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "<Override PartName=\"/docProps/app.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>");
    if (hasHeader())
        x += QLatin1String("<Override PartName=\"/word/header1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>");
    if (hasFooter())
        x += QLatin1String("<Override PartName=\"/word/footer1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>");
    if (hasHeader() && facing())
        x += QLatin1String("<Override PartName=\"/word/header2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>");
    if (hasFooter() && facing())
        x += QLatin1String("<Override PartName=\"/word/footer2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>");
    x += QLatin1String("</Types>");
    return x.toUtf8();
}

} // namespace

bool DocxExporter::exportManuscript(const Manuscript &m, const QString &path, QString *error)
{
    Builder b(m);
    // document first: it registers images, links and lists
    const QByteArray doc = b.document();
    const QByteArray fn = b.footnotes();
    const QString tmpPath = path + QStringLiteral(".part");
    {
        ZipWriter zw(tmpPath);
        if (!zw.isOpen()) {
            if (error)
                *error = tr("Cannot write %1: %2").arg(path, zw.errorString());
            return false;
        }
        zw.addFile(QStringLiteral("[Content_Types].xml"), b.contentTypes());
        zw.addFile(QStringLiteral("_rels/.rels"),
                   QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                              "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                              "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
                              "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
                              "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" Target=\"docProps/app.xml\"/>"
                              "</Relationships>"));
        zw.addFile(QStringLiteral("docProps/core.xml"), b.core());
        zw.addFile(QStringLiteral("docProps/app.xml"),
                   QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                              "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
                              "<Application>Quillbinder</Application></Properties>"));
        zw.addFile(QStringLiteral("word/document.xml"), doc);
        zw.addFile(QStringLiteral("word/styles.xml"), b.styles());
        zw.addFile(QStringLiteral("word/settings.xml"), b.settings());
        zw.addFile(QStringLiteral("word/footnotes.xml"), fn);
        zw.addFile(QStringLiteral("word/comments.xml"), b.comments());
        zw.addFile(QStringLiteral("word/numbering.xml"), b.numbering());
        if (b.hasHeader())
            zw.addFile(QStringLiteral("word/header1.xml"), b.header());
        if (b.hasFooter())
            zw.addFile(QStringLiteral("word/footer1.xml"), b.footer());
        if (b.hasHeader() && b.facing())
            zw.addFile(QStringLiteral("word/header2.xml"), b.header(true));
        if (b.hasFooter() && b.facing())
            zw.addFile(QStringLiteral("word/footer2.xml"), b.footer(true));
        for (const auto &md : b.media())
            zw.addFile(QStringLiteral("word/media/") + md.first, md.second, false);
        zw.addFile(QStringLiteral("word/_rels/document.xml.rels"), b.documentRels());
        if (!zw.close()) {
            if (error)
                *error = tr("Cannot write %1").arg(path);
            QFile::remove(tmpPath);
            return false;
        }
    }
    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        if (error)
            *error = tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- markdown

static QString mdRuns(const QList<Run> &runs, const Manuscript &m, QStringList &notes)
{
    QString out;
    for (const Run &r : runs) {
        switch (r.kind) {
        case Run::Text: {
            QString t = md::escapeInline(r.text, true);
            if (r.bold && r.italic)
                t = QLatin1String("***") + t + QLatin1String("***");
            else if (r.bold)
                t = QLatin1String("**") + t + QLatin1String("**");
            else if (r.italic)
                t = QLatin1Char('*') + t + QLatin1Char('*');
            if (r.strike)
                t = QLatin1String("~~") + t + QLatin1String("~~");
            if (!r.link.isEmpty())
                t = QLatin1Char('[') + t + QLatin1String("](") + r.link + QLatin1Char(')');
            out += t;
            break;
        }
        case Run::Tab: out += QLatin1Char('\t'); break;
        case Run::LineBreak: out += QLatin1String("  \n"); break;
        case Run::FootnoteRef:
            if (r.footnote >= 0 && r.footnote < m.footnotes.size()) {
                const int n = notes.size() + 1;
                out += QStringLiteral("[^%1]").arg(n);
                QString text;
                for (const Paragraph &p : m.footnotes[r.footnote].paragraphs)
                    text += p.plainText() + QLatin1Char(' ');
                notes << QStringLiteral("[^%1]: %2").arg(n).arg(text.trimmed());
            }
            break;
        case Run::Image: break;
        }
    }
    return out;
}

bool writeMarkdown(const Manuscript &m, const QString &path, QString *error)
{
    QString out;
    QStringList notes;
    if (!m.title.isEmpty())
        out += QStringLiteral("---\ntitle: \"%1\"\nauthor: \"%2\"\n---\n\n").arg(m.title, m.author);
    int prevListId = -1;
    for (const Paragraph &p : m.paragraphs) {
        if (p.pageBreakBefore && !out.isEmpty())
            out += QLatin1String("\n");
        const QString body = mdRuns(p.runs, m, notes);
        QString line;
        switch (p.style) {
        case Paragraph::Title: line = QLatin1String("# ") + body; break;
        case Paragraph::Heading1: line = QLatin1String("# ") + body; break;
        case Paragraph::Heading2: line = QLatin1String("## ") + body; break;
        case Paragraph::Heading3: line = QLatin1String("### ") + body; break;
        case Paragraph::Subtitle: line = QLatin1Char('*') + body + QLatin1Char('*'); break;
        case Paragraph::BlockQuote: line = QLatin1String("> ") + body; break;
        case Paragraph::Separator: line = QStringLiteral("<center>") + body + QStringLiteral("</center>"); break;
        case Paragraph::ListBullet: line = QString(p.listLevel * 2, QLatin1Char(' ')) + QLatin1String("- ") + body; break;
        case Paragraph::ListNumber: line = QString(p.listLevel * 3, QLatin1Char(' ')) + QLatin1String("1. ") + body; break;
        case Paragraph::Synopsis: line = QLatin1Char('_') + body + QLatin1Char('_'); break;
        default: line = body; break;
        }
        if (line.trimmed().isEmpty())
            continue;
        const bool isList = p.style == Paragraph::ListBullet || p.style == Paragraph::ListNumber;
        // list items are separated by single line breaks, everything else
        // (and the end of a list) by a blank line
        if (prevListId != -1 && (!isList || p.listId != prevListId))
            out += QLatin1Char('\n');
        out += line + (isList ? QStringLiteral("\n") : QStringLiteral("\n\n"));
        prevListId = isList ? p.listId : -1;
    }
    if (!notes.isEmpty())
        out += QLatin1Char('\n') + notes.join(QLatin1Char('\n')) + QLatin1Char('\n');
    QSaveFile f(path);
    const QByteArray data = out.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace compile
} // namespace qb
