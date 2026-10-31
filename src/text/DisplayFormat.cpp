#include "text/DisplayFormat.h"

#include "text/DirectFormat.h"
#include "text/TextProps.h"

#include <QFontDatabase>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextOption>

namespace qb {

QColor defaultCommentColor() { return QColor(255, 236, 150); }
QColor defaultAnnotationColor() { return QColor(200, 30, 30); }
QColor footnoteBubbleColor() { return QColor(210, 210, 210); }

namespace display {

QString resolvedFontFamily(const Formatting &f)
{
    if (!f.fontFamily.isEmpty())
        return f.fontFamily;
    static QString cached;
    if (cached.isEmpty()) {
        const QStringList families = QFontDatabase::families();
        const QStringList preferred = {QStringLiteral("Noto Serif"), QStringLiteral("Liberation Serif"),
                                       QStringLiteral("DejaVu Serif"), QStringLiteral("Times New Roman"),
                                       QStringLiteral("FreeSerif")};
        for (const QString &p : preferred) {
            if (families.contains(p, Qt::CaseInsensitive)) {
                cached = p;
                break;
            }
        }
        if (cached.isEmpty())
            cached = QStringLiteral("Serif");
    }
    return cached;
}

QFont baseFont(const Formatting &f, double zoom)
{
    QFont font(resolvedFontFamily(f));
    font.setPointSizeF(qMax(4.0, f.fontSize * zoom));
    font.setStyleHint(QFont::Serif);
    return font;
}

QTextBlockFormat normalBlockFormat(const Formatting &f)
{
    QTextBlockFormat bf;
    bf.setTextIndent(f.firstLineIndent);
    bf.setLineHeight(f.lineSpacing * 100.0, QTextBlockFormat::ProportionalHeight);
    bf.setBottomMargin(f.paragraphSpacing);
    bf.setTopMargin(0);
    bf.setLeftMargin(0);
    bf.setRightMargin(0);
    bf.setAlignment(bodyAlignment(f.align));
    bf.setProperty(props::Style, QString());
    return bf;
}

QTextBlockFormat blockFormatFor(const Formatting &f, const QString &style, const QTextBlockFormat &base)
{
    QTextBlockFormat bf = normalBlockFormat(f);
    // keep list related and page-break properties from base
    if (base.hasProperty(QTextFormat::PageBreakPolicy))
        bf.setPageBreakPolicy(base.pageBreakPolicy());
    if (base.hasProperty(props::PageBreak))
        bf.setProperty(props::PageBreak, base.property(props::PageBreak));
    if (base.hasProperty(QTextFormat::BlockIndent))
        bf.setIndent(base.indent());
    if (base.objectIndex() >= 0)
        bf.setObjectIndex(base.objectIndex()); // stays an item of its list
    // explicit centre alignment on an unstyled paragraph is semantic
    if (style.isEmpty() && base.hasProperty(QTextFormat::BlockAlignment)
        && (base.alignment() & Qt::AlignHCenter)) {
        bf.setAlignment(Qt::AlignHCenter);
        bf.setTextIndent(0);
    }

    if (const Style *s = styles::find(f, style)) {
        bf.setProperty(props::Style, s->name);
        if (s->headingLevel > 0)
            bf.setHeadingLevel(s->headingLevel);
        if (s->para.bits) {
            direct::applyParaSpec(bf, s->para);
            bf.clearProperty(props::ParaDirect); // the style's formatting is not set by hand
        }
    }
    // formatting the writer set by hand wins over project and style
    const direct::ParaSpec own = direct::paraSpec(base);
    if (own.bits) {
        direct::applyParaSpec(bf, own);
        // centred or right-aligned text is not indented unless asked for
        if ((own.bits & direct::Align) && !(own.bits & direct::First)
            && (own.align & (Qt::AlignHCenter | Qt::AlignRight)))
            bf.setTextIndent(0);
    }
    return bf;
}

QTextCharFormat charFormatFor(const Formatting &f, const QString &style)
{
    QTextCharFormat cf;
    if (const Style *s = styles::find(f, style))
        styles::applyChars(cf, *s);
    return cf;
}

void applyParagraphStyle(QTextCursor cursor, const QString &style, const Formatting &f)
{
    QTextDocument *doc = cursor.document();
    if (!doc)
        return;
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextBlock b = doc->findBlock(start);
    const QTextBlock last = doc->findBlock(end);
    const Style *ns = styles::find(f, style);
    cursor.beginEditBlock();
    while (b.isValid()) {
        const Style *old = styles::find(f, b.blockFormat().property(props::Style).toString());
        QTextCursor c(b);
        // a style replaces the paragraph formatting set by hand
        QTextBlockFormat base = b.blockFormat();
        base.clearProperty(props::ParaDirect);
        base.clearProperty(QTextFormat::TabPositions);
        if (old)
            base.clearProperty(QTextFormat::BlockAlignment);
        if (ns)
            base.setObjectIndex(-1); // a styled paragraph leaves its list
        QTextBlockFormat nbf = blockFormatFor(f, style, base);
        if (nbf.objectIndex() >= 0)
            nbf.setTextIndent(0);
        c.setBlockFormat(nbf);
        // char formats over the block content: the old style's formatting
        // goes, the new one comes; character styles stay on top
        auto restyled = [&](QTextCharFormat cf) {
            const Style *chr = styles::findChar(f, cf.stringProperty(props::CharStyle));
            cf = styles::withoutStyles(cf, old, f);
            const direct::CharSpec own = direct::charSpec(cf);
            if (ns)
                styles::applyChars(cf, *ns);
            if (chr)
                styles::applyChars(cf, *chr);
            if (!own.isEmpty()) {
                direct::mergeCharSpec(cf, own);
                if (own.size > 0)
                    cf.clearProperty(QTextFormat::FontSizeAdjustment);
            }
            return cf;
        };
        struct Piece {
            int s, e;
            QTextCharFormat cf;
        };
        QList<Piece> pieces;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid() || frag.charFormat().isImageFormat())
                continue;
            pieces.append({frag.position(), frag.position() + frag.length(), restyled(frag.charFormat())});
        }
        for (const Piece &p : pieces) {
            QTextCursor fc(doc);
            fc.setPosition(p.s);
            fc.setPosition(p.e, QTextCursor::KeepAnchor);
            fc.setCharFormat(p.cf);
        }
        c.setBlockCharFormat(restyled(b.charFormat()));
        if (b == last)
            break;
        b = b.next();
    }
    cursor.endEditBlock();
}

