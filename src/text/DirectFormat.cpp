#include "text/DirectFormat.h"

#include "text/DisplayFormat.h"
#include "text/TextProps.h"

#include <QFont>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <cmath>
#include <functional>

namespace qb {
namespace direct {

namespace {

QString num(double v, int decimals = 2)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s;
}

QString cleanFamily(QString f)
{
    static const QRegularExpression bad(QStringLiteral("[\"'<>;{}\\\\]"));
    f.remove(bad);
    return f.trimmed();
}

// "12.5mm", "6pt", "20px", "1.2cm", "0.5in" -> px
double lengthToPx(const QString &t, bool *ok)
{
    static const QRegularExpression re(QStringLiteral("^(-?\\d+(?:\\.\\d+)?)(mm|cm|pt|px|in)?$"));
    const auto m = re.match(t.trimmed());
    if (ok)
        *ok = m.hasMatch();
    if (!m.hasMatch())
        return 0;
    const double v = m.captured(1).toDouble();
    const QString u = m.captured(2);
    if (u == QLatin1String("mm"))
        return v * kPxPerMm;
    if (u == QLatin1String("cm"))
        return v * 10 * kPxPerMm;
    if (u == QLatin1String("pt"))
        return v * kPxPerPt;
    if (u == QLatin1String("in"))
        return v * 96.0;
    return v;
}

bool near(double a, double b) { return std::fabs(a - b) < 0.05; }

// Millimetres on a 0.05 mm grid: hides the rounding of RTF twips and pixels.
QString mm(double px)
{
    return num(std::round(px / kPxPerMm * 20.0) / 20.0) + QLatin1String("mm");
}

bool sameTabs(const QList<QTextOption::Tab> &a, const QList<QTextOption::Tab> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (!near(a[i].position, b[i].position) || a[i].type != b[i].type)
            return false;
    return true;
}

} // namespace

// ------------------------------------------------------------------ characters

double spacingToEm(int percent)
{
    return percent <= 0 ? 0.0 : (percent - 100) / 200.0;
}

int spacingFromEm(double em)
{
    const int p = qRound(100 + em * 200.0);
    return p == 100 ? 0 : qBound(50, p, 400);
}

CharSpec charSpec(const QTextCharFormat &cf)
{
    CharSpec s;
    if (cf.isImageFormat())
        return s;
    const QVariant fam = cf.property(QTextFormat::FontFamilies);
    if (fam.isValid()) {
        const QStringList l = fam.toStringList();
        if (!l.isEmpty())
            s.family = l.first();
    }
    if (cf.hasProperty(QTextFormat::FontPointSize) && cf.fontPointSize() > 0)
        s.size = cf.fontPointSize();
    if (cf.hasProperty(QTextFormat::FontWeight)) {
        const int w = cf.fontWeight();
        if (w != QFont::Normal && w != QFont::Bold)
            s.weight = w;
    }
    if (cf.hasProperty(QTextFormat::ForegroundBrush) && !cf.hasProperty(props::NoteKind) && !cf.isAnchor()
        && cf.intProperty(props::Revision) <= 0) {
        const QBrush br = cf.foreground();
        if (br.style() != Qt::NoBrush)
            s.color = QColor(br.color().red(), br.color().green(), br.color().blue());
    }
    if (cf.hasProperty(QTextFormat::FontLetterSpacing)
        && cf.fontLetterSpacingType() == QFont::PercentageSpacing) {
        const int p = qRound(cf.fontLetterSpacing());
        if (p > 0 && p != 100)
            s.spacing = p;
    }
    if (cf.hasProperty(QTextFormat::FontCapitalization)) {
        if (cf.fontCapitalization() == QFont::SmallCaps)
            s.caps = 1;
        else if (cf.fontCapitalization() == QFont::AllUppercase)
            s.caps = 2;
    }
    return s;
}

void applyCharSpec(QTextCharFormat &cf, const CharSpec &s)
{
    if (s.family.isEmpty()) {
        cf.clearProperty(QTextFormat::FontFamilies);
        cf.clearProperty(QTextFormat::FontFamily);
    } else {
        cf.setFontFamilies(QStringList{s.family});
    }
    if (s.size > 0)
        cf.setFontPointSize(s.size);
    else
        cf.clearProperty(QTextFormat::FontPointSize);
    if (s.weight > 0)
        cf.setFontWeight(s.weight);
    else if (cf.hasProperty(QTextFormat::FontWeight) && cf.fontWeight() != QFont::Bold)
        cf.clearProperty(QTextFormat::FontWeight);
    if (!cf.hasProperty(props::NoteKind) && !cf.isAnchor() && cf.intProperty(props::Revision) <= 0) {
        if (s.color.isValid())
            cf.setForeground(s.color);
        else
            cf.clearProperty(QTextFormat::ForegroundBrush);
    }
    if (s.spacing > 0 && s.spacing != 100) {
        cf.setFontLetterSpacingType(QFont::PercentageSpacing);
        cf.setFontLetterSpacing(s.spacing);
    } else {
        cf.clearProperty(QTextFormat::FontLetterSpacing);
        cf.clearProperty(QTextFormat::FontLetterSpacingType);
    }
    if (s.caps == 1)
        cf.setFontCapitalization(QFont::SmallCaps);
    else if (s.caps == 2)
        cf.setFontCapitalization(QFont::AllUppercase);
    else
        cf.clearProperty(QTextFormat::FontCapitalization);
}

void mergeCharSpec(QTextCharFormat &cf, const CharSpec &s)
{
    CharSpec cur = charSpec(cf);
    if (!s.family.isEmpty())
        cur.family = s.family;
    if (s.size > 0)
        cur.size = s.size;
    if (s.weight > 0)
        cur.weight = s.weight;
    if (s.color.isValid())
        cur.color = s.color;
    if (s.spacing > 0)
        cur.spacing = s.spacing;
    if (s.caps > 0)
        cur.caps = s.caps;
    applyCharSpec(cf, cur);
}

void clearCharSpec(QTextCharFormat &cf)
{
    applyCharSpec(cf, CharSpec());
}

QString toCss(const CharSpec &s)
{
    QStringList d;
    if (!s.family.isEmpty())
        d << QStringLiteral("font-family:'%1'").arg(cleanFamily(s.family));
    if (s.size > 0)
        d << QStringLiteral("font-size:%1pt").arg(num(s.size));
    if (s.weight > 0)
        d << QStringLiteral("font-weight:%1").arg(s.weight);
    if (s.color.isValid())
        d << QStringLiteral("color:%1").arg(s.color.name());
    if (s.spacing > 0 && s.spacing != 100)
        d << QStringLiteral("letter-spacing:%1em").arg(num(spacingToEm(s.spacing), 3));
    if (s.caps == 1)
        d << QStringLiteral("font-variant:small-caps");
    else if (s.caps == 2)
        d << QStringLiteral("text-transform:uppercase");
    return d.join(QLatin1String("; "));
}

CharSpec charSpecFromCss(const QString &css)
{
    CharSpec s;
    const QStringList decls = css.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &d : decls) {
        const int colon = d.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = d.left(colon).trimmed().toLower();
        QString val = d.mid(colon + 1).trimmed();
        if (key == QLatin1String("font-family")) {
            // first family of a list, without quotes
            val = val.section(QLatin1Char(','), 0, 0);
            s.family = cleanFamily(val);
        } else if (key == QLatin1String("font-size")) {
            bool ok = false;
            const double px = lengthToPx(val, &ok);
            if (ok && px > 0)
                s.size = qBound(1.0, px / kPxPerPt, 999.0);
        } else if (key == QLatin1String("font-weight")) {
            bool ok = false;
            const int w = val.toInt(&ok);
            if (ok && w >= 100 && w <= 900 && w != 400 && w != 700)
                s.weight = w;
        } else if (key == QLatin1String("color")) {
            const QColor c(val);
            if (c.isValid())
                s.color = c;
        } else if (key == QLatin1String("letter-spacing")) {
            if (val.endsWith(QLatin1String("em")))
                s.spacing = spacingFromEm(val.chopped(2).toDouble());
        } else if (key == QLatin1String("font-variant")) {
            if (val.toLower() == QLatin1String("small-caps"))
                s.caps = 1;
        } else if (key == QLatin1String("text-transform")) {
            if (val.toLower() == QLatin1String("uppercase"))
                s.caps = 2;
        }
    }
    return s;
}

