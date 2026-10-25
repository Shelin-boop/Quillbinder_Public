#include "text/Styles.h"

#include "text/Script.h"

#include "text/DisplayFormat.h"
#include "text/TextProps.h"

#include <QDomDocument>
#include <QFile>
#include <QObject>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <cmath>
#include <functional>

namespace qb {

namespace {

bool nearPx(double a, double b) { return std::fabs(a - b) < 0.2; }

bool sameTabs(const QList<QTextOption::Tab> &a, const QList<QTextOption::Tab> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (!nearPx(a[i].position, b[i].position) || a[i].type != b[i].type)
            return false;
    return true;
}

bool samePara(const direct::ParaSpec &a, const direct::ParaSpec &b)
{
    if (a.bits != b.bits)
        return false;
    using namespace direct;
    if ((a.bits & Align) && a.align != b.align)
        return false;
    if ((a.bits & Left) && !nearPx(a.left, b.left))
        return false;
    if ((a.bits & Right) && !nearPx(a.right, b.right))
        return false;
    if ((a.bits & First) && !nearPx(a.first, b.first))
        return false;
    if ((a.bits & Before) && !nearPx(a.before, b.before))
        return false;
    if ((a.bits & After) && !nearPx(a.after, b.after))
        return false;
    if ((a.bits & Line) && (a.lineType != b.lineType || !nearPx(a.line, b.line)))
        return false;
    if ((a.bits & Tabs) && !sameTabs(a.tabs, b.tabs))
        return false;
    return true;
}

StyleSheet makeDefaults()
{
    using namespace direct;
    StyleSheet sheet;
    auto para = [](Style &s, int bits) { s.para.bits |= bits; };
    auto heading = [&](int level, int adjust, bool bold, bool italic, double before, double after, bool keep) {
        Style s;
        s.name = QStringLiteral("Heading %1").arg(level);
        s.builtin = true;
        s.headingLevel = level;
        s.sizeAdjust = adjust;
        s.bold = bold;
        s.italic = italic;
        s.keepWithNext = keep;
        s.next = styles::noStyle();
        para(s, First | Line);
        s.para.first = 0;
        s.para.line = 100;
        s.para.lineType = QTextBlockFormat::ProportionalHeight;
        if (before > 0) {
            para(s, Before);
            s.para.before = before;
        }
        if (after >= 0) {
            para(s, After);
            s.para.after = after;
        }
        sheet.styles.append(s);
    };
    heading(1, 2, true, false, 18, 8, true);
    heading(2, 1, true, false, 14, 6, true);
    heading(3, 0, true, true, 10, 4, true);
    heading(4, 0, true, false, 8, -1, true);
    heading(5, 0, true, false, 0, -1, false);
    heading(6, 0, false, true, 0, -1, false);

    Style s;
    s = {};
    s.name = QStringLiteral("Title");
    s.builtin = true;
    s.sizeAdjust = 3;
    s.bold = true;
    s.next = styles::noStyle();
    s.para.bits = Align | First | Before | After | Line;
    s.para.align = Qt::AlignHCenter;
    s.para.before = 24;
    s.para.after = 12;
    s.para.line = 100;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Subtitle");
    s.builtin = true;
    s.sizeAdjust = 1;
    s.italic = true;
    s.next = styles::noStyle();
    s.para.bits = Align | First | After;
    s.para.align = Qt::AlignHCenter;
    s.para.after = 12;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Block Quote");
    s.builtin = true;
    s.para.bits = Left | Right | First | After;
    s.para.left = 40;
    s.para.right = 40;
    s.para.after = 8;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Centered");
    s.builtin = true;
    s.para.bits = Align | First;
    s.para.align = Qt::AlignHCenter;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Caption");
    s.builtin = true;
    s.italic = true;
    s.sizeAdjust = -1;
    s.next = styles::noStyle();
    s.para.bits = Align | First;
    s.para.align = Qt::AlignHCenter;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Attribution");
    s.builtin = true;
    s.next = styles::noStyle();
    s.para.bits = Align | First;
    s.para.align = Qt::AlignRight;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Code Block");
    s.builtin = true;
    s.chars.family = QStringLiteral("monospace");
    s.sizeAdjust = -1;
    s.para.bits = Align | Left | First | After | Line;
    s.para.align = Qt::AlignLeft;
    s.para.left = 24;
    s.para.after = 0;
    s.para.line = 100;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Emphasis");
    s.builtin = true;
    s.character = true;
    s.italic = true;
    sheet.styles.append(s);

    s = {};
    s.name = QStringLiteral("Code Span");
    s.builtin = true;
    s.character = true;
    s.chars.family = QStringLiteral("monospace");
    sheet.styles.append(s);
    return sheet;
}

int alignIndex(Qt::Alignment a)
{
    return (a & Qt::AlignHCenter) ? 1 : (a & Qt::AlignRight) ? 2 : (a & Qt::AlignJustify) ? 3 : 0;
}

// The char format of text: what was set by hand (base) under the formatting
// of its paragraph style and its character style.
QTextCharFormat compose(const QTextCharFormat &base, const Style *para, const Style *chr)
{
    QTextCharFormat out = base;
    const direct::CharSpec own = direct::charSpec(base);
    if (para)
        styles::applyChars(out, *para);
    if (chr) {
        styles::applyChars(out, *chr);
        out.setProperty(props::CharStyle, chr->name);
    } else {
        out.clearProperty(props::CharStyle);
    }
    if (!own.isEmpty()) {
        direct::mergeCharSpec(out, own); // set by hand wins
        if (own.size > 0)
            out.clearProperty(QTextFormat::FontSizeAdjustment);
    }
    return out;
}

void setFragments(QTextDocument *doc, const QTextBlock &b, int from, int to,
                  const std::function<bool(QTextCharFormat &)> &change)
{
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
        if (cf.isImageFormat())
            continue;
        if (change(cf))
            pieces.append({s, e, cf});
    }
    for (const Piece &p : pieces) {
        QTextCursor fc(doc);
        fc.setPosition(p.s);
        fc.setPosition(p.e, QTextCursor::KeepAnchor);
        fc.setCharFormat(p.cf);
    }
}

} // namespace

