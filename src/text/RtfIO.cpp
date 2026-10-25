#include "text/RtfIO.h"

#include "text/Script.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QBuffer>
#include <QImage>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QUrl>
#include <QUuid>

namespace qb {
namespace rtf {

// ======================================================================
// helpers
// ======================================================================

static const ushort kCp1252[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                   0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                   0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                   0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

static QChar decodeByte(uchar b, int codepage)
{
    if (b >= 0x80 && b <= 0x9F && (codepage == 1252 || codepage == 0))
        return QChar(kCp1252[b - 0x80]);
    if (codepage == 10000 && b >= 0x80) { // Mac Roman (partial: common letters)
        static const ushort macRoman[128] = {
            0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3,
            0x00E5, 0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
            0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3,
            0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
            0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA,
            0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
            0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D,
            0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
            0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE,
            0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
            0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};
        return QChar(macRoman[b - 0x80]);
    }
    return QChar(b);
}

bool looksLikeRtf(const QByteArray &data)
{
    int i = 0;
    while (i < data.size() && (data[i] == ' ' || data[i] == '\n' || data[i] == '\r' || data[i] == '\t'))
        ++i;
    return data.mid(i, 5) == "{\\rtf";
}

static QString familyOf(const QTextCharFormat &cf)
{
    const QVariant v = cf.property(QTextFormat::FontFamilies);
    if (v.isValid()) {
        const QStringList l = v.toStringList();
        if (!l.isEmpty())
            return l.first();
    }
    return QString();
}

// ======================================================================
// writer
// ======================================================================

namespace {

class Writer
{
public:
    Writer(const QTextDocument *doc, const Formatting &f) : m_doc(doc), m_f(f)
    {
        m_bodyFamily = display::resolvedFontFamily(f);
        m_fonts << m_bodyFamily;
        m_colors << QColor(); // index 0 = auto
        // the style table: the project's styles, then the elements of the
        // script format if the text uses them
        m_styles = styles::sheet(f).styles;
        if (script::hasElements(doc))
            m_styles << script::data(f).sheet.styles;
    }

    QByteArray run()
    {
        // first pass: fonts & colours
        for (QTextBlock b = m_doc->begin(); b.isValid(); b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat cf = it.fragment().charFormat();
                const QString fam = familyOf(cf);
                if (!fam.isEmpty() && !m_fonts.contains(fam))
                    m_fonts << fam;
                colorIndex(explicitForeground(cf));
                colorIndex(explicitBackground(cf));
            }
        }
        QByteArray body;
        bool first = true;
        for (QTextBlock b = m_doc->begin(); b.isValid(); b = b.next()) {
            if (!first)
                body += "\\par\n";
            first = false;
            body += paragraph(b);
        }

        QByteArray out = "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl";
        for (int i = 0; i < m_fonts.size(); ++i)
            out += "{\\f" + QByteArray::number(i) + "\\fnil\\fcharset0 " + escape(m_fonts[i]) + ";}";
        out += "}\n{\\colortbl;";
        for (int i = 1; i < m_colors.size(); ++i) {
            const QColor &c = m_colors[i];
            out += "\\red" + QByteArray::number(c.red()) + "\\green" + QByteArray::number(c.green()) + "\\blue"
                   + QByteArray::number(c.blue()) + ";";
        }
        out += "}\n{\\stylesheet{\\s0\\snext0 Normal;}";
        const auto &st = m_styles;
        for (int i = 0; i < st.size(); ++i) {
            if (st[i].character)
                out += "{\\*\\cs" + QByteArray::number(i + 1) + "\\additive " + escape(st[i].name) + ";}";
            else
                out += "{\\s" + QByteArray::number(i + 1) + "\\snext0 " + escape(st[i].name) + ";}";
        }
        out += "}\n{\\*\\qbformat 1}\n\\pard\\plain ";
        out += body;
        out += "}\n";
        return out;
    }

private:
    const QTextDocument *m_doc;
    Formatting m_f;
    QList<Style> m_styles;
    QString m_bodyFamily;
    QStringList m_fonts;
    QList<QColor> m_colors;

    static QColor explicitForeground(const QTextCharFormat &cf)
    {
        if (cf.hasProperty(props::NoteKind) || cf.isAnchor())
            return QColor(); // display colour only
        if (!cf.hasProperty(QTextFormat::ForegroundBrush))
            return QColor();
        const QBrush br = cf.foreground();
        if (br.style() == Qt::NoBrush)
            return QColor();
        return br.color();
    }

    static QColor explicitBackground(const QTextCharFormat &cf)
    {
        if (cf.hasProperty(props::NoteKind))
            return QColor();
        if (!cf.hasProperty(QTextFormat::BackgroundBrush))
            return QColor();
        const QBrush br = cf.background();
        if (br.style() == Qt::NoBrush)
            return QColor();
        return br.color();
    }

    int colorIndex(const QColor &c)
    {
        if (!c.isValid())
            return 0;
        const QColor opaque(c.red(), c.green(), c.blue());
        int i = m_colors.indexOf(opaque);
        if (i < 0) {
            m_colors << opaque;
            i = m_colors.size() - 1;
        }
        return i;
    }

    static QByteArray twips(double px) { return QByteArray::number(qRound(px * 15.0)); }

    QByteArray escape(const QString &s) const
    {
        QByteArray out;
        out.reserve(s.size() + 8);
        for (const QChar c : s) {
            const ushort u = c.unicode();
            if (u == '\\' || u == '{' || u == '}') {
                out += '\\';
                out += char(u);
            } else if (u == '\t') {
                out += "\\tab ";
            } else if (u == 0x2028 || u == '\n') {
                out += "\\line ";
            } else if (u == 0x00A0) {
                out += "\\~";
            } else if (u == 0x2029 || u == '\r' || u == 0xFFFC) {
                // skip
            } else if (u < 0x80) {
                if (u >= 32)
                    out += char(u);
            } else {
                out += "\\u" + QByteArray::number(qint16(u)) + '?';
            }
        }
        return out;
    }

    QByteArray paragraph(const QTextBlock &b)
    {
        QByteArray out = "\\pard\\plain";
        const QTextBlockFormat bf = b.blockFormat();
        const QString style = bf.property(props::Style).toString();
        const auto &st = m_styles;
        for (int i = 0; i < st.size(); ++i)
            if (st[i].name == style && !st[i].character)
                out += "\\s" + QByteArray::number(i + 1);
        const Qt::Alignment a = bf.alignment();
        if (a & Qt::AlignHCenter)
            out += "\\qc";
        else if (a & Qt::AlignRight)
            out += "\\qr";
        else if (a & Qt::AlignJustify)
            out += "\\qj";
        else
            out += "\\ql";
        QTextList *list = b.textList();
        double indent = bf.textIndent();
        double left = bf.leftMargin();
        int level = 0;
        bool numbered = false;
        if (list) {
            level = qMax(0, list->format().indent() - 1);
            const auto ls = list->format().style();
            numbered = ls == QTextListFormat::ListDecimal || ls == QTextListFormat::ListLowerAlpha
                       || ls == QTextListFormat::ListUpperAlpha || ls == QTextListFormat::ListLowerRoman
                       || ls == QTextListFormat::ListUpperRoman;
            indent = -18;
            left += 36 * (level + 1);
        }
        if (qAbs(indent) > 0.01)
            out += "\\fi" + twips(indent);
        if (left > 0.01)
            out += "\\li" + twips(left);
        if (bf.rightMargin() > 0.01)
            out += "\\ri" + twips(bf.rightMargin());
        if (bf.topMargin() > 0.01)
            out += "\\sb" + twips(bf.topMargin());
        if (bf.bottomMargin() > 0.01)
            out += "\\sa" + twips(bf.bottomMargin());
        if (bf.lineHeightType() == QTextBlockFormat::ProportionalHeight && qAbs(bf.lineHeight() - 100) > 0.5)
            out += "\\sl" + QByteArray::number(qRound(240 * bf.lineHeight() / 100.0)) + "\\slmult1";
        else if (bf.lineHeightType() == QTextBlockFormat::FixedHeight)
            out += "\\sl-" + twips(bf.lineHeight()) + "\\slmult0";
        else if (bf.lineHeightType() == QTextBlockFormat::MinimumHeight)
            out += "\\sl" + twips(bf.lineHeight()) + "\\slmult0";
        for (const QTextOption::Tab &tab : bf.tabPositions()) {
            if (tab.type == QTextOption::RightTab)
                out += "\\tqr";
            else if (tab.type == QTextOption::CenterTab)
                out += "\\tqc";
            else if (tab.type == QTextOption::DelimiterTab)
                out += "\\tqdec";
            out += "\\tx" + twips(tab.position);
        }
        const int own = bf.intProperty(props::ParaDirect);
        if (own)
            out += "\\qbown" + QByteArray::number(own);
        if ((bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) || bf.property(props::PageBreak).toBool())
            out += "\\pagebb";
        out += ' ';
        if (list) {
            out += "{\\*\\qblist " + QByteArray::number(level) + ' ' + (numbered ? "2" : "1") + "}";
            if (numbered)
                out += "{\\pntext " + QByteArray::number(list->itemNumber(b) + 1) + ".\\tab}";
            else
                out += "{\\pntext \\bullet\\tab}";
        }
        out += runs(b);
        return out;
    }

    QByteArray charCodes(const QTextCharFormat &cf)
    {
        QByteArray o;
        if (cf.hasProperty(props::CharStyle)) {
            const int idx = styles::sheet(m_f).indexOf(cf.stringProperty(props::CharStyle));
            if (idx >= 0 && styles::sheet(m_f).styles[idx].character)
                o += "\\cs" + QByteArray::number(idx + 1);
        }
        const QString fam = familyOf(cf);
        o += "\\f" + QByteArray::number(fam.isEmpty() ? 0 : qMax(0, int(m_fonts.indexOf(fam))));
        double size = m_f.fontSize;
        if (cf.hasProperty(QTextFormat::FontPointSize) && cf.fontPointSize() > 0)
            size = cf.fontPointSize();
        else if (cf.hasProperty(QTextFormat::FontSizeAdjustment)) {
            static const double factors[] = {0.7, 0.8, 1.0, 1.2, 1.5, 2.0, 2.4};
            const int idx = qBound(0, cf.intProperty(QTextFormat::FontSizeAdjustment) + 2, 6);
            size = m_f.fontSize * factors[idx];
        }
        o += "\\fs" + QByteArray::number(qRound(size * 2));
        if (cf.fontWeight() >= QFont::DemiBold)
            o += "\\b";
        if (cf.fontItalic())
            o += "\\i";
        if (cf.fontUnderline() && !cf.isAnchor())
            o += "\\ul";
        if (cf.fontStrikeOut())
            o += "\\strike";
        if (cf.verticalAlignment() == QTextCharFormat::AlignSuperScript)
            o += "\\super";
        else if (cf.verticalAlignment() == QTextCharFormat::AlignSubScript)
            o += "\\sub";
        const direct::CharSpec spec = direct::charSpec(cf);
        if (spec.spacing > 0)
            o += "\\expndtw" + QByteArray::number(qRound(direct::spacingToEm(spec.spacing) * size * 20));
        if (spec.caps == 1)
            o += "\\scaps";
        else if (spec.caps == 2)
            o += "\\caps";
        if (spec.weight > 0)
            o += "\\qbweight" + QByteArray::number(spec.weight);
        const int fg = colorIndex(explicitForeground(cf));
        if (fg)
            o += "\\cf" + QByteArray::number(fg);
        const int bg = colorIndex(explicitBackground(cf));
        if (bg)
            o += "\\highlight" + QByteArray::number(bg) + "\\cb" + QByteArray::number(bg);
        if (cf.property(props::Highlight).toBool())
            o += "\\qbhl";
        const int rev = cf.intProperty(props::Revision);
        if (rev > 0)
            o += "\\qbrev" + QByteArray::number(rev);
        return o;
    }

    QByteArray image(const QTextImageFormat &imf)
    {
        QTextDocument *doc = const_cast<QTextDocument *>(m_doc);
        QVariant v = doc->resource(QTextDocument::ImageResource, QUrl(imf.name()));
        QImage img;
        if (v.canConvert<QImage>())
            img = v.value<QImage>();
        else if (v.canConvert<QPixmap>())
            img = v.value<QPixmap>().toImage();
        else if (v.canConvert<QByteArray>())
            img.loadFromData(v.toByteArray());
        if (img.isNull())
            img.load(imf.name());
        if (img.isNull())
            return QByteArray();
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        const double w = imf.width() > 0 ? imf.width() : img.width();
        const double h = imf.height() > 0 ? imf.height() : (imf.width() > 0 ? img.height() * imf.width() / img.width()
                                                                               : img.height());
        QByteArray o = "{\\pict\\pngblip\\picw" + QByteArray::number(img.width()) + "\\pich"
                       + QByteArray::number(img.height()) + "\\picwgoal" + twips(w) + "\\pichgoal" + twips(h) + "\n";
        const QByteArray hex = png.toHex();
        for (int i = 0; i < hex.size(); i += 128)
            o += hex.mid(i, 128) + '\n';
        o += "}";
        return o;
    }

    struct Frag {
        QString text;
        QTextCharFormat cf;
    };

    QByteArray runs(const QTextBlock &b)
    {
        QList<Frag> frags;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.isValid())
                frags.append({f.text(), f.charFormat()});
        }
        QByteArray out;
        int i = 0;
        while (i < frags.size()) {
            const QTextCharFormat &cf = frags[i].cf;
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            const QString id = cf.stringProperty(props::NoteId);
            int j = i + 1;
            while (j < frags.size() && NoteKind(frags[j].cf.intProperty(props::NoteKind)) == kind
                   && frags[j].cf.stringProperty(props::NoteId) == id)
                ++j;
            const QByteArray inner = links(frags.mid(i, j - i));
            const QColor col = cf.property(props::NoteColor).value<QColor>();
            const QByteArray colorDest = col.isValid() ? "{\\*\\qbcolor " + col.name().toLatin1() + "}" : QByteArray();
            switch (kind) {
            case NoteKind::None: out += inner; break;
            case NoteKind::InlineAnnotation:
                out += "{\\qbannot" + (colorDest.isEmpty() ? QByteArray(" ") : colorDest) + inner + "}";
                break;
            case NoteKind::InlineFootnote: out += "{\\qbfn " + inner + "}"; break;
            case NoteKind::Comment:
                out += "{\\qbcmt{\\*\\qbnote " + escape(cf.stringProperty(props::NoteText)) + "}" + colorDest + inner
                       + "}";
                break;
            case NoteKind::LinkedFootnote:
                out += "{\\qblfn{\\*\\qbnote " + escape(cf.stringProperty(props::NoteText)) + "}" + inner + "}";
                break;
            }
            i = j;
        }
        return out;
    }