// ------------------------------------------------------------------ paragraphs

ParaSpec effectiveParaSpec(const QTextBlockFormat &bf)
{
    ParaSpec s;
    s.bits = AllPara;
    const Qt::Alignment a = bf.alignment();
    s.align = (a & Qt::AlignHCenter) ? Qt::AlignHCenter
              : (a & Qt::AlignRight) ? Qt::AlignRight
              : (a & Qt::AlignJustify) ? Qt::AlignJustify
                                       : Qt::AlignLeft;
    s.left = bf.leftMargin();
    s.right = bf.rightMargin();
    s.first = bf.textIndent();
    s.before = bf.topMargin();
    s.after = bf.bottomMargin();
    s.lineType = bf.lineHeightType();
    s.line = bf.lineHeight();
    if (s.lineType == QTextBlockFormat::SingleHeight || s.line <= 0) {
        s.lineType = QTextBlockFormat::ProportionalHeight;
        s.line = 100;
    }
    s.tabs = bf.tabPositions();
    return s;
}

ParaSpec paraSpec(const QTextBlockFormat &bf)
{
    ParaSpec s = effectiveParaSpec(bf);
    s.bits = bf.intProperty(props::ParaDirect) & AllPara;
    return s;
}

void applyParaSpec(QTextBlockFormat &bf, const ParaSpec &s)
{
    if (!s.bits)
        return;
    if (s.bits & Align)
        bf.setAlignment(s.align);
    if (s.bits & Left)
        bf.setLeftMargin(qMax(0.0, s.left));
    if (s.bits & Right)
        bf.setRightMargin(qMax(0.0, s.right));
    if (s.bits & First)
        bf.setTextIndent(s.first);
    if (s.bits & Before)
        bf.setTopMargin(qMax(0.0, s.before));
    if (s.bits & After)
        bf.setBottomMargin(qMax(0.0, s.after));
    if (s.bits & Line)
        bf.setLineHeight(s.line, s.lineType);
    if (s.bits & Tabs) {
        QList<QTextOption::Tab> tabs = s.tabs;
        std::sort(tabs.begin(), tabs.end(),
                  [](const QTextOption::Tab &a, const QTextOption::Tab &b) { return a.position < b.position; });
        bf.setTabPositions(tabs);
    }
    bf.setProperty(props::ParaDirect, (bf.intProperty(props::ParaDirect) | s.bits) & AllPara);
}