bool Style::sameFormatting(const Style &o) const
{
    return bold == o.bold && italic == o.italic && underline == o.underline && strike == o.strike
           && sizeAdjust == o.sizeAdjust && chars == o.chars && samePara(para, o.para);
}

bool Style::operator==(const Style &o) const
{
    return name == o.name && character == o.character && builtin == o.builtin && headingLevel == o.headingLevel
           && keepWithNext == o.keepWithNext && next == o.next && sameFormatting(o);
}

const Style *StyleSheet::find(const QString &name) const
{
    if (name.isEmpty())
        return nullptr;
    for (const Style &s : styles)
        if (s.name.compare(name, Qt::CaseInsensitive) == 0)
            return &s;
    const auto it = aliases.constFind(name);
    if (it != aliases.constEnd())
        for (const Style &s : styles)
            if (s.name == it.value())
                return &s;
    return nullptr;
}

int StyleSheet::indexOf(const QString &name) const
{
    for (int i = 0; i < styles.size(); ++i)
        if (styles[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QString StyleSheet::uniqueName(const QString &wanted) const
{
    QString base = wanted.trimmed();
    if (base.isEmpty())
        base = QObject::tr("New Style");
    if (indexOf(base) < 0)
        return base;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(n);
        if (indexOf(candidate) < 0)
            return candidate;
    }
}

namespace styles {

const StyleSheet &defaults()
{
    static const StyleSheet sheet = makeDefaults();
    return sheet;
}

const StyleSheet &sheet(const Formatting &f)
{
    return f.styles ? *f.styles : defaults();
}

Formatting withSheet(Formatting f, const StyleSheet &s)
{
    f.styles = QSharedPointer<const StyleSheet>(new StyleSheet(s));
    return f;
}

const Style *find(const Formatting &f, const QString &name)
{
    // the elements of script documents are paragraph styles of the script format
    if (script::isScriptStyle(name))
        return script::data(f).sheet.find(name);
    const Style *s = sheet(f).find(name);
    return s && !s->character ? s : nullptr;
}

const Style *findChar(const Formatting &f, const QString &name)
{
    const Style *s = sheet(f).find(name);
    return s && s->character ? s : nullptr;
}

const Style *builtinDefault(const QString &name)
{
    const StyleSheet &d = defaults();
    const int i = d.indexOf(name);
    return i >= 0 ? &d.styles[i] : nullptr;
}

QStringList names(const Formatting &f)
{
    QStringList n;
    for (const Style &s : sheet(f).styles)
        if (!s.character)
            n << s.name;
    return n;
}

QStringList charNames(const Formatting &f)
{
    QStringList n;
    for (const Style &s : sheet(f).styles)
        if (s.character)
            n << s.name;
    return n;
}

QString forHeadingLevel(int level)
{
    level = qBound(1, level, 6);
    return QStringLiteral("Heading %1").arg(level);
}

bool isHeadingName(const QString &name)
{
    return name.size() == 9 && name.startsWith(QLatin1String("Heading "), Qt::CaseInsensitive)
           && name.at(8) >= QLatin1Char('1') && name.at(8) <= QLatin1Char('6');
}

bool isFixedName(const QString &name)
{
    if (isHeadingName(name))
        return true;
    for (const char *n : {"Title", "Subtitle", "Block Quote", "Centered", "Caption"})
        if (name.compare(QLatin1String(n), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString marker(const Style &s)
{
    if (s.character)
        return QStringLiteral("a ");
    return s.hasCharFormatting() ? QStringLiteral("¶a ") : QStringLiteral("¶ ");
}

bool validName(const QString &name)
{
    if (name.isEmpty() || name.size() > 60 || name != name.trimmed())
        return false;
    if (name == noStyle() || name.compare(QLatin1String("No Style"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("Normal"), Qt::CaseInsensitive) == 0)
        return false;
    for (const QChar c : name)
        if (c == QLatin1Char(':') || c == QLatin1Char('"') || c == QLatin1Char('<') || c == QLatin1Char('>')
            || c == QLatin1Char('\\') || c == QLatin1Char('{') || c == QLatin1Char('}') || c == QLatin1Char(';')
            || c.unicode() < 32)
            return false;
    return true;
}

double adjustedSize(double baseSize, int sizeAdjust)
{
    static const double factors[] = {0.7, 0.8, 1.0, 1.2, 1.5, 2.0, 2.4};
    return baseSize * factors[qBound(0, sizeAdjust + 2, 6)];
}

void applyChars(QTextCharFormat &cf, const Style &s)
{
    if (s.bold)
        cf.setFontWeight(QFont::Bold);
    if (s.italic)
        cf.setFontItalic(true);
    if (s.underline && !cf.isAnchor())
        cf.setFontUnderline(true);
    if (s.strike)
        cf.setFontStrikeOut(true);
    if (!s.chars.isEmpty())
        direct::mergeCharSpec(cf, s.chars);
    if (s.chars.size > 0)
        cf.clearProperty(QTextFormat::FontSizeAdjustment);
    else if (s.sizeAdjust != 0)
        cf.setProperty(QTextFormat::FontSizeAdjustment, s.sizeAdjust);
}

void stripChars(QTextCharFormat &cf, const Style &s)
{
    if (s.bold && cf.hasProperty(QTextFormat::FontWeight) && cf.fontWeight() == QFont::Bold)
        cf.clearProperty(QTextFormat::FontWeight);
    if (s.italic && cf.fontItalic())
        cf.clearProperty(QTextFormat::FontItalic);
    if (s.underline && cf.fontUnderline() && !cf.isAnchor()) {
        cf.clearProperty(QTextFormat::TextUnderlineStyle);
        cf.clearProperty(QTextFormat::FontUnderline);
    }
    if (s.strike && cf.fontStrikeOut())
        cf.clearProperty(QTextFormat::FontStrikeOut);
    if (s.sizeAdjust != 0 && cf.hasProperty(QTextFormat::FontSizeAdjustment)
        && cf.intProperty(QTextFormat::FontSizeAdjustment) == s.sizeAdjust)
        cf.clearProperty(QTextFormat::FontSizeAdjustment);
    if (!s.chars.isEmpty()) {
        direct::CharSpec cur = direct::charSpec(cf);
        const direct::CharSpec before = cur;
        if (!s.chars.family.isEmpty() && cur.family.compare(s.chars.family, Qt::CaseInsensitive) == 0)
            cur.family.clear();
        if (s.chars.size > 0 && std::fabs(cur.size - s.chars.size) < 0.01)
            cur.size = 0;
        if (s.chars.weight > 0 && cur.weight == s.chars.weight)
            cur.weight = 0;
        if (s.chars.color.isValid() && cur.color == s.chars.color)
            cur.color = QColor();
        if (s.chars.spacing > 0 && cur.spacing == s.chars.spacing)
            cur.spacing = 0;
        if (s.chars.caps > 0 && cur.caps == s.chars.caps)
            cur.caps = 0;
        if (cur != before) {
            const bool bold = cf.hasProperty(QTextFormat::FontWeight) && cf.fontWeight() == QFont::Bold;
            direct::applyCharSpec(cf, cur);
            if (bold)
                cf.setFontWeight(QFont::Bold);
        }
    }
}

QTextCharFormat withoutStyles(const QTextCharFormat &cf, const Style *paragraph, const Formatting &f)
{
    QTextCharFormat out = cf;
    if (cf.hasProperty(props::CharStyle)) {
        if (const Style *cs = findChar(f, cf.stringProperty(props::CharStyle)))
            stripChars(out, *cs);
    }
    if (paragraph)
        stripChars(out, *paragraph);
    return out;
}

Style capture(const QString &name, const QTextBlockFormat &bf, const QTextCharFormat &cf, const Formatting &f,
              const CaptureOptions &opt)
{
    Style s;
    s.name = name;
    s.character = opt.character;
    if (!opt.character) {
        QTextBlockFormat plain = display::normalBlockFormat(f);
        s.para = direct::effectiveParaSpec(bf);
        s.para.bits = direct::differences(bf, plain);
        if (!(s.para.bits & direct::Tabs))
            s.para.tabs.clear();
    }
    if (opt.character || opt.withChars) {
        s.bold = cf.fontWeight() >= QFont::DemiBold;
        s.italic = cf.fontItalic();
        s.underline = cf.fontUnderline() && !cf.isAnchor();
        s.strike = cf.fontStrikeOut();
        s.chars = direct::charSpec(cf);
        if (s.chars.weight > 0)
            s.bold = false;
        if (!opt.withFamily)
            s.chars.family.clear();
        if (!opt.withSize)
            s.chars.size = 0;
        else if (s.chars.size <= 0 && cf.hasProperty(QTextFormat::FontSizeAdjustment))
            s.sizeAdjust = cf.intProperty(QTextFormat::FontSizeAdjustment);
    }
    return s;
}

void applyCharacterStyle(QTextDocument *doc, int from, int to, const QString &style, const Formatting &f)
{
    if (!doc || from >= to)
        return;
    const Style *chr = findChar(f, style);
    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (QTextBlock b = doc->findBlock(from); b.isValid() && b.position() < to; b = b.next()) {
        const Style *para = find(f, b.blockFormat().stringProperty(props::Style));
        setFragments(doc, b, from, to, [&](QTextCharFormat &cf) {
            const QTextCharFormat out = compose(withoutStyles(cf, para, f), para, chr);
            if (out == cf)
                return false;
            cf = out;
            return true;
        });
    }
    edit.endEditBlock();
}

QString charStyleAt(const QTextDocument *doc, int position)
{
    if (!doc)
        return QString();
    QTextCursor c(const_cast<QTextDocument *>(doc));
    c.setPosition(qBound(0, position, qMax(0, doc->characterCount() - 1)));
    return c.charFormat().stringProperty(props::CharStyle);
}

bool restyle(QTextDocument *doc, const Formatting &oldF, const Formatting &newF, const Changes &changes)
{
    bool namesChanged = false;
    QTextCursor edit(doc);
    edit.beginEditBlock();
    auto newName = [&](const QString &n) { return changes.renamed.value(n, n); };
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        const QString name = bf.stringProperty(props::Style);
        const Style *oldP = find(oldF, name);
        const Style *newP = name.isEmpty() ? nullptr : find(newF, newName(name));
        const bool paraRemoved = oldP && !newP;
        if (!name.isEmpty() && (!newP || newP->name != name))
            namesChanged = true;

        auto convert = [&](QTextCharFormat &cf) {
            const QString cn = cf.stringProperty(props::CharStyle);
            if (!oldP && !newP && cn.isEmpty())
                return false;
            const Style *oldC = findChar(oldF, cn);
            const Style *newC = cn.isEmpty() ? nullptr : findChar(newF, newName(cn));
            if (!cn.isEmpty() && (!newC || newC->name != cn))
                namesChanged = true;
            QTextCharFormat base = cf;
            // a style that no longer exists leaves its look behind
            if (oldC && newC)
                stripChars(base, *oldC);
            if (oldP && !paraRemoved)
                stripChars(base, *oldP);
            if (oldC && !newC && oldC->sizeAdjust != 0 && base.hasProperty(QTextFormat::FontSizeAdjustment)) {
                base.setFontPointSize(adjustedSize(oldF.fontSize, base.intProperty(QTextFormat::FontSizeAdjustment)));
                base.clearProperty(QTextFormat::FontSizeAdjustment);
            }
            if (paraRemoved && base.hasProperty(QTextFormat::FontSizeAdjustment)) {
                base.setFontPointSize(adjustedSize(oldF.fontSize, base.intProperty(QTextFormat::FontSizeAdjustment)));
                base.clearProperty(QTextFormat::FontSizeAdjustment);
            }
            const QTextCharFormat out = compose(base, newP, newC);
            if (out == cf)
                return false;
            cf = out;
            return true;
        };
        setFragments(doc, b, b.position(), b.position() + b.length(), convert);
        QTextCharFormat bcf = b.charFormat();
        if (convert(bcf)) {
            QTextCursor bc(b);
            bc.setBlockCharFormat(bcf);
        }

        if (!oldP && !newP)
            continue;
        QTextBlockFormat base = bf;
        if (paraRemoved && !b.textList()) {
            const int bits = (bf.intProperty(props::ParaDirect) | oldP->para.bits) & direct::AllPara;
            base.setProperty(props::ParaDirect, bits);
        }
        QTextBlockFormat nbf = display::blockFormatFor(newF, newP ? newP->name : QString(), base);
        if (b.textList() && !(nbf.intProperty(props::ParaDirect) & direct::First))
            nbf.setTextIndent(0);
        if (nbf != bf) {
            QTextCursor bc(b);
            bc.setBlockFormat(nbf);
        }
    }
    edit.endEditBlock();
    return namesChanged;
}

QStringList usedStyles(const QTextDocument *doc)
{
    QSet<QString> seen;
    QStringList out;
    auto add = [&](const QString &n) {
        if (!n.isEmpty() && !seen.contains(n)) {
            seen.insert(n);
            out << n;
        }
    };
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        add(b.blockFormat().stringProperty(props::Style));
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().isValid())
                add(it.fragment().charFormat().stringProperty(props::CharStyle));
    }
    return out;
}

// ------------------------------------------------------------------ storage

QDomElement toXml(QDomDocument &dom, const StyleSheet &sheet)
{
    QDomElement root = dom.createElement(QStringLiteral("Styles"));
    for (const Style &s : sheet.styles) {
        QDomElement e = dom.createElement(QStringLiteral("Style"));
        e.setAttribute(QStringLiteral("name"), s.name);
        e.setAttribute(QStringLiteral("kind"), s.character ? QStringLiteral("character") : QStringLiteral("paragraph"));
        if (s.builtin)
            e.setAttribute(QStringLiteral("builtin"), 1);
        if (s.headingLevel)
            e.setAttribute(QStringLiteral("heading"), s.headingLevel);
        if (s.keepWithNext)
            e.setAttribute(QStringLiteral("keepWithNext"), 1);
        if (!s.next.isEmpty())
            e.setAttribute(QStringLiteral("next"), s.next);
        if (s.bold)
            e.setAttribute(QStringLiteral("bold"), 1);
        if (s.italic)
            e.setAttribute(QStringLiteral("italic"), 1);
        if (s.underline)
            e.setAttribute(QStringLiteral("underline"), 1);
        if (s.strike)
            e.setAttribute(QStringLiteral("strike"), 1);
        if (s.sizeAdjust)
            e.setAttribute(QStringLiteral("sizeStep"), s.sizeAdjust);
        if (!s.chars.isEmpty())
            e.setAttribute(QStringLiteral("font"), direct::toCss(s.chars));
        if (s.para.bits)
            e.setAttribute(QStringLiteral("paragraph"), direct::toText(s.para));
        root.appendChild(e);
    }
    QStringList keys = sheet.aliases.keys();
    keys.sort();
    for (const QString &k : keys) {
        QDomElement e = dom.createElement(QStringLiteral("Alias"));
        e.setAttribute(QStringLiteral("from"), k);
        e.setAttribute(QStringLiteral("to"), sheet.aliases.value(k));
        root.appendChild(e);
    }
    return root;
}

StyleSheet fromXml(const QDomElement &el)
{
    StyleSheet sheet;
    for (QDomElement e = el.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        if (e.tagName() == QLatin1String("Alias")) {
            const QString from = e.attribute(QStringLiteral("from")), to = e.attribute(QStringLiteral("to"));
            if (!from.isEmpty() && !to.isEmpty())
                sheet.aliases.insert(from, to);
            continue;
        }
        if (e.tagName() != QLatin1String("Style"))
            continue;
        Style s;
        s.name = e.attribute(QStringLiteral("name")).trimmed();
        if (!validName(s.name) || sheet.indexOf(s.name) >= 0)
            continue; // names the text files cannot carry (file changed by hand)
        s.character = e.attribute(QStringLiteral("kind")) == QLatin1String("character");
        s.builtin = e.attribute(QStringLiteral("builtin")) == QLatin1String("1");
        s.headingLevel = qBound(0, e.attribute(QStringLiteral("heading")).toInt(), 6);
        s.keepWithNext = e.attribute(QStringLiteral("keepWithNext")) == QLatin1String("1");
        s.next = e.attribute(QStringLiteral("next"));
        s.bold = e.attribute(QStringLiteral("bold")) == QLatin1String("1");
        s.italic = e.attribute(QStringLiteral("italic")) == QLatin1String("1");
        s.underline = e.attribute(QStringLiteral("underline")) == QLatin1String("1");
        s.strike = e.attribute(QStringLiteral("strike")) == QLatin1String("1");
        s.sizeAdjust = qBound(-2, e.attribute(QStringLiteral("sizeStep")).toInt(), 4);
        s.chars = direct::charSpecFromCss(e.attribute(QStringLiteral("font")));
        if (!s.character)
            s.para = direct::paraSpecFromText(e.attribute(QStringLiteral("paragraph")));
        if (s.character) {
            s.headingLevel = 0;
            s.next.clear();
        }
        sheet.styles.append(s);
    }
    // the heading styles always exist: the Markdown dialect and compile rely on them
    for (int level = 1; level <= 6; ++level) {
        const QString n = forHeadingLevel(level);
        const int i = sheet.indexOf(n);
        if (i < 0)
            sheet.styles.insert(qMin(level - 1, int(sheet.styles.size())), *builtinDefault(n));
        else {
            sheet.styles[i].headingLevel = level;
            sheet.styles[i].character = false;
        }
    }
    return sheet;
}

QString toXmlString(const StyleSheet &sheet)
{
    QDomDocument dom;
    dom.appendChild(toXml(dom, sheet));
    return dom.toString(1);
}

bool fromXmlString(const QString &xml, StyleSheet *sheet)
{
    QDomDocument dom;
    if (xml.trimmed().isEmpty() || !dom.setContent(xml))
        return false;
    const QDomElement root = dom.documentElement();
    if (root.tagName() != QLatin1String("Styles"))
        return false;
    *sheet = fromXml(root);
    return true;
}

bool readFromProjectFile(const QString &path, StyleSheet *sheet, QString *error)
{
    QFile f(path);
    QDomDocument dom;
    if (!f.open(QIODevice::ReadOnly) || !dom.setContent(&f)) {
        if (error)
            *error = QObject::tr("Cannot read the project file %1.").arg(path);
        return false;
    }
    const QDomElement el = dom.documentElement().firstChildElement(QStringLiteral("Styles"));
    *sheet = el.isNull() ? defaults() : fromXml(el);
    return true;
}

} // namespace styles
} // namespace qb