    QByteArray links(const QList<Frag> &frags)
    {
        QByteArray out;
        int i = 0;
        while (i < frags.size()) {
            const QString href = frags[i].cf.isAnchor() ? frags[i].cf.anchorHref() : QString();
            int j = i + 1;
            while (j < frags.size() && (frags[j].cf.isAnchor() ? frags[j].cf.anchorHref() : QString()) == href)
                ++j;
            QByteArray inner;
            for (int k = i; k < j; ++k) {
                const Frag &fr = frags[k];
                if (fr.cf.isImageFormat()) {
                    for (int n = 0; n < fr.text.size(); ++n)
                        inner += image(fr.cf.toImageFormat());
                    continue;
                }
                inner += "{" + charCodes(fr.cf) + " " + escape(fr.text) + "}";
            }
            if (href.isEmpty())
                out += inner;
            else
                out += "{\\field{\\*\\fldinst{HYPERLINK \"" + escape(href) + "\"}}{\\fldrslt " + inner + "}}";
            i = j;
        }
        return out;
    }
};

// ======================================================================
// reader
// ======================================================================

enum Dest {
    D_Normal,
    D_Skip,
    D_FontTable,
    D_ColorTable,
    D_StyleSheet,
    D_Pict,
    D_FldInst,
    D_QbNote,
    D_QbColor,
    D_QbList,
    D_ScrvColor,
    D_Annotation,
    D_Collect // generic text collection (e.g. atnid)
};

struct CharState {
    bool b = false, i = false, ul = false, strike = false, sup = false, sub = false, hidden = false;
    int f = -1;
    int fs = -1;
    int cf = 0;
    int cb = 0;
    int uc = 1;
    bool userHighlight = false;
    int revision = 0;
    int expnd = 0;   // letter spacing, twips
    int caps = 0;    // 1 small caps, 2 all caps
    int weight = 0;  // font weight other than normal / bold
    int charStyle = -1; // \\csN
    NoteKind note = NoteKind::None;
    QString noteId;
    QString noteText;
    QColor noteColor;
    QString href;
    bool inFootnote = false;
};

struct ParState {
    int align = 0; // 0 left 1 center 2 right 3 justify
    int fi = 0, li = 0, ri = 0, sb = 0, sa = 0, sl = 0;
    bool slmult = false;
    int style = -1;
    bool pagebb = false;
    int listLevel = -1;
    int listType = 0;
    bool hasLs = false;
    QList<QTextOption::Tab> tabs;
    int tabKind = 0; // alignment of the next \tx: 0 left 1 right 2 centre 3 decimal
    int own = 0;     // direct::ParaBit set of a Quillbinder file
};

struct Group {
    CharState cs;
    ParState ps;
    Dest dest = D_Normal;
    bool fieldStart = false;
};

struct Pict {
    QByteArray hex;
    QByteArray bin;
    QString type;
    int picw = 0, pich = 0, goalw = 0, goalh = 0, scalex = 100, scaley = 100;
};

class Reader
{
public:
    Reader(const QByteArray &data, QTextDocument *doc, const Formatting &f, const ReadOptions &opt)
        : m_data(data), m_doc(doc), m_f(f), m_opt(opt), m_cur(doc)
    {
        m_bodyFamily = display::resolvedFontFamily(f);
    }