QString toText(const ParaSpec &s)
{
    QStringList p;
    if (s.bits & Align) {
        p << QLatin1String("align=")
                 + ((s.align & Qt::AlignHCenter) ? QLatin1String("center")
                    : (s.align & Qt::AlignRight) ? QLatin1String("right")
                    : (s.align & Qt::AlignJustify) ? QLatin1String("justify")
                                                   : QLatin1String("left"));
    }
    if (s.bits & Left)
        p << QLatin1String("left=") + mm(s.left);
    if (s.bits & Right)
        p << QLatin1String("right=") + mm(s.right);
    if (s.bits & First)
        p << QLatin1String("first=") + mm(s.first);
    if (s.bits & Before)
        p << QStringLiteral("before=%1pt").arg(num(pxToPt(s.before)));
    if (s.bits & After)
        p << QStringLiteral("after=%1pt").arg(num(pxToPt(s.after)));
    if (s.bits & Line) {
        if (s.lineType == QTextBlockFormat::FixedHeight)
            p << QStringLiteral("line=%1pt").arg(num(pxToPt(s.line)));
        else if (s.lineType == QTextBlockFormat::MinimumHeight)
            p << QStringLiteral("line=%1pt+").arg(num(pxToPt(s.line)));
        else
            p << QStringLiteral("line=%1").arg(num(s.line / 100.0, 3));
    }
    if (s.bits & Tabs) {
        QStringList t;
        for (const QTextOption::Tab &tab : s.tabs) {
            QString e = mm(tab.position);
            if (tab.type == QTextOption::RightTab)
                e += QLatin1Char('R');
            else if (tab.type == QTextOption::CenterTab)
                e += QLatin1Char('C');
            else if (tab.type == QTextOption::DelimiterTab)
                e += QLatin1Char('D');
            t << e;
        }
        p << QLatin1String("tabs=") + (t.isEmpty() ? QStringLiteral("none") : t.join(QLatin1Char(',')));
    }
    return p.join(QLatin1Char(' '));
}