void prepareDocument(QTextDocument *doc, const Formatting &f, double zoom)
{
    doc->setDefaultFont(baseFont(f, zoom));
    doc->setDocumentMargin(6);
    QTextOption opt = doc->defaultTextOption();
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc->setDefaultTextOption(opt);
    doc->setIndentWidth(28);
}

void normalizeDocument(QTextDocument *doc, const Formatting &f)
{
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString style = b.blockFormat().property(props::Style).toString();
        QTextCursor bc(b);
        QTextBlockFormat nbf = blockFormatFor(f, style, b.blockFormat());
        if (b.textList() && !(nbf.intProperty(props::ParaDirect) & direct::First))
            nbf.setTextIndent(0);
        bc.setBlockFormat(nbf);
        if (!style.isEmpty()) {
            const QTextCharFormat add = charFormatFor(f, style);
            QTextCharFormat bcf = b.charFormat();
            bcf.merge(add);
            bc.setBlockCharFormat(bcf);
        }
    }
    c.endEditBlock();
}

QTextCharFormat semanticCharFormat(const QTextCharFormat &in)
{
    QTextCharFormat out;
    if (in.fontWeight() >= QFont::DemiBold)
        out.setFontWeight(QFont::Bold);
    if (in.fontItalic())
        out.setFontItalic(true);
    if (in.fontUnderline())
        out.setFontUnderline(true);
    if (in.fontStrikeOut())
        out.setFontStrikeOut(true);
    if (in.verticalAlignment() == QTextCharFormat::AlignSuperScript
        || in.verticalAlignment() == QTextCharFormat::AlignSubScript)
        out.setVerticalAlignment(in.verticalAlignment());
    if (in.isAnchor() && !in.anchorHref().isEmpty()) {
        out.setAnchor(true);
        out.setAnchorHref(in.anchorHref());
        out.setFontUnderline(true);
        out.setForeground(QColor(30, 90, 200));
    }
    for (int p : {int(props::NoteKind), int(props::NoteId), int(props::NoteText), int(props::NoteColor),
                  int(props::Highlight), int(props::Revision), int(props::CharStyle)}) {
        if (in.hasProperty(p))
            out.setProperty(p, in.property(p));
    }
    if (in.hasProperty(props::Highlight) || in.hasProperty(props::NoteKind) || in.hasProperty(props::Revision)) {
        if (in.hasProperty(QTextFormat::BackgroundBrush))
            out.setBackground(in.background());
        if (in.hasProperty(QTextFormat::ForegroundBrush))
            out.setForeground(in.foreground());
    }
    return out;
}

void insertSeparator(QTextCursor &c, const QString &kind, const QString &custom, const Formatting &f)
{
    if (kind == QLatin1String("none"))
        return;
    const QTextBlockFormat normal = normalBlockFormat(f);
    if (kind == QLatin1String("page")) {
        QTextBlockFormat bf = normal;
        bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        c.insertBlock(bf);
        return;
    }
    c.insertBlock(normal);
    if (kind == QLatin1String("empty")) {
        c.insertBlock(normal);
    } else if (kind == QLatin1String("custom")) {
        QTextBlockFormat centered = normal;
        centered.setAlignment(Qt::AlignHCenter);
        centered.setTextIndent(0);
        c.setBlockFormat(centered);
        c.insertText(custom);
        c.insertBlock(normal);
    }
}

} // namespace display
} // namespace qb