    ReadResult run()
    {
        ReadResult res;
        m_res = &res;
        if (!looksLikeRtf(m_data)) {
            res.error = QStringLiteral("Not an RTF document");
            return res;
        }
        m_stack.append(Group());
        parse();
        flushParagraph(true);
        res.ok = true;
        return res;
    }

private:
    const QByteArray &m_data;
    QTextDocument *m_doc;
    Formatting m_f;
    ReadOptions m_opt;
    QTextCursor m_cur;
    QString m_bodyFamily;
    ReadResult *m_res = nullptr;
    int m_pos = 0;
    QList<Group> m_stack;
    int m_codepage = 1252;
    int m_ucSkip = 0;
    bool m_starNext = false;
    bool m_firstBlock = true;
    bool m_pendingPageBreak = false;
    bool m_scrvAnnot = false, m_scrvFn = false;
    bool m_afterPar = false;
    bool m_footnoteFresh = false;
    ParState m_finalPar;
    bool m_haveFinalPar = false;
    QColor m_scrvColor;

    // tables
    QMap<int, QString> m_fonts;
    QMap<int, int> m_fontCharset;
    int m_curFont = -1;
    QString m_fontName;
    QList<QColor> m_colors;
    int m_r = 0, m_g = 0, m_b = 0;
    bool m_colorHasValue = false;
    QMap<int, QString> m_styleNames;
    QMap<int, QString> m_charStyleNames;
    int m_curStyle = 0;
    QString m_styleName;
    bool m_styleIsChar = false;