ParaSpec paraSpecFromText(const QString &text)
{
    ParaSpec s;
    const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = part.left(eq).toLower();
        const QString val = part.mid(eq + 1);
        bool ok = false;
        if (key == QLatin1String("align")) {
            const QString v = val.toLower();
            s.align = v == QLatin1String("center") ? Qt::AlignHCenter
                      : v == QLatin1String("right") ? Qt::AlignRight
                      : v == QLatin1String("justify") ? Qt::AlignJustify
                                                      : Qt::AlignLeft;
            s.bits |= Align;
        } else if (key == QLatin1String("left")) {
            s.left = lengthToPx(val, &ok);
            if (ok)
                s.bits |= Left;
        } else if (key == QLatin1String("right")) {
            s.right = lengthToPx(val, &ok);
            if (ok)
                s.bits |= Right;
        } else if (key == QLatin1String("first")) {
            s.first = lengthToPx(val, &ok);
            if (ok)
                s.bits |= First;
        } else if (key == QLatin1String("before")) {
            s.before = lengthToPx(val, &ok);
            if (ok)
                s.bits |= Before;
        } else if (key == QLatin1String("after")) {
            s.after = lengthToPx(val, &ok);
            if (ok)
                s.bits |= After;
        } else if (key == QLatin1String("line")) {
            if (val.endsWith(QLatin1Char('+'))) {
                s.line = lengthToPx(val.chopped(1), &ok);
                s.lineType = QTextBlockFormat::MinimumHeight;
            } else if (val.endsWith(QLatin1String("pt")) || val.endsWith(QLatin1String("mm"))
                       || val.endsWith(QLatin1String("px"))) {
                s.line = lengthToPx(val, &ok);
                s.lineType = QTextBlockFormat::FixedHeight;
            } else {
                s.line = val.toDouble(&ok) * 100.0;
                s.lineType = QTextBlockFormat::ProportionalHeight;
                ok = ok && s.line >= 50 && s.line <= 1000;
            }
            if (ok && s.line > 0)
                s.bits |= Line;
        } else if (key == QLatin1String("tabs")) {
            s.bits |= Tabs;
            if (val.toLower() == QLatin1String("none"))
                continue;
            for (QString e : val.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                QTextOption::TabType type = QTextOption::LeftTab;
                if (e.endsWith(QLatin1Char('R')))
                    type = QTextOption::RightTab;
                else if (e.endsWith(QLatin1Char('C')))
                    type = QTextOption::CenterTab;
                else if (e.endsWith(QLatin1Char('D')))
                    type = QTextOption::DelimiterTab;
                if (type != QTextOption::LeftTab)
                    e.chop(1);
                else if (e.endsWith(QLatin1Char('L')))
                    e.chop(1);
                const double px = lengthToPx(e, &ok);
                if (ok && px > 0)
                    s.tabs.append(QTextOption::Tab(px, type, type == QTextOption::DelimiterTab ? QLatin1Char('.') : QChar()));
            }
        }
    }
    return s;
}

QTextBlockFormat withParaSpec(const QTextBlockFormat &cur, const ParaSpec &s, const Formatting &f, bool inList)
{
    QTextBlockFormat bf = cur;
    applyParaSpec(bf, s);
    const QString style = bf.property(props::Style).toString();
    QTextBlockFormat plain = bf;
    plain.clearProperty(props::ParaDirect);
    plain.clearProperty(QTextFormat::BlockAlignment);
    plain.clearProperty(QTextFormat::TabPositions);
    const QTextBlockFormat expected = display::blockFormatFor(f, style, plain);
    const int bits = bf.intProperty(props::ParaDirect) & differences(bf, expected);
    bf.setProperty(props::ParaDirect, bits);
    if (!(bits & Tabs))
        bf.clearProperty(QTextFormat::TabPositions);
    if (!(bits & Align))
        bf.clearProperty(QTextFormat::BlockAlignment);
    QTextBlockFormat out = display::blockFormatFor(f, style, bf);
    if (!bits)
        out.clearProperty(props::ParaDirect);
    if (inList && !(bits & First))
        out.setTextIndent(0);
    return out;
}

int differences(const QTextBlockFormat &a, const QTextBlockFormat &b)
{
    const ParaSpec x = effectiveParaSpec(a), y = effectiveParaSpec(b);
    int bits = 0;
    if (x.align != y.align)
        bits |= Align;
    if (!near(x.left, y.left))
        bits |= Left;
    if (!near(x.right, y.right))
        bits |= Right;
    if (!near(x.first, y.first))
        bits |= First;
    if (!near(x.before, y.before))
        bits |= Before;
    if (!near(x.after, y.after))
        bits |= After;
    if (x.lineType != y.lineType || !near(x.line, y.line))
        bits |= Line;
    if (!sameTabs(x.tabs, y.tabs))
        bits |= Tabs;
    return bits;
}

void markDifferences(QTextDocument *doc, const Formatting &f, int mask)
{
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat bf = b.blockFormat();
        if (b.textList())
            continue; // list geometry belongs to the list
        const QString style = bf.property(props::Style).toString();
        QTextBlockFormat plain;
        const QTextBlockFormat expected = display::blockFormatFor(f, style, plain);
        int bits = (differences(bf, expected) & mask) | (bf.intProperty(props::ParaDirect) & AllPara);
        // a centred paragraph has no first line indent; that is not a choice
        if ((bf.alignment() & Qt::AlignHCenter) && near(bf.textIndent(), 0))
            bits &= ~First;
        if (bits == bf.intProperty(props::ParaDirect))
            continue;
        bf.setProperty(props::ParaDirect, bits);
        QTextCursor bc(b);
        bc.setBlockFormat(bf);
    }
    c.endEditBlock();
}

void removeDirect(QTextDocument *doc, const Formatting &f, bool chars, int paraBits, int from, int to)
{
    if (to < 0)
        to = doc->characterCount() - 1;
    QTextCursor c(doc);
    c.beginEditBlock();
    QTextBlock b = doc->findBlock(from);
    const QTextBlock last = doc->findBlock(to);
    while (b.isValid()) {
        if (paraBits) {
            const QTextBlockFormat bf = b.blockFormat();
            QTextBlockFormat base = bf;
            base.setProperty(props::ParaDirect, bf.intProperty(props::ParaDirect) & AllPara & ~paraBits);
            if (paraBits & Align)
                base.clearProperty(QTextFormat::BlockAlignment);
            QTextBlockFormat nf = display::blockFormatFor(f, bf.property(props::Style).toString(), base);
            if (b.textList())
                nf.setTextIndent(0);
            QTextCursor bc(b);
            bc.setBlockFormat(nf);
        }
        if (chars) {
            struct Piece {
                int s, e;
                QTextCharFormat cf;
            };
            QList<Piece> pieces;
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment fr = it.fragment();
                if (!fr.isValid())
                    continue;
                const int s = qMax(from, fr.position()), e = qMin(to, fr.position() + fr.length());
                if (s >= e)
                    continue;
                QTextCharFormat cf = fr.charFormat();
                if (charSpec(cf).isEmpty())
                    continue;
                clearCharSpec(cf);
                // what the styles of the text say is not set by hand
                if (const Style *ps = styles::find(f, b.blockFormat().stringProperty(props::Style)))
                    styles::applyChars(cf, *ps);
                if (const Style *cs = styles::findChar(f, cf.stringProperty(props::CharStyle)))
                    styles::applyChars(cf, *cs);
                if (cf == fr.charFormat())
                    continue;
                pieces.append({s, e, cf});
            }
            for (const Piece &p : pieces) {
                QTextCursor fc(doc);
                fc.setPosition(p.s);
                fc.setPosition(p.e, QTextCursor::KeepAnchor);
                fc.setCharFormat(p.cf);
            }
        }
        if (b == last)
            break;
        b = b.next();
    }
    c.endEditBlock();
}


// ------------------------------------------------------------------ follow a changed formatting