    // destinations
    QString m_collect;
    Pict m_pict;
    struct Field {
        int depth;
        QString inst;
    };
    QList<Field> m_fields;
    QMap<int, QTextList *> m_lists;
    QMap<int, int> m_listTypes;
    bool m_prevBlockList = false;
    QString m_qbNote;
    QString m_qbListSpec;

    Group &top() { return m_stack.last(); }
    CharState &cs() { return m_stack.last().cs; }
    ParState &ps() { return m_stack.last().ps; }

    // ------------------------------------------------------------ parsing
    void parse()
    {
        const int n = m_data.size();
        while (m_pos < n) {
            const char c = m_data.at(m_pos);
            if (c == '{') {
                ++m_pos;
                Group g = m_stack.last();
                g.fieldStart = false;
                m_stack.append(g);
                // a pending "\*" only applies to the next control word
                continue;
            }
            if (c == '}') {
                ++m_pos;
                endGroup();
                if (m_stack.isEmpty())
                    return;
                continue;
            }
            if (c == '\\') {
                parseControl();
                continue;
            }
            if (c == '\r' || c == '\n') {
                ++m_pos;
                continue;
            }
            // plain text run
            int start = m_pos;
            while (m_pos < n) {
                const char d = m_data.at(m_pos);
                if (d == '{' || d == '}' || d == '\\' || d == '\r' || d == '\n')
                    break;
                ++m_pos;
            }
            text(m_data.mid(start, m_pos - start));
        }
    }