namespace {

QString familyKey(const QString &f) { return f.trimmed().toLower(); }
bool samePt(double a, double b) { return std::fabs(a - b) < 0.05; }

// a paragraph attribute as a comparable number
qint64 paraKey(const ParaSpec &s, int bit)
{
    switch (bit) {
    case Align: return qint64(s.align);
    case First: return qRound64(s.first * 10);
    case After: return qRound64(s.after * 10);
    case Line: return qint64(s.lineType) * 1000000 + qRound64(s.line * 10);
    default: return 0;
    }
}

constexpr int kFollowBits[] = {Align, First, After, Line};

template<typename K>
K mostUsed(const QHash<K, qint64> &h, bool *found)
{
    K best{};
    qint64 n = -1;
    for (auto it = h.cbegin(); it != h.cend(); ++it)
        if (it.value() > n) {
            n = it.value();
            best = it.key();
        }
    *found = n > 0;
    return best;
}

// The character format of a piece with the styles applied and only `keep` set by hand.
QTextCharFormat rebuilt(const QTextCharFormat &cf, const Formatting &f, const QString &paraStyle, const CharSpec &keep)
{
    QTextCharFormat n = cf;
    clearCharSpec(n);
    if (const Style *ps = styles::find(f, paraStyle))
        styles::applyChars(n, *ps);
    if (const Style *cs = styles::findChar(f, cf.stringProperty(props::CharStyle)))
        styles::applyChars(n, *cs);
    if (!keep.isEmpty())
        mergeCharSpec(n, keep);
    if (keep.size > 0)
        n.clearProperty(QTextFormat::FontSizeAdjustment);
    return n;
}

// Applies `change` to every piece of text and to the format of empty paragraphs.
int changeChars(QTextDocument *doc, const std::function<bool(const QTextBlock &, QTextCharFormat &)> &change)
{
    struct Piece {
        int s, e;
        QTextCharFormat cf;
    };
    int n = 0;
    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QList<Piece> pieces;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.isValid() || fr.charFormat().isImageFormat())
                continue;
            QTextCharFormat cf = fr.charFormat();
            if (change(b, cf) && cf != fr.charFormat())
                pieces.append({fr.position(), fr.position() + fr.length(), cf});
        }
        for (const Piece &p : pieces) {
            QTextCursor fc(doc);
            fc.setPosition(p.s);
            fc.setPosition(p.e, QTextCursor::KeepAnchor);
            fc.setCharFormat(p.cf);
        }
        n += pieces.size();
        QTextCharFormat bcf = b.charFormat();
        if (change(b, bcf) && bcf != b.charFormat()) {
            QTextCursor bc(b);
            bc.setBlockCharFormat(bcf);
            if (b.length() <= 1)
                ++n;
        }
    }
    edit.endEditBlock();
    return n;
}

} // namespace

int changedCharMembers(const Formatting &a, const Formatting &b)
{
    int m = 0;
    if (familyKey(display::resolvedFontFamily(a)) != familyKey(display::resolvedFontFamily(b)))
        m |= Family;
    if (!samePt(a.fontSize, b.fontSize))
        m |= Size;
    return m;
}

int changedParaBits(const Formatting &a, const Formatting &b)
{
    int bits = 0;
    if (!near(a.firstLineIndent, b.firstLineIndent))
        bits |= First;
    if (!near(a.lineSpacing * 100, b.lineSpacing * 100))
        bits |= Line;
    if (!near(a.paragraphSpacing, b.paragraphSpacing))
        bits |= After;
    if (a.justify != b.justify)
        bits |= Align;
    return bits;
}