    void parseControl()
    {
        const int n = m_data.size();
        ++m_pos; // backslash
        if (m_pos >= n)
            return;
        const char c = m_data.at(m_pos);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            ++m_pos;
            controlSymbol(c);
            return;
        }
        int start = m_pos;
        while (m_pos < n) {
            const char d = m_data.at(m_pos);
            if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z'))
                ++m_pos;
            else
                break;
        }
        QByteArray word = m_data.mid(start, m_pos - start);
        // Scrivener style markers use underscores: \Scrv_annot, \end_Scrv_annot
        if ((word == "Scrv" || word == "end") && m_pos < n && m_data.at(m_pos) == '_') {
            while (m_pos < n) {
                const char d = m_data.at(m_pos);
                if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || d == '_')
                    ++m_pos;
                else
                    break;
            }
            word = m_data.mid(start, m_pos - start);
        }
        bool hasParam = false;
        int param = 0;
        bool neg = false;
        if (m_pos < n && m_data.at(m_pos) == '-') {
            neg = true;
            ++m_pos;
        }
        int ps = m_pos;
        while (m_pos < n && m_data.at(m_pos) >= '0' && m_data.at(m_pos) <= '9')
            ++m_pos;
        if (m_pos > ps) {
            hasParam = true;
            param = m_data.mid(ps, m_pos - ps).toInt();
            if (neg)
                param = -param;
        } else if (neg) {
            --m_pos;
        }
        if (m_pos < n && m_data.at(m_pos) == ' ')
            ++m_pos;
        controlWord(word, hasParam, param);
    }

    void controlSymbol(char c)
    {
        switch (c) {
        case '*': m_starNext = true; return;
        case '\'': {
            const QByteArray hex = m_data.mid(m_pos, 2);
            m_pos += 2;
            bool ok = false;
            const uchar b = uchar(hex.toInt(&ok, 16));
            if (!ok)
                return;
            if (m_ucSkip > 0) {
                --m_ucSkip;
                return;
            }
            emitChar(decodeByte(b, m_codepage));
            return;
        }
        case '~': emitChar(QChar(0x00A0)); return;
        case '_': emitChar(QChar(0x2011)); return;
        case '-': return; // optional hyphen
        case '\\': emitChar(QLatin1Char('\\')); return;
        case '{': emitChar(QLatin1Char('{')); return;
        case '}': emitChar(QLatin1Char('}')); return;
        case '\n':
        case '\r': paragraphBreak(); return;
        case '|':
        case ':': return;
        default: return;
        }
    }

    void setDest(Dest d) { top().dest = d; }

    void controlWord(const QByteArray &w, bool hasParam, int p)
    {
        const bool starred = m_starNext;
        m_starNext = false;
        Dest &dest = top().dest;

        if (dest == D_Skip)
            return;
        if (dest == D_Pict) {
            pictWord(w, hasParam, p);
            return;
        }
        if (dest == D_FontTable) {
            if (w == "f") {
                m_curFont = p;
                m_fontName.clear();
            } else if (w == "fcharset") {
                m_fontCharset[m_curFont] = p;
            } else if (starred || w == "panose" || w == "falt" || w == "fname") {
                setDest(D_Skip);
                // keep font table state; the inner group is skipped
            }
            return;
        }
        if (dest == D_ColorTable) {
            if (w == "red") { m_r = p; m_colorHasValue = true; }
            else if (w == "green") { m_g = p; m_colorHasValue = true; }
            else if (w == "blue") { m_b = p; m_colorHasValue = true; }
            return;
        }
        if (dest == D_ScrvColor) {
            if (w == "red") m_r = p;
            else if (w == "green") m_g = p;
            else if (w == "blue") m_b = p;
            else if (w == "colortbl") {}
            return;
        }
        if (dest == D_StyleSheet) {
            if (w == "s") {
                m_curStyle = p;
                m_styleIsChar = false;
            } else if (w == "cs" || w == "ds" || w == "ts") {
                m_curStyle = w == "cs" ? p : -1;
                m_styleIsChar = true;
            } else if (starred && w != "cs") {
                setDest(D_Skip);
            }
            return;
        }
        if (dest == D_QbNote || dest == D_Collect || dest == D_QbColor || dest == D_QbList || dest == D_FldInst
            || dest == D_Annotation) {
            // text collecting destinations: handle a few escapes
            if (w == "u" && hasParam) {
                m_collect += QChar(ushort(p < 0 ? p + 65536 : p));
                m_ucSkip = cs().uc;
            } else if (w == "line" || w == "par") {
                m_collect += QLatin1Char('\n');
            } else if (w == "tab") {
                m_collect += QLatin1Char('\t');
            } else if (w == "field" || w == "fldrslt") {
                // nested field inside note text: keep collecting text
            }
            return;
        }

        // --------------------------------------------------- destinations
        if (w == "fonttbl") { setDest(D_FontTable); return; }
        if (w == "colortbl") {
            setDest(D_ColorTable);
            m_colors.clear();
            m_r = m_g = m_b = 0;
            m_colorHasValue = false;
            return;
        }
        if (w == "stylesheet") { setDest(D_StyleSheet); return; }
        if (w == "info" || w == "header" || w == "footer" || w == "headerl" || w == "headerr" || w == "headerf"
            || w == "footerl" || w == "footerr" || w == "footerf" || w == "object" || w == "nonshppict"
            || w == "pntext" || w == "pntxta" || w == "pntxtb" || w == "listtext" || w == "listtable"
            || w == "listoverridetable" || w == "revtbl" || w == "rsidtbl" || w == "xmlnstbl" || w == "themedata"
            || w == "colorschememapping" || w == "latentstyles" || w == "datastore" || w == "generator"
            || w == "bkmkstart" || w == "bkmkend" || w == "atnid" || w == "atnauthor" || w == "atndate"
            || w == "atnref" || w == "atrfstart" || w == "atrfend" || w == "pgdsctbl" || w == "mmathPr"
            || w == "expandedcolortbl" || w == "filetbl" || w == "sp" || w == "shpinst" || w == "fldtype") {
            setDest(D_Skip);
            return;
        }
        if (w == "shppict") { return; } // container of a \pict we want
        if (w == "pict") {
            setDest(D_Pict);
            m_pict = Pict();
            return;
        }
        if (w == "field") {
            m_fields.append({int(m_stack.size()), QString()});
            return;
        }
        if (w == "fldinst") {
            setDest(D_FldInst);
            m_collect.clear();
            return;
        }
        if (w == "fldrslt") {
            if (!m_fields.isEmpty())
                applyFieldResult(m_fields.last().inst);
            return;
        }
        if (w == "footnote") {
            m_footnoteFresh = true;
            cs().note = NoteKind::InlineFootnote;
            cs().noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            cs().inFootnote = true;
            // footnote text often starts with \pard\plain: keep char state sane
            return;
        }
        if (w == "annotation") {
            setDest(D_Annotation);
            m_collect.clear();
            return;
        }
        if (w == "qbnote") {
            setDest(D_QbNote);
            m_collect.clear();
            return;
        }
        if (w == "qbcolor") {
            setDest(D_QbColor);
            m_collect.clear();
            return;
        }
        if (w == "qblist") {
            setDest(D_QbList);
            m_collect.clear();
            return;
        }
        if (w == "qbformat") {
            setDest(D_Skip);
            return;
        }
        if (starred) {
            setDest(D_Skip);
            return;
        }

        // --------------------------------------------------- notes (own)
        if (w == "qbannot") {
            cs().note = NoteKind::InlineAnnotation;
            cs().noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            return;
        }
        if (w == "qbfn") {
            cs().note = NoteKind::InlineFootnote;
            cs().noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            return;
        }
        if (w == "qbcmt" || w == "qblfn") {
            cs().note = (w == "qbcmt") ? NoteKind::Comment : NoteKind::LinkedFootnote;
            cs().noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            return;
        }
        if (w == "qbhl") { cs().userHighlight = true; return; }
        if (w == "qbrev") { cs().revision = p; return; }

        // --------------------------------------------------- Scrivener
        if (w == "Scrv_annot") {
            m_scrvAnnot = true;
            m_scrvColor = QColor();
            return;
        }
        if (w == "end_Scrv_annot") { m_scrvAnnot = false; return; }
        if (w == "Scrv_fn") { m_scrvFn = true; return; }
        if (w == "end_Scrv_fn") { m_scrvFn = false; return; }
        if (w == "color" || w == "text") {
            if (m_pos < m_data.size() && m_data.at(m_pos) == '=') {
                ++m_pos;
                if (w == "color" && m_pos < m_data.size() && m_data.at(m_pos) == '{') {
                    // "{\colortbl;\red..}" describing the annotation colour
                    ++m_pos;
                    Group g = m_stack.last();
                    g.dest = D_ScrvColor;
                    m_stack.append(g);
                    m_r = m_g = m_b = 0;
                }
            }
            return;
        }

        // --------------------------------------------------- document
        if (w == "ansicpg") { m_codepage = p; return; }
        if (w == "mac") { m_codepage = 10000; return; }
        if (w == "uc") { cs().uc = hasParam ? p : 1; return; }
        if (w == "u") {
            if (!hasParam)
                return;
            emitChar(QChar(ushort(p < 0 ? p + 65536 : p)));
            m_ucSkip = cs().uc;
            return;
        }

        // --------------------------------------------------- characters
        if (w == "par") { paragraphBreak(); return; }
        if (w == "sect" || w == "page") {
            paragraphBreak();
            m_pendingPageBreak = true;
            return;
        }
        if (w == "row") { paragraphBreak(); return; }
        if (w == "cell" || w == "nestcell") { emitChar(QLatin1Char('\t')); return; }
        if (w == "line") { emitChar(QChar::LineSeparator); return; }
        if (w == "tab") { emitChar(QLatin1Char('\t')); return; }
        if (w == "emdash") { emitChar(QChar(0x2014)); return; }
        if (w == "endash") { emitChar(QChar(0x2013)); return; }
        if (w == "bullet") { emitChar(QChar(0x2022)); return; }
        if (w == "lquote") { emitChar(QChar(0x2018)); return; }
        if (w == "rquote") { emitChar(QChar(0x2019)); return; }
        if (w == "ldblquote") { emitChar(QChar(0x201C)); return; }
        if (w == "rdblquote") { emitChar(QChar(0x201D)); return; }
        if (w == "emspace" || w == "enspace" || w == "qmspace") { emitChar(QLatin1Char(' ')); return; }
        if (w == "chftn" || w == "chatn") { return; }

        // --------------------------------------------------- char format
        CharState &c = cs();
        const bool on = !hasParam || p != 0;
        if (w == "plain") {
            const CharState keep = c;
            c = CharState();
            c.uc = keep.uc;
            c.note = keep.note;
            c.noteId = keep.noteId;
            c.noteText = keep.noteText;
            c.noteColor = keep.noteColor;
            c.href = keep.href;
            c.inFootnote = keep.inFootnote;
            return;
        }
        if (w == "b") { c.b = on; return; }
        if (w == "i") { c.i = on; return; }
        if (w == "ul" || w == "uld" || w == "uldash" || w == "uldb" || w == "ulth" || w == "ulw"
            || w == "ulwave" || w == "uldashd" || w == "ulhwave" || w == "ulldash") {
            c.ul = on;
            return;
        }
        if (w == "ulnone") { c.ul = false; return; }
        if (w == "strike" || w == "striked") { c.strike = on; return; }
        if (w == "super") { c.sup = on; c.sub = false; return; }
        if (w == "sub") { c.sub = on; c.sup = false; return; }
        if (w == "nosupersub") { c.sup = c.sub = false; return; }
        if (w == "up") { c.sup = hasParam ? p > 0 : true; return; }
        if (w == "dn") { c.sub = hasParam ? p > 0 : true; return; }
        if (w == "v") { c.hidden = on; return; }
        if (w == "f") { c.f = p; return; }
        if (w == "fs") { c.fs = p; return; }
        if (w == "expndtw") { c.expnd = p; return; }
        if (w == "expnd") { c.expnd = p * 5; return; }
        if (w == "scaps") { c.caps = on ? 1 : 0; return; }
        if (w == "caps") { c.caps = on ? 2 : 0; return; }
        if (w == "qbweight") { c.weight = p; return; }
        if (w == "cs") { c.charStyle = hasParam ? p : -1; return; }
        if (w == "cf") { c.cf = p; return; }
        if (w == "cb" || w == "highlight" || w == "chcbpat") { c.cb = p; return; }

        // --------------------------------------------------- paragraph
        ParState &q = ps();
        if (w == "pard") {
            q = ParState();
            m_afterPar = false;
            return;
        }
        if (w == "ql") { q.align = 0; return; }
        if (w == "qc") { q.align = 1; return; }
        if (w == "qr") { q.align = 2; return; }
        if (w == "qj" || w == "qd") { q.align = 3; return; }
        if (w == "fi") { q.fi = p; return; }
        if (w == "li" || w == "lin") { q.li = p; return; }
        if (w == "ri" || w == "rin") { q.ri = p; return; }
        if (w == "sb") { q.sb = p; return; }
        if (w == "sa") { q.sa = p; return; }
        if (w == "sl") { q.sl = p; return; }
        if (w == "tqr") { q.tabKind = 1; return; }
        if (w == "tqc") { q.tabKind = 2; return; }
        if (w == "tqdec") { q.tabKind = 3; return; }
        if (w == "tx") {
            const QTextOption::TabType t = q.tabKind == 1 ? QTextOption::RightTab
                                           : q.tabKind == 2 ? QTextOption::CenterTab
                                           : q.tabKind == 3 ? QTextOption::DelimiterTab
                                                            : QTextOption::LeftTab;
            if (p > 0)
                q.tabs.append(QTextOption::Tab(p / 15.0, t, t == QTextOption::DelimiterTab ? QLatin1Char('.') : QChar()));
            q.tabKind = 0;
            return;
        }
        if (w == "qbown") { q.own = p; return; }
        if (w == "slmult") { q.slmult = p != 0; return; }
        if (w == "s") { q.style = p; return; }
        if (w == "pagebb") { q.pagebb = true; return; }
        if (w == "ls") { q.hasLs = true; if (q.listType == 0) q.listType = 1; if (q.listLevel < 0) q.listLevel = 0; return; }
        if (w == "ilvl") { q.listLevel = p; return; }
    }

    void pictWord(const QByteArray &w, bool, int p)
    {
        if (w == "pngblip") m_pict.type = QStringLiteral("png");
        else if (w == "jpegblip") m_pict.type = QStringLiteral("jpg");
        else if (w == "emfblip" || w == "wmetafile" || w == "macpict" || w == "dibitmap" || w == "wbitmap")
            m_pict.type = QStringLiteral("unsupported");
        else if (w == "picw") m_pict.picw = p;
        else if (w == "pich") m_pict.pich = p;
        else if (w == "picwgoal") m_pict.goalw = p;
        else if (w == "pichgoal") m_pict.goalh = p;
        else if (w == "picscalex") m_pict.scalex = p;
        else if (w == "picscaley") m_pict.scaley = p;
        else if (w == "bin") {
            m_pict.bin = m_data.mid(m_pos, p);
            m_pos += p;
        }
    }

    void applyFieldResult(const QString &instIn)
    {
        QString inst = instIn.trimmed();
        if (!inst.startsWith(QLatin1String("HYPERLINK"), Qt::CaseInsensitive))
            return;
        const int q1 = inst.indexOf(QLatin1Char('"'));
        const int q2 = inst.indexOf(QLatin1Char('"'), q1 + 1);
        QString url = (q1 >= 0 && q2 > q1) ? inst.mid(q1 + 1, q2 - q1 - 1)
                                            : inst.mid(9).trimmed();
        if (url.startsWith(QLatin1String("scrivcmt://"))) {
            const QString id = url.mid(11);
            cs().note = NoteKind::Comment;
            cs().noteId = id;
            m_res->linkedNoteIds << id;
            return;
        }
        if (url.startsWith(QLatin1String("scrivlnk://"))) {
            cs().href = QStringLiteral("qb://") + url.mid(11);
            return;
        }
        cs().href = url;
    }

    void endGroup()
    {
        if (m_stack.size() <= 1) {
            if (!m_stack.isEmpty())
                m_finalPar = m_stack.last().ps;
            m_stack.clear();
            return;
        }
        if (m_stack.size() == 2) {
            // the document group closes: its paragraph state belongs to the
            // last paragraph (files usually end without a final \par)
            m_finalPar = m_stack.last().ps;
            m_haveFinalPar = true;
        }
        const Group g = m_stack.takeLast();
        const Dest parentDest = m_stack.last().dest;
        switch (g.dest) {
        case D_FontTable:
            if (parentDest == D_FontTable) {
                // end of one {\fN ...;} entry
            }
            break;
        case D_Pict:
            if (parentDest != D_Pict)
                insertPicture();
            break;
        case D_FldInst:
            if (parentDest != D_FldInst && !m_fields.isEmpty())
                m_fields.last().inst += m_collect;
            break;
        case D_QbNote:
            if (parentDest != D_QbNote) {
                cs().noteText = m_collect;
            }
            break;
        case D_QbColor:
            if (parentDest != D_QbColor) {
                const QColor col(m_collect.trimmed());
                if (col.isValid())
                    cs().noteColor = col;
            }
            break;
        case D_QbList:
            if (parentDest != D_QbList) {
                const QStringList parts = m_collect.simplified().split(QLatin1Char(' '));
                if (parts.size() >= 2) {
                    ps().listLevel = parts[0].toInt();
                    ps().listType = parts[1].toInt();
                }
            }
            break;
        case D_ScrvColor:
            if (parentDest != D_ScrvColor)
                m_scrvColor = QColor(m_r, m_g, m_b);
            break;
        case D_Annotation:
            // standard RTF comment: attach to the previous character run is
            // not tracked; insert as an inline annotation instead
            if (parentDest != D_Annotation && !m_collect.trimmed().isEmpty()) {
                CharState saved = cs();
                cs().note = NoteKind::InlineAnnotation;
                cs().noteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                emitString(m_collect.trimmed());
                cs() = saved;
            }
            break;
        case D_StyleSheet:
            break;
        default:
            break;
        }
        // a group inside the font table / stylesheet finishing an entry
        if (parentDest == D_FontTable && g.dest == D_FontTable) {
            // nothing: names are stored when ';' is seen
        }
        // paragraph properties are not group-scoped in practice for the last
        // paragraph of a footnote: restore char state only.
        if (!m_fields.isEmpty() && m_fields.last().depth > m_stack.size())
            m_fields.removeLast();
        if (g.fieldStart) {
        }
    }

    // ------------------------------------------------------------ text
    void text(const QByteArray &bytes)
    {
        const Dest d = top().dest;
        if (d == D_Skip)
            return;
        if (d == D_Pict) {
            for (char ch : bytes)
                if (std::isxdigit(uchar(ch)))
                    m_pict.hex += ch;
            return;
        }
        if (d == D_FontTable) {
            for (char ch : bytes) {
                if (ch == ';') {
                    if (m_curFont >= 0)
                        m_fonts[m_curFont] = m_fontName.trimmed();
                    m_fontName.clear();
                } else {
                    m_fontName += QLatin1Char(ch);
                }
            }
            return;
        }
        if (d == D_ColorTable) {
            for (char ch : bytes) {
                if (ch == ';') {
                    m_colors << (m_colorHasValue ? QColor(m_r, m_g, m_b) : QColor());
                    m_r = m_g = m_b = 0;
                    m_colorHasValue = false;
                }
            }
            return;
        }
        if (d == D_StyleSheet) {
            for (char ch : bytes) {
                if (ch == ';') {
                    if (!m_styleIsChar)
                        m_styleNames[m_curStyle] = m_styleName.trimmed();
                    else if (m_curStyle >= 0)
                        m_charStyleNames[m_curStyle] = m_styleName.trimmed();
                    m_styleName.clear();
                    m_curStyle = 0;
                    m_styleIsChar = false;
                } else {
                    m_styleName += QLatin1Char(ch);
                }
            }
            return;
        }
        if (d == D_ScrvColor)
            return;
        QString s;
        s.reserve(bytes.size());
        for (char ch : bytes) {
            if (m_ucSkip > 0) {
                --m_ucSkip;
                continue;
            }
            s += decodeByte(uchar(ch), m_codepage);
        }
        emitString(s);
    }

    void emitChar(QChar c) { emitString(QString(c)); }

    void emitString(const QString &s)
    {
        if (s.isEmpty())
            return;
        const Dest d = top().dest;
        if (d == D_QbNote || d == D_QbColor || d == D_QbList || d == D_FldInst || d == D_Collect
            || d == D_Annotation) {
            m_collect += s;
            return;
        }
        if (d != D_Normal)
            return;
        const CharState &c = cs();
        if (c.hidden)
            return;
        QString out = s;
        if (c.inFootnote) {
            out.replace(QChar::LineSeparator, QLatin1Char(' '));
            if (m_footnoteFresh) {
                int n = 0;
                while (n < out.size() && out.at(n).isSpace())
                    ++n;
                out = out.mid(n);
                if (out.isEmpty())
                    return;
                m_footnoteFresh = false;
            }
        }
        const QTextCharFormat fmt = currentCharFormat();
        m_cur.insertText(out, fmt);
        m_afterPar = false;
        // statistics
        const QString fam = c.f >= 0 ? m_fonts.value(c.f) : QString();
        if (!fam.isEmpty())
            m_res->fontUsage[fam] += out.size();
        if (c.fs > 0)
            m_res->sizeUsage[c.fs] += out.size();
    }

    QTextCharFormat currentCharFormat() const
    {
        const CharState &c = m_stack.last().cs;
        QTextCharFormat f;
        if (c.charStyle >= 0) {
            if (const Style *s = styles::findChar(m_f, m_charStyleNames.value(c.charStyle))) {
                f.setProperty(props::CharStyle, s->name);
                if (!m_opt.keepFormatting)
                    styles::applyChars(f, *s); // the look comes from the style
            }
        }
        if (c.b)
            f.setFontWeight(QFont::Bold);
        if (c.i)
            f.setFontItalic(true);
        if (c.ul)
            f.setFontUnderline(true);
        if (c.strike)
            f.setFontStrikeOut(true);
        if (c.sup)
            f.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        else if (c.sub)
            f.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        if (m_opt.keepFormatting) {
            const QString fam = c.f >= 0 ? m_fonts.value(c.f) : QString();
            if (!fam.isEmpty() && fam.compare(m_bodyFamily, Qt::CaseInsensitive) != 0)
                f.setFontFamilies(QStringList{fam});
            if (c.fs > 0 && qAbs(c.fs / 2.0 - m_f.fontSize) > 0.01)
                f.setFontPointSize(c.fs / 2.0);
            if (c.cf > 0 && c.cf < m_colors.size()) {
                const QColor col = m_colors.at(c.cf);
                if (col.isValid() && col != QColor(0, 0, 0))
                    f.setForeground(col);
            }
            const double pt = c.fs > 0 ? c.fs / 2.0 : m_f.fontSize;
            if (c.expnd != 0 && pt > 0) {
                const int pct = direct::spacingFromEm(c.expnd / 20.0 / pt);
                if (pct > 0) {
                    f.setFontLetterSpacingType(QFont::PercentageSpacing);
                    f.setFontLetterSpacing(pct);
                }
            }
            if (c.caps == 1)
                f.setFontCapitalization(QFont::SmallCaps);
            else if (c.caps == 2)
                f.setFontCapitalization(QFont::AllUppercase);
            if (c.weight >= 100 && c.weight <= 900 && c.weight != 400 && c.weight != 700)
                f.setFontWeight(c.weight);
        }
        if (c.cb > 0 && c.cb < m_colors.size()) {
            const QColor col = m_colors.at(c.cb);
            if (col.isValid() && col != QColor(255, 255, 255)) {
                f.setProperty(props::Highlight, true);
                f.setBackground(col);
            }
        } else if (c.userHighlight) {
            f.merge(text::highlightFormat());
        }
        if (c.revision > 0)
            f.merge(text::revisionFormat(c.revision));
        if (!c.href.isEmpty())
            f.merge(text::linkFormat(c.href));
        NoteKind note = c.note;
        QString noteId = c.noteId;
        QColor noteColor = c.noteColor;
        if (note == NoteKind::None && m_scrvAnnot) {
            note = NoteKind::InlineAnnotation;
            noteColor = m_scrvColor;
        } else if (note == NoteKind::None && m_scrvFn) {
            note = NoteKind::InlineFootnote;
        }
        if (note != NoteKind::None) {
            QTextCharFormat nf = text::noteFormat(note, noteId, c.noteText, noteColor);
            f.merge(nf);
        }
        return f;
    }

    void insertPicture()
    {
        QByteArray raw = m_pict.bin.isEmpty() ? QByteArray::fromHex(m_pict.hex) : m_pict.bin;
        if (raw.isEmpty() || m_pict.type == QLatin1String("unsupported"))
            return;
        QImage img;
        if (!img.loadFromData(raw))
            return;
        const QString name = QStringLiteral("rtfimage://%1.png").arg(QUuid::createUuid().toString(QUuid::Id128));
        m_doc->addResource(QTextDocument::ImageResource, QUrl(name), img);
        QTextImageFormat imf;
        imf.setName(name);
        double w = m_pict.goalw > 0 ? m_pict.goalw / 15.0 : img.width();
        double h = m_pict.goalh > 0 ? m_pict.goalh / 15.0 : img.height();
        w = w * m_pict.scalex / 100.0;
        h = h * m_pict.scaley / 100.0;
        if (w > 0)
            imf.setWidth(w);
        if (h > 0)
            imf.setHeight(h);
        m_cur.insertImage(imf);
    }

    // ------------------------------------------------------------ paragraphs
    void paragraphBreak()
    {
        const Dest d = top().dest;
        if (d == D_QbNote || d == D_Collect || d == D_Annotation) {
            m_collect += QLatin1Char('\n');
            return;
        }
        if (d != D_Normal)
            return;
        if (cs().inFootnote) {
            emitString(QStringLiteral(" "));
            return;
        }
        flushParagraph(false);
        m_cur.insertBlock();
        m_afterPar = true;
    }

    void flushParagraph(bool last)
    {
        if (last && m_afterPar && m_doc->blockCount() > 1 && m_cur.block().length() <= 1) {
            // RTF ended with \par: drop the empty trailing paragraph
            m_cur.deletePreviousChar();
            return;
        }
        const ParState q = (m_stack.isEmpty() || (last && m_haveFinalPar)) ? m_finalPar : ps();
        QString styleName = m_styleNames.value(q.style);
        if (const Style *known = styles::find(m_f, styleName))
            styleName = known->name;
        else
            styleName.clear();
        QTextBlockFormat bf;
        if (m_opt.keepFormatting) {
            bf.setProperty(props::Style, styleName);
            if (const ParagraphStyle *s = styles::find(m_f, styleName))
                if (s->headingLevel)
                    bf.setHeadingLevel(s->headingLevel);
            switch (q.align) {
            case 1: bf.setAlignment(Qt::AlignHCenter); break;
            case 2: bf.setAlignment(Qt::AlignRight); break;
            case 3: bf.setAlignment(Qt::AlignJustify); break;
            default: bf.setAlignment(Qt::AlignLeft); break;
            }
            bf.setTextIndent(q.fi / 15.0);
            bf.setLeftMargin(q.li / 15.0);
            bf.setRightMargin(q.ri / 15.0);
            bf.setTopMargin(q.sb / 15.0);
            bf.setBottomMargin(q.sa / 15.0);
            if (q.sl != 0) {
                if (q.slmult)
                    bf.setLineHeight(qAbs(q.sl) * 100.0 / 240.0, QTextBlockFormat::ProportionalHeight);
                else if (q.sl < 0)
                    bf.setLineHeight(-q.sl / 15.0, QTextBlockFormat::FixedHeight);
                else
                    bf.setLineHeight(q.sl / 15.0, QTextBlockFormat::MinimumHeight);
            }
            if (!q.tabs.isEmpty())
                bf.setTabPositions(q.tabs);
            if (q.own)
                bf.setProperty(props::ParaDirect, q.own & direct::AllPara);
        } else {
            QTextBlockFormat base;
            if (q.align == 1)
                base.setAlignment(Qt::AlignHCenter);
            bf = display::blockFormatFor(m_f, styleName, base);
        }
        if (q.pagebb || m_pendingPageBreak) {
            bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            bf.setProperty(props::PageBreak, true);
            m_pendingPageBreak = false;
        }
        const bool isList = q.listType > 0 && q.listLevel >= 0;
        if (isList) {
            bf.setTextIndent(0);
            bf.setLeftMargin(0);
        }
        m_cur.setBlockFormat(bf);
        if (!styleName.isEmpty() && !m_opt.keepFormatting)
            m_cur.setBlockCharFormat(display::charFormatFor(m_f, styleName));

        if (isList) {
            if (!m_prevBlockList)
                m_lists.clear();
            for (auto it = m_lists.begin(); it != m_lists.end();) {
                if (it.key() > q.listLevel)
                    it = m_lists.erase(it);
                else
                    ++it;
            }
            QTextList *l = m_lists.value(q.listLevel, nullptr);
            if (l && m_listTypes.value(q.listLevel) == q.listType) {
                l->add(m_cur.block());
            } else {
                QTextListFormat lf;
                lf.setStyle(q.listType == 2 ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
                lf.setIndent(q.listLevel + 1);
                l = m_cur.createList(lf);
                m_lists[q.listLevel] = l;
                m_listTypes[q.listLevel] = q.listType;
            }
            m_prevBlockList = true;
        } else {
            m_prevBlockList = false;
        }
    }
};

} // namespace

// ======================================================================

QByteArray write(const QTextDocument *doc, const Formatting &f)
{
    Writer w(doc, f);
    return w.run();
}

ReadResult read(const QByteArray &data, QTextDocument *doc, const Formatting &f, const ReadOptions &opt)
{
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    Reader r(data, doc, f, opt);
    ReadResult res = r.run();
    doc->setUndoRedoEnabled(undo);
    doc->setModified(false);
    return res;
}

QString toPlainText(const QByteArray &data)
{
    if (!looksLikeRtf(data))
        return QString::fromUtf8(data);
    QTextDocument doc;
    Formatting f;
    ReadOptions o;
    o.keepFormatting = false;
    read(data, &doc, f, o);
    return text::plainText(&doc, true);
}

} // namespace rtf
} // namespace qb