int followProject(QTextDocument *doc, const Formatting &oldF, const Formatting &newF, int charMembers, int paraBits,
                  bool ownToo)
{
    int changed = 0;
    charMembers &= Family | Size;
    paraBits &= Align | First | After | Line;
    if (paraBits) {
        // the values of the main text: paragraphs without a style and list, weighted by their length
        QHash<int, QHash<qint64, qint64>> use;
        if (ownToo)
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                const QTextBlockFormat bf = b.blockFormat();
                if (b.textList() || !bf.stringProperty(props::Style).isEmpty() || b.length() <= 1)
                    continue;
                const ParaSpec s = effectiveParaSpec(bf);
                for (int bit : kFollowBits)
                    if (paraBits & bit)
                        use[bit][paraKey(s, bit)] += b.length();
            }
        QHash<int, qint64> body;
        for (int bit : kFollowBits) {
            bool found = false;
            const qint64 k = mostUsed(use.value(bit), &found);
            if (found)
                body.insert(bit, k);
        }
        QTextCursor edit(doc);
        edit.beginEditBlock();
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            const QTextBlockFormat bf = b.blockFormat();
            const QString style = bf.stringProperty(props::Style);
            const int before = bf.intProperty(props::ParaDirect) & AllPara;
            int bits = before;
            if (!b.textList()) {
                // what differs from the old formatting is the paragraph's own from now on
                const QTextBlockFormat oldExpected = display::blockFormatFor(oldF, style, QTextBlockFormat());
                bits |= differences(bf, oldExpected);
                if ((bf.alignment() & Qt::AlignHCenter) && near(bf.textIndent(), 0))
                    bits &= ~First;
                const ParaSpec cur = effectiveParaSpec(bf);
                const ParaSpec was = effectiveParaSpec(oldExpected);
                const ParaSpec now = effectiveParaSpec(display::blockFormatFor(newF, style, QTextBlockFormat()));
                for (int bit : kFollowBits) {
                    if (!(paraBits & bit) || !(bits & bit))
                        continue;
                    const qint64 v = paraKey(cur, bit);
                    if (v == paraKey(was, bit) || (!(before & bit) && v == paraKey(now, bit))
                        || (ownToo && style.isEmpty() && body.contains(bit) && body.value(bit) == v))
                        bits &= ~bit;
                }
            }
            QTextBlockFormat base = bf;
            base.setProperty(props::ParaDirect, bits);
            QTextBlockFormat nf = display::blockFormatFor(newF, style, base);
            if (b.textList() && !(bits & First))
                nf.setTextIndent(0);
            if (!bits)
                nf.clearProperty(props::ParaDirect);
            else
                nf.setProperty(props::ParaDirect, bits);
            if (nf != bf) {
                QTextCursor bc(b);
                bc.setBlockFormat(nf);
                ++changed;
            }
        }
        edit.endEditBlock();
    }
    if (charMembers) {
        const QString oldFam = familyKey(display::resolvedFontFamily(oldF));
        const QString newFam = familyKey(display::resolvedFontFamily(newF));
        QString bodyFam;
        double bodySize = 0;
        if (ownToo) {
            QHash<QString, qint64> fams;
            QHash<qint64, qint64> sizes;
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                if (b.textList() || !b.blockFormat().stringProperty(props::Style).isEmpty())
                    continue;
                for (auto it = b.begin(); !it.atEnd(); ++it) {
                    const QTextFragment fr = it.fragment();
                    if (!fr.isValid())
                        continue;
                    const QTextCharFormat cf = fr.charFormat();
                    if (cf.isImageFormat() || cf.hasProperty(props::CharStyle) || cf.hasProperty(props::NoteKind))
                        continue;
                    const CharSpec sp = charSpec(cf);
                    fams[familyKey(sp.family)] += fr.length();
                    sizes[sp.size > 0 ? qRound64(sp.size * 10) : 0] += fr.length();
                }
            }
            bool found = false;
            bodyFam = mostUsed(fams, &found);
            const qint64 sz = mostUsed(sizes, &found);
            bodySize = found ? sz / 10.0 : 0;
        }
        changed += changeChars(doc, [&](const QTextBlock &b, QTextCharFormat &cf) {
            CharSpec sp = charSpec(cf);
            bool drop = false;
            if ((charMembers & Family) && !sp.family.isEmpty()) {
                const QString k = familyKey(sp.family);
                if (k == oldFam || k == newFam || (ownToo && !bodyFam.isEmpty() && k == bodyFam)) {
                    sp.family.clear();
                    drop = true;
                }
            }
            if ((charMembers & Size) && sp.size > 0) {
                if (samePt(sp.size, oldF.fontSize) || samePt(sp.size, newF.fontSize)
                    || (ownToo && bodySize > 0 && samePt(sp.size, bodySize))) {
                    sp.size = 0;
                    drop = true;
                }
            }
            if (!drop)
                return false;
            // what the styles bring along comes back with them
            cf = rebuilt(cf, newF, b.blockFormat().stringProperty(props::Style), sp);
            return true;
        });
    }
    return changed;
}

int followStyle(QTextDocument *doc, const Formatting &f, const QString &style, bool character, int charMembers,
                const CharSpec &oldValues, const CharSpec &newValues)
{
    charMembers &= Family | Size;
    if (!charMembers || style.isEmpty())
        return 0;
    return changeChars(doc, [&](const QTextBlock &b, QTextCharFormat &cf) {
        const QString paraStyle = b.blockFormat().stringProperty(props::Style);
        if (character ? cf.stringProperty(props::CharStyle) != style : paraStyle != style)
            return false;
        CharSpec sp = charSpec(cf);
        bool drop = false;
        if ((charMembers & Family) && !sp.family.isEmpty()) {
            const QString k = familyKey(sp.family);
            if (k == familyKey(oldValues.family) || k == familyKey(newValues.family)) {
                sp.family.clear();
                drop = true;
            }
        }
        if ((charMembers & Size) && sp.size > 0
            && ((oldValues.size > 0 && samePt(sp.size, oldValues.size)) || (newValues.size > 0 && samePt(sp.size, newValues.size)))) {
            sp.size = 0;
            drop = true;
        }
        if (!drop)
            return false;
        cf = rebuilt(cf, f, paraStyle, sp);
        return true;
    });
}

} // namespace direct
} // namespace qb
