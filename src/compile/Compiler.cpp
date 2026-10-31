#include "compile/Compiler.h"

#include "text/Script.h"

#include <QFontInfo>
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Objects.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QDate>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QUrl>

namespace qb {
namespace compile {

// ---------------------------------------------------------------- placeholders

QString Placeholders::numberToWords(int n)
{
    static const char *ones[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
                                 "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
                                 "seventeen", "eighteen", "nineteen"};
    static const char *tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    if (n < 0)
        return QStringLiteral("minus ") + numberToWords(-n);
    if (n < 20)
        return QString::fromLatin1(ones[n]);
    if (n < 100)
        return QString::fromLatin1(tens[n / 10]) + (n % 10 ? QLatin1Char('-') + QString::fromLatin1(ones[n % 10]) : QString());
    if (n < 1000)
        return QString::fromLatin1(ones[n / 100]) + QStringLiteral(" hundred")
               + (n % 100 ? QStringLiteral(" and ") + numberToWords(n % 100) : QString());
    if (n < 1000000)
        return numberToWords(n / 1000) + QStringLiteral(" thousand")
               + (n % 1000 ? (n % 1000 < 100 ? QStringLiteral(" and ") : QStringLiteral(" ")) + numberToWords(n % 1000)
                           : QString());
    return QString::number(n);
}

QString Placeholders::toRoman(int n)
{
    if (n <= 0 || n > 3999)
        return QString::number(n);
    static const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *syms[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    QString out;
    for (int i = 0; i < 13; ++i)
        while (n >= values[i]) {
            out += QLatin1String(syms[i]);
            n -= values[i];
        }
    return out;
}

static QString capitalizeWords(const QString &s)
{
    QString out = s;
    bool start = true;
    for (int i = 0; i < out.size(); ++i) {
        if (out[i].isLetter()) {
            if (start)
                out[i] = out[i].toUpper();
            start = false;
        } else {
            start = out[i] == QLatin1Char(' ') || out[i] == QLatin1Char('-');
        }
    }
    return out;
}

namespace {
// File > Options > General > Author Information (0.21): street, city, state, zip, country, phone, email
QMap<QString, QString> &contact()
{
    static QMap<QString, QString> c;
    return c;
}
} // namespace

void setAuthorContact(const QMap<QString, QString> &c) { contact() = c; }
QMap<QString, QString> authorContact() { return contact(); }

QString Placeholders::apply(const QString &s)
{
    if (!s.contains(QLatin1String("<$")))
        return s;
    static const QRegularExpression re(QStringLiteral("<\\$([A-Za-z0-9]+)(?::([^>]*))?>"));
    QString out;
    int last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        QString tag = m.captured(1);
        const QString arg = m.captured(2);
        // word tags in any case (Scrivener writes <$PROJECTTITLE>); the
        // counters keep theirs (W and w, R and r differ)
        static const QStringList caseFree = {QStringLiteral("title"), QStringLiteral("projecttitle"),
                                             QStringLiteral("shorttitle"), QStringLiteral("author"),
                                             QStringLiteral("fullname"), QStringLiteral("surname"),
                                             QStringLiteral("lastname"), QStringLiteral("forename"),
                                             QStringLiteral("firstname"), QStringLiteral("date"), QStringLiteral("year"),
                                             QStringLiteral("street"), QStringLiteral("city"), QStringLiteral("state"),
                                             QStringLiteral("zip"), QStringLiteral("country"), QStringLiteral("phone"),
                                             QStringLiteral("email"), QStringLiteral("address")};
        if (tag.size() > 2 && caseFree.contains(tag.toLower()))
            tag = tag.toLower();
        auto next = [&]() { return ++m_counters[arg]; };
        if (tag == QLatin1String("n")) {
            out += QString::number(next());
        } else if (tag == QLatin1String("w")) {
            out += numberToWords(next());
        } else if (tag == QLatin1String("W")) {
            out += capitalizeWords(numberToWords(next()));
        } else if (tag == QLatin1String("WW")) {
            out += numberToWords(next()).toUpper();
        } else if (tag == QLatin1String("R")) {
            out += toRoman(next());
        } else if (tag == QLatin1String("r")) {
            out += toRoman(next()).toLower();
        } else if (tag == QLatin1String("l")) {
            const int n = next();
            out += QChar(QLatin1Char('a').unicode() + (n - 1) % 26);
        } else if (tag == QLatin1String("L")) {
            const int n = next();
            out += QChar(QLatin1Char('A').unicode() + (n - 1) % 26);
        } else if (tag == QLatin1String("title")) {
            out += sectionTitle;
        } else if (tag == QLatin1String("projecttitle")) {
            out += projectTitle;
        } else if (tag == QLatin1String("shorttitle")) {
            QStringList w = projectTitle.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (w.size() > 3)
                w = w.mid(0, 3);
            out += w.join(QLatin1Char(' ')).toUpper();
        } else if (tag == QLatin1String("author") || tag == QLatin1String("fullname")) {
            out += author;
        } else if (tag == QLatin1String("surname") || tag == QLatin1String("lastname")) {
            out += surname;
        } else if (tag == QLatin1String("forename") || tag == QLatin1String("firstname")) {
            out += forename;
        } else if (tag == QLatin1String("wc")) {
            out += QString::number(wordCount);
        } else if (tag.startsWith(QLatin1String("wc")) && tag.mid(2).toInt() > 0) {
            const int r = tag.mid(2).toInt();
            out += QString::number(qMax(r, int((wordCount + r / 2) / r) * r));
        } else if (tag == QLatin1String("date")) {
            out += QDate::currentDate().toString(Qt::ISODate);
        } else if (tag == QLatin1String("year")) {
            out += QString::number(QDate::currentDate().year());
        } else if (contact().contains(tag)) {
            out += contact().value(tag); // the author's address and the like (0.21)
        } else if (tag == QLatin1String("address")) {
            // street, then postcode and city, then state and country: one per line
            QStringList lines;
            const QMap<QString, QString> &c = contact();
            if (!c.value(QStringLiteral("street")).trimmed().isEmpty())
                lines << c.value(QStringLiteral("street")).trimmed();
            const QString place = (c.value(QStringLiteral("zip")) + QLatin1Char(' ') + c.value(QStringLiteral("city"))).trimmed();
            if (!place.isEmpty())
                lines << place;
            if (!c.value(QStringLiteral("state")).trimmed().isEmpty())
                lines << c.value(QStringLiteral("state")).trimmed();
            if (!c.value(QStringLiteral("country")).trimmed().isEmpty())
                lines << c.value(QStringLiteral("country")).trimmed();
            out += lines.join(QLatin1Char('\n'));
        } else if (tag == QLatin1String("p")) {
            out += m.captured(0); // page number: resolved by the exporter
        } else {
            out += m.captured(0);
        }
    }
    out += s.mid(last);
    return out;
}

// ---------------------------------------------------------------- compiler

Compiler::Compiler(Project *project, const Format &format, const CompileProjectSettings &settings,
                   const AuthorInfo &author)
    : m_project(project), m_format(format), m_settings(settings), m_author(author)
{
}

BinderItem *Compiler::compileRoot() const
{
    BinderItem *r = m_project->item(m_settings.compileGroupId);
    return r ? r : m_project->draft();
}

void Compiler::collect(BinderItem *item, int level, CompileEntry::Part part, QList<CompileEntry> &out,
                       const BinderItem *root) const
{
    for (BinderItem *c : item->children) {
        if (!c->hasText())
            continue;
        CompileEntry e;
        e.item = c;
        e.level = level;
        e.part = part;
        e.sectionType = m_project->effectiveSectionType(c, root);
        e.included = m_settings.includeOverrides.value(c->id, c->includeInCompile);
        out << e;
        collect(c, level + 1, part, out, root);
    }
}

QList<CompileEntry> Compiler::entries() const
{
    QList<CompileEntry> out;
    BinderItem *root = compileRoot();
    if (BinderItem *front = m_project->item(m_settings.frontMatterId))
        collect(front, 1, CompileEntry::Front, out, front);
    if (root != m_project->draft() && m_settings.includeGroupText) {
        CompileEntry e;
        e.item = root;
        e.level = 0;
        e.sectionType = m_project->effectiveSectionType(root, root);
        e.included = true;
        out << e;
    }
    collect(root, 1, CompileEntry::Main, out, root);
    if (BinderItem *back = m_project->item(m_settings.backMatterId))
        collect(back, 1, CompileEntry::Back, out, back);
    return out;
}

QString Compiler::layoutNameFor(const CompileEntry &e) const
{
    const QString key = m_format.name + QLatin1Char('|') + e.sectionType;
    const QString assigned = m_settings.layoutAssignments.value(key);
    if (!assigned.isEmpty() && m_format.layout(assigned))
        return assigned;
    const SectionType *st = m_project->sectionTypeById(e.sectionType);
    if (st) {
        const QString byName = m_format.defaultAssignments.value(st->name.toLower());
        if (!byName.isEmpty() && m_format.layout(byName))
            return byName;
    }
    const QString any = m_format.defaultAssignments.value(QStringLiteral("*"));
    if (!any.isEmpty() && m_format.layout(any))
        return any;
    if (e.item && e.item->isFolder()) {
        for (const QString &n : {QStringLiteral("Chapter Heading"), QStringLiteral("Heading")})
            if (m_format.layout(n))
                return n;
    }
    for (const QString &n : {QStringLiteral("Section Text"), QStringLiteral("Text")})
        if (m_format.layout(n))
            return n;
    return m_format.layouts.isEmpty() ? QString() : m_format.layouts.first().name;
}

static bool isSeparatorText(const QString &t)
{
    const QString s = t.trimmed();
    if (s.isEmpty() || s.size() > 12)
        return false;
    for (const QChar c : s)
        if (!(c == QLatin1Char('#') || c == QLatin1Char('*') || c == QLatin1Char(' ') || c == QLatin1Char('-')
              || c == QLatin1Char('~') || c == QChar(0x2022) || c == QChar(0x2042)))
            return false;
    return true;
}

static Paragraph::Style styleFromName(const QString &name)
{
    if (name == QLatin1String("Heading 1"))
        return Paragraph::Heading1;
    if (name == QLatin1String("Heading 2"))
        return Paragraph::Heading2;
    if (name.startsWith(QLatin1String("Heading")))
        return Paragraph::Heading3;
    if (name == QLatin1String("Title"))
        return Paragraph::Title;
    if (name == QLatin1String("Subtitle"))
        return Paragraph::Subtitle;
    if (name == QLatin1String("Block Quote"))
        return Paragraph::BlockQuote;
    if (name == QLatin1String("Centered"))
        return Paragraph::Centered;
    if (name == QLatin1String("Caption"))
        return Paragraph::Caption;
    return Paragraph::Normal;
}

// Paragraph attributes in which a style differs from its built-in definition.
static int changedParaBits(const direct::ParaSpec &a, const direct::ParaSpec &b)
{
    using namespace direct;
    auto differ = [](double x, double y) { return qAbs(x - y) > 0.2; };
    int bits = (a.bits ^ b.bits) & AllPara;
    const int both = a.bits & b.bits;
    if ((both & Align) && a.align != b.align)
        bits |= Align;
    if ((both & Left) && differ(a.left, b.left))
        bits |= Left;
    if ((both & Right) && differ(a.right, b.right))
        bits |= Right;
    if ((both & First) && differ(a.first, b.first))
        bits |= First;
    if ((both & Before) && differ(a.before, b.before))
        bits |= Before;
    if ((both & After) && differ(a.after, b.after))
        bits |= After;
    if ((both & Line) && (a.lineType != b.lineType || differ(a.line, b.line)))
        bits |= Line;
    if (both & Tabs)
        bits |= Tabs;
    return bits;
}

// "monospace" and the other generic names mean nothing to a word processor.
static QString exportFamily(const QString &family)
{
    static QHash<QString, QString> cache;
    if (family.compare(QLatin1String("monospace"), Qt::CaseInsensitive) != 0
        && family.compare(QLatin1String("serif"), Qt::CaseInsensitive) != 0
        && family.compare(QLatin1String("sans-serif"), Qt::CaseInsensitive) != 0)
        return family;
    auto it = cache.constFind(family);
    if (it == cache.constEnd())
        it = cache.insert(family, QFontInfo(QFont(family)).family());
    return it.value();
}

// ---------------------------------------------------------------- style overrides (Format > Styles)

static void applyRunOverride(Run &r, const StyleOverride &o)
{
    if (!o.fontFamily.isEmpty())
        r.fontFamily = o.fontFamily;
    if (o.fontSize > 0)
        r.fontSize = o.fontSize;
    if (o.bold >= 0)
        r.bold = o.bold;
    if (o.italic >= 0)
        r.italic = o.italic;
    if (o.underline >= 0)
        r.underline = o.underline;
    if (o.capitals >= 0)
        r.capitals = o.capitals;
    if (o.color.isValid())
        r.color = o.color;
    if (o.uppercase && r.kind == Run::Text)
        r.text = r.text.toUpper();
}

static void applyParaOverride(Paragraph &p, const StyleOverride &o, Placeholders &ph)
{
    if (o.align >= 0) {
        p.direct |= Paragraph::DAlign;
        p.align = o.align == 1 ? Qt::AlignHCenter : o.align == 2 ? Qt::AlignRight : o.align == 3 ? Qt::AlignJustify : Qt::AlignLeft;
    }
    if (o.leftIndentMm >= 0) {
        p.direct |= Paragraph::DLeft;
        p.leftIndentMm = o.leftIndentMm;
    }
    if (o.rightIndentMm >= 0) {
        p.direct |= Paragraph::DRight;
        p.rightIndentMm = o.rightIndentMm;
    }
    if (o.firstLineMm > -999) {
        p.direct |= Paragraph::DFirst;
        p.firstLineMm = o.firstLineMm;
    }
    if (o.spaceBeforePt >= 0) {
        p.direct |= Paragraph::DBefore;
        p.spaceBeforePt = o.spaceBeforePt;
    }
    if (o.spaceAfterPt >= 0) {
        p.direct |= Paragraph::DAfter;
        p.spaceAfterPt = o.spaceAfterPt;
    }
    if (o.lineSpacing > 0) {
        p.direct |= Paragraph::DLine;
        p.lineSpacing = o.lineSpacing;
        p.lineHeightPt = 0;
    }
    if (o.pageBreakBefore)
        p.pageBreakBefore = true;
    for (Run &r : p.runs)
        if (r.kind == Run::Text)
            applyRunOverride(r, o);
    Run look = p.runs.isEmpty() ? Run() : p.runs.first();
    look.kind = Run::Text;
    look.link.clear();
    look.footnote = look.comment = -1;
    if (!o.prefix.isEmpty()) {
        Run r = look;
        r.text = ph.apply(o.prefix);
        p.runs.prepend(r);
    }
    if (!o.suffix.isEmpty()) {
        Run r = look;
        r.text = ph.apply(o.suffix);
        p.runs.append(r);
    }
}

// the names compile formats give the styles they know
static QString styleNameOf(Paragraph::Style s)
{
    switch (s) {
    case Paragraph::Title: return QStringLiteral("Title");
    case Paragraph::Subtitle: return QStringLiteral("Subtitle");
    case Paragraph::Heading1: return QStringLiteral("Heading 1");
    case Paragraph::Heading2: return QStringLiteral("Heading 2");
    case Paragraph::Heading3: return QStringLiteral("Heading 3");
    case Paragraph::BlockQuote: return QStringLiteral("Block Quote");
    case Paragraph::Centered: return QStringLiteral("Centered");
    case Paragraph::Caption: return QStringLiteral("Caption");
    default: return QString();
    }
}

void Compiler::appendDocument(Manuscript &m, QTextDocument *doc, const QString &sourceId, bool noIndentFirst,
                              bool pageBreakFirst, Paragraph::Style forceStyle)
{
    QHash<QTextList *, int> listIds;
    QHash<QString, int> commentIndex;   // note id -> comment
    QList<Paragraph> paras;

    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        Paragraph p;
        p.sourceId = sourceId;
        const QTextBlockFormat bf = b.blockFormat();
        const QString styleName = bf.property(props::Style).toString();
        p.style = forceStyle != Paragraph::Normal ? forceStyle : m_editorLook ? Paragraph::Normal : styleFromName(styleName);
        if (forceStyle == Paragraph::Normal)
            if (const script::Element *se = script::element(m_project->formatting, styleName))
                p.script = script::roleKey(se->role);
        // Styles the compile formats know by name (headings, title, block
        // quote ...) get their look from the format. Every other style brings
        // its formatting along; so does a redefined built-in style when the
        // editor's look is kept.
        const Style *pstyle = forceStyle != Paragraph::Normal ? nullptr : styles::find(m_project->formatting, styleName);
        const bool ownLook = pstyle && p.style == Paragraph::Normal;
        const StyleOverride *pov = (pstyle && p.script.isEmpty()) ? m_format.styleOverride(pstyle->name) : nullptr;
        if (pov && pov->mode == StyleOverride::Remove)
            continue; // the format leaves this style out
        const bool asBody = pov && pov->mode == StyleOverride::AsBody;
        if (pstyle && p.script.isEmpty() && !asBody) {
            p.styleName = pstyle->name;
            int level = pstyle->headingLevel;
            if (!level && pstyle->name.startsWith(QLatin1String("Heading ")))
                level = pstyle->name.mid(8).toInt();
            if (level > 0)
                p.outlineLevel = qBound(1, m_docBaseLevel + level, 6);
        }
        if (asBody)
            p.style = Paragraph::Normal;
        int styleBits = 0;
        if (pstyle && !b.textList() && !asBody) {
            const Style *def = styles::builtinDefault(pstyle->name);
            if (ownLook)
                styleBits = pstyle->para.bits;
            else if (m_format.useEditorFormatting && def)
                styleBits = changedParaBits(pstyle->para, def->para);
        }
        if (QTextList *list = b.textList()) {
            const auto ls = list->format().style();
            const bool numbered = ls == QTextListFormat::ListDecimal || ls == QTextListFormat::ListLowerAlpha
                                  || ls == QTextListFormat::ListUpperAlpha || ls == QTextListFormat::ListLowerRoman
                                  || ls == QTextListFormat::ListUpperRoman;
            p.style = numbered ? Paragraph::ListNumber : Paragraph::ListBullet;
            p.listLevel = qMax(0, list->format().indent() - 1);
            if (!listIds.contains(list))
                listIds.insert(list, ++m_listCounter);
            p.listId = listIds.value(list);
        }
        // centered or right-aligned paragraphs; when the project's body text is centered or
        // right-aligned (0.40.3), that alignment counts only with the editor's formatting, a
        // compile format with a look of its own decides about body text
        const bool bodyAlign = bodyAlignment(bf.alignment()) == bodyAlignment(m_project->formatting.align);
        if (p.style == Paragraph::Normal && (m_format.useEditorFormatting || !bodyAlign)) {
            if (bf.alignment() & Qt::AlignHCenter)
                p.align = Qt::AlignHCenter;
            if (bf.alignment() & Qt::AlignRight)
                p.align = Qt::AlignRight;
        }
        // paragraph formatting set by hand. Markdown text marks it; RTF text
        // carries complete formatting, there it is what differs from the project
        int own = bf.intProperty(props::ParaDirect) & direct::AllPara;
        if (m_project->storage == StorageFormat::Rtf && !b.textList()) {
            QTextBlockFormat plain;
            own |= direct::differences(bf, display::blockFormatFor(m_project->formatting, styleName, plain));
            if ((bf.alignment() & Qt::AlignHCenter) && qAbs(bf.textIndent()) < 0.05)
                own &= ~direct::First;
        }
        // a compile format with a look of its own decides about indents and
        // spacing; alignment and tab stops belong to the text
        if (!m_format.useEditorFormatting)
            own &= direct::Align | direct::Tabs;
        own |= styleBits;
        if (ownLook && pstyle->keepWithNext && !asBody)
            p.keepWithNext = true;
        if (forceStyle != Paragraph::Normal)
            own = 0;
        if (own) {
            const direct::ParaSpec ps = direct::effectiveParaSpec(bf);
            p.direct = own;
            if (own & direct::Align)
                p.align = ps.align;
            p.leftIndentMm = direct::pxToMm(ps.left);
            p.rightIndentMm = direct::pxToMm(ps.right);
            p.firstLineMm = direct::pxToMm(ps.first);
            p.spaceBeforePt = direct::pxToPt(ps.before);
            p.spaceAfterPt = direct::pxToPt(ps.after);
            if (ps.lineType == QTextBlockFormat::ProportionalHeight) {
                p.lineSpacing = ps.line / 100.0;
            } else {
                p.lineHeightPt = direct::pxToPt(ps.line);
                p.lineAtLeast = ps.lineType == QTextBlockFormat::MinimumHeight;
            }
            for (const QTextOption::Tab &t : ps.tabs) {
                compile::TabStop ts;
                ts.positionMm = direct::pxToMm(bf.leftMargin() + bf.textIndent() + t.position);
                ts.type = t.type == QTextOption::RightTab ? compile::TabStop::Right
                          : t.type == QTextOption::CenterTab ? compile::TabStop::Center
                          : t.type == QTextOption::DelimiterTab ? compile::TabStop::Decimal
                                                                : compile::TabStop::Left;
                p.tabs << ts;
            }
        }
        if (bf.property(props::PageBreak).toBool() || (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore))
            p.pageBreakBefore = true;

        // runs
        QString openFootnoteId;   // inline footnote currently being collected
        int openFootnote = -1;
        auto closeInlineFootnote = [&]() {
            openFootnoteId.clear();
            openFootnote = -1;
        };
        QList<QTextFragment> frags;
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().isValid())
                frags << it.fragment();

        for (int fi = 0; fi < frags.size(); ++fi) {
            const QTextFragment &f = frags[fi];
            const QTextCharFormat cf = f.charFormat();
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            const QString noteId = cf.stringProperty(props::NoteId);

            Run base;
            base.bold = cf.fontWeight() >= QFont::DemiBold;
            base.italic = cf.fontItalic();
            base.underline = cf.fontUnderline() && !cf.isAnchor();
            base.strike = cf.fontStrikeOut();
            base.superscript = cf.verticalAlignment() == QTextCharFormat::AlignSuperScript;
            base.subscript = cf.verticalAlignment() == QTextCharFormat::AlignSubScript;
            // headings: bold/italic from the style are not character formatting
            if (pstyle && (!ownLook || asBody)) {
                if (pstyle->bold)
                    base.bold = false;
                if (pstyle->italic)
                    base.italic = false;
            }
            const Style *runStyle = styles::findChar(m_project->formatting, cf.stringProperty(props::CharStyle));
            const StyleOverride *cov = runStyle ? m_format.styleOverride(runStyle->name) : nullptr;
            if (cov && cov->mode == StyleOverride::Remove && kind == NoteKind::None)
                continue;
            if (runStyle && !(cov && cov->mode == StyleOverride::AsBody))
                base.charStyle = runStyle->name;
            // links inside the project (documents, objects) are not links of the manuscript
            if (cf.isAnchor() && !cf.anchorHref().startsWith(QLatin1String("qb://")) && !cf.anchorHref().startsWith(QLatin1String("qbobj://")))
                base.link = cf.anchorHref();
            if (!m_format.removeHighlights && cf.property(props::Highlight).toBool())
                base.highlight = cf.background().color();
            {
                // fonts set by hand; a compile format with its own font replaces family and size
                const direct::CharSpec sp = direct::charSpec(cf);
                const Style *cstyle = styles::findChar(m_project->formatting, cf.stringProperty(props::CharStyle));
                // a font that belongs to a style is part of the text's meaning (code, verse ...)
                const bool styleFamily = !sp.family.isEmpty()
                                         && ((cstyle && cstyle->chars.family.compare(sp.family, Qt::CaseInsensitive) == 0)
                                             || (ownLook && pstyle->chars.family.compare(sp.family, Qt::CaseInsensitive) == 0));
                const bool styleSize = sp.size > 0
                                       && ((cstyle && qAbs(cstyle->chars.size - sp.size) < 0.01)
                                           || (ownLook && qAbs(pstyle->chars.size - sp.size) < 0.01));
                if (m_format.useEditorFormatting || styleFamily)
                    base.fontFamily = exportFamily(sp.family);
                if (m_format.useEditorFormatting || styleSize)
                    base.fontSize = sp.size;
                // relative sizes of styles without a compile style of their own
                if (base.fontSize <= 0 && cf.hasProperty(QTextFormat::FontSizeAdjustment)
                    && (ownLook || (cstyle && cstyle->sizeAdjust != 0))) {
                    const double body = m_format.useEditorFormatting ? m_project->formatting.fontSize : m_format.fontSize;
                    base.fontSize = styles::adjustedSize(body, cf.intProperty(QTextFormat::FontSizeAdjustment));
                }
                base.fontWeight = sp.weight;
                base.color = sp.color;
                base.letterSpacingEm = direct::spacingToEm(sp.spacing);
                base.capitals = sp.caps;
                if (asBody && ownLook) {
                    // the paragraph style's own font goes with it
                    if (styleFamily && !(cstyle && cstyle->chars.family.compare(sp.family, Qt::CaseInsensitive) == 0))
                        base.fontFamily.clear();
                    if (styleSize && !(cstyle && qAbs(cstyle->chars.size - sp.size) < 0.01))
                        base.fontSize = 0;
                }
                if (cov && cov->mode == StyleOverride::AsBody && cstyle) {
                    if (cstyle->bold)
                        base.bold = false;
                    if (cstyle->italic)
                        base.italic = false;
                    if (!cstyle->chars.family.isEmpty())
                        base.fontFamily.clear();
                    if (cstyle->chars.size > 0)
                        base.fontSize = 0;
                    if (cstyle->chars.color.isValid())
                        base.color = QColor();
                    if (cstyle->chars.caps)
                        base.capitals = 0;
                }
                if (cov && cov->mode == StyleOverride::Restyle)
                    applyRunOverride(base, *cov);
            }

            if (kind != NoteKind::InlineFootnote && openFootnote >= 0)
                closeInlineFootnote();

            if (kind == NoteKind::InlineAnnotation) {
                if (m_format.keepAnnotationsAsComments) {
                    int ci = commentIndex.value(noteId, -1);
                    if (ci < 0) {
                        m.comments.append({m_author.fullName(), f.text()});
                        ci = m.comments.size() - 1;
                        commentIndex.insert(noteId, ci);
                        Run r;
                        r.comment = ci;
                        p.runs << r;
                    } else {
                        m.comments[ci].text += f.text();
                    }
                }
                continue;
            }

            QList<Run> pieces;
            if (cf.isImageFormat()) {
                const QTextImageFormat imf = cf.toImageFormat();
                QVariant v = doc->resource(QTextDocument::ImageResource, QUrl(imf.name()));
                QImage img = v.value<QImage>();
                if (img.isNull() && v.canConvert<QByteArray>())
                    img.loadFromData(v.toByteArray());
                if (!img.isNull()) {
                    Run r = base;
                    r.kind = Run::Image;
                    r.image = img;
                    const double w = imf.width() > 0 ? imf.width() : img.width();
                    const double h = imf.height() > 0 ? imf.height()
                                                       : (imf.width() > 0 ? img.height() * imf.width() / img.width()
                                                                          : img.height());
                    r.imageSize = QSizeF(w * 0.75, h * 0.75);
                    pieces << r;
                }
            } else {
                QString t = f.text();
                if (t.contains(QLatin1String("<$")))
                    t = m_ph.apply(t);
                QString cur;
                auto flush = [&]() {
                    if (!cur.isEmpty()) {
                        Run r = base;
                        r.text = cur;
                        pieces << r;
                        cur.clear();
                    }
                };
                for (const QChar c : t) {
                    if (c == QLatin1Char('\t')) {
                        flush();
                        Run r = base;
                        r.kind = Run::Tab;
                        pieces << r;
                    } else if (c == QChar::LineSeparator) {
                        flush();
                        Run r = base;
                        r.kind = Run::LineBreak;
                        pieces << r;
                    } else if (c != QChar::ObjectReplacementCharacter) {
                        cur += c;
                    }
                }
                flush();
            }

            if (kind == NoteKind::InlineFootnote) {
                if (openFootnoteId != noteId || openFootnote < 0) {
                    Footnote fn;
                    Paragraph fp;
                    fn.paragraphs << fp;
                    m.footnotes << fn;
                    openFootnote = m.footnotes.size() - 1;
                    openFootnoteId = noteId;
                    Run ref;
                    ref.kind = Run::FootnoteRef;
                    ref.footnote = openFootnote;
                    p.runs << ref;
                }
                for (Run r : pieces) {
                    r.comment = -1;
                    m.footnotes[openFootnote].paragraphs.last().runs << r;
                }
                continue;
            }

            if (kind == NoteKind::Comment && m_format.keepComments) {
                int ci = commentIndex.value(noteId, -1);
                if (ci < 0) {
                    m.comments.append({m_author.fullName(), cf.stringProperty(props::NoteText)});
                    ci = m.comments.size() - 1;
                    commentIndex.insert(noteId, ci);
                }
                for (Run &r : pieces)
                    r.comment = ci;
            }
            p.runs << pieces;

            if (kind == NoteKind::LinkedFootnote) {
                // footnote marker after the last fragment of this anchor
                const bool last = fi + 1 >= frags.size()
                                  || frags[fi + 1].charFormat().stringProperty(props::NoteId) != noteId;
                if (last) {
                    Footnote fn;
                    Paragraph fp;
                    Run r;
                    r.text = cf.stringProperty(props::NoteText);
                    fp.runs << r;
                    fn.paragraphs << fp;
                    m.footnotes << fn;
                    Run ref;
                    ref.kind = Run::FootnoteRef;
                    ref.footnote = m.footnotes.size() - 1;
                    p.runs << ref;
                }
            }
        }
        if (p.style == Paragraph::Normal && p.align == Qt::Alignment() && isSeparatorText(p.plainText()))
            p.style = Paragraph::Separator;
        if (pov && pov->mode == StyleOverride::Restyle)
            applyParaOverride(p, *pov, m_ph);
        if (p.outlineLevel > 0)
            p.anchor = QStringLiteral("sec%1").arg(++m_anchorCounter);
        paras << p;
    }
    // trim trailing empty paragraphs
    while (!paras.isEmpty() && paras.last().runs.isEmpty() && !paras.last().pageBreakBefore)
        paras.removeLast();
    if (paras.isEmpty())
        return;
    if (pageBreakFirst)
        paras.first().pageBreakBefore = true;
    if (noIndentFirst) {
        for (Paragraph &p : paras) {
            if (!p.plainText().trimmed().isEmpty() || !p.runs.isEmpty()) {
                p.noIndent = true;
                break;
            }
        }
    }
    m.paragraphs << paras;
}

Manuscript Compiler::build()
{
    Manuscript m;
    m_ph.reset();
    m_listCounter = 0;
    m.title = m_settings.titleOverride.isEmpty() ? m_project->title() : m_settings.titleOverride;
    m.author = m_settings.authorOverride.isEmpty() ? m_author.fullName() : m_settings.authorOverride;
    m_ph.projectTitle = m.title;
    m_ph.author = m.author;
    m_ph.forename = m_author.forename;
    m_ph.surname = m_author.surname.isEmpty() ? m.author.section(QLatin1Char(' '), -1) : m_author.surname;

    // formatting
    if (m_format.useEditorFormatting) {
        const Formatting &f = m_project->formatting;
        m.fontFamily = display::resolvedFontFamily(f);
        m.fontSize = f.fontSize;
        m.lineSpacing = f.lineSpacing;
        m.firstLineIndentMm = f.firstLineIndent * 25.4 / 96.0;
        m.paragraphSpacingPt = f.paragraphSpacing * 0.75;
        m.justify = f.align == Qt::AlignJustify;
    } else {
        m.fontFamily = m_format.fontFamily;
        m.fontSize = m_format.fontSize;
        m.lineSpacing = m_format.lineSpacing;
        m.firstLineIndentMm = m_format.firstLineIndentMm;
        m.paragraphSpacingPt = m_format.paragraphSpacingPt;
        m.justify = m_format.justify;
    }
    m.headingFontFamily = m_format.headingFontFamily;
    if (!m_settings.fontOverride.isEmpty()) {
        // "Font:" above the section layouts (0.21): one font for all of it (finish() drops fonts set by hand)
        m.fontFamily = m_settings.fontOverride;
        m.headingFontFamily = m_settings.fontOverride;
    }
    m.headingScale = m_format.headingScale;
    m.headingBold = m_format.headingBold;
    m.page = m_format.page;

    const QList<CompileEntry> list = entries();
    // word count of what is compiled (for <$wc>)
    int wc = 0;
    for (const CompileEntry &e : list)
        if (e.included)
            wc += e.item->wordCount;
    m_ph.wordCount = wc;
    m.metadata.insert(QStringLiteral("wordcount"), QString::number(wc));

    m.page.headerLeft = m_ph.apply(m.page.headerLeft);
    m.page.headerCenter = m_ph.apply(m.page.headerCenter);
    m.page.headerRight = m_ph.apply(m.page.headerRight);
    m.page.footerCenter = m_ph.apply(m.page.footerCenter);

    bool lastWasText = false;
    bool pendingBreak = false;
    bool first = true;
    CompileEntry::Part lastPart = CompileEntry::Front;
    for (const CompileEntry &e : list) {
        if (!e.included)
            continue;
        BinderItem *item = e.item;
        const SectionLayout *layout = m_format.layout(layoutNameFor(e));
        SectionLayout fallback;
        if (!layout)
            layout = &fallback;
        const int before = m.paragraphs.size();
        bool breakHere = pendingBreak || (layout->pageBreakBefore && !first);
        if (e.part != lastPart && !first && e.part == CompileEntry::Main)
            breakHere = true; // main text starts on a new page after front matter
        if (e.part == CompileEntry::Back && lastPart != CompileEntry::Back && !first)
            breakHere = true;
        lastPart = e.part;
        pendingBreak = false;
        m_ph.sectionTitle = item->title;

        if (layout->includeTitle) {
            for (int i = 0; i < layout->blankLinesBeforeTitle; ++i) {
                Paragraph blank;
                blank.sourceId = item->id;
                blank.noIndent = true;
                m.paragraphs << blank;
            }
            QString t = m_ph.apply(layout->titlePrefix) + (layout->showItemTitle ? item->title : QString())
                        + m_ph.apply(layout->titleSuffix);
            if (layout->titleUppercase)
                t = t.toUpper();
            const QStringList lines = t.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            const StyleOverride *tov = m_format.styleOverride(styleNameOf(layout->titleStyle));
            QStringList tocParts;
            for (const QString &line : lines)
                tocParts << line.trimmed();
            for (int li = 0; li < lines.size(); ++li) {
                Paragraph p;
                p.sourceId = item->id;
                p.style = layout->titleStyle;
                p.align = layout->titleAlign;
                p.keepWithNext = true;
                p.noIndent = true;
                Run r;
                r.text = lines[li].trimmed();
                p.runs << r;
                if (li == 0) {
                    // the heading of a section: an entry of the contents
                    p.outlineLevel = qBound(1, e.level, 6);
                    p.tocText = tocParts.join(QStringLiteral(": "));
                    p.anchor = QStringLiteral("sec%1").arg(++m_anchorCounter);
                }
                if (tov && tov->mode == StyleOverride::Restyle)
                    applyParaOverride(p, *tov, m_ph);
                if (!(tov && tov->mode == StyleOverride::Remove))
                    m.paragraphs << p;
            }
        } else if (lastWasText && !breakHere) {
            const QString sep = layout->separator;
            if (sep == QLatin1String("blank")) {
                Paragraph p;
                p.sourceId = item->id;
                m.paragraphs << p;
            } else if (sep == QLatin1String("page")) {
                breakHere = true;
            } else if (!sep.isEmpty() && sep != QLatin1String("none")) {
                Paragraph p;
                p.sourceId = item->id;
                p.style = Paragraph::Separator;
                Run r;
                r.text = sep;
                p.runs << r;
                m.paragraphs << p;
            }
        }
        if (layout->includeSynopsis && !item->synopsis.isEmpty()) {
            Paragraph p;
            p.sourceId = item->id;
            p.style = Paragraph::Synopsis;
            Run r;
            r.text = m_ph.apply(item->synopsis);
            p.runs << r;
            m.paragraphs << p;
        }
        if (layout->includeNotes) {
            QTextDocument *nd = m_project->docs()->notes(item->id);
            if (!nd->isEmpty())
                appendDocument(m, nd, item->id, true, false, Paragraph::Synopsis);
        }
        bool hadText = false;
        if (layout->includeText && item->hasText()) {
            QTextDocument *doc = m_project->docs()->text(item->id);
            const int beforeText = m.paragraphs.size();
            m_docBaseLevel = layout->includeTitle ? e.level : qMax(0, e.level - 1);
            appendDocument(m, doc, item->id, layout->noIndentFirstParagraph || layout->includeTitle, false);
            hadText = m.paragraphs.size() > beforeText;
        }
        if (breakHere && m.paragraphs.size() > before)
            m.paragraphs[before].pageBreakBefore = true;
        else if (breakHere)
            pendingBreak = true;
        if (m.paragraphs.size() > before)
            first = false;
        lastWasText = hadText;
        if (layout->name == QLatin1String("Title Page"))
            pendingBreak = true;
    }
    if (m_settings.objectSheets)
        appendObjectSheets(m);
    if (!m.paragraphs.isEmpty())
        m.paragraphs.first().pageBreakBefore = false;
    finish(m);
    return m;
}

void Compiler::appendObjectSheets(Manuscript &m)
{
    objects::Store *store = m_project->objects();
    if (!store)
        return;
    // the text of a long field (Markdown with links to objects): paragraphs with bold and italics
    auto longText = [&](const QString &md, const QString &sourceId) {
        QList<Paragraph> out;
        QTextDocument doc;
        doc.setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
            Paragraph p;
            p.sourceId = sourceId;
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                Run r;
                r.text = f.text();
                r.bold = f.charFormat().fontWeight() >= QFont::Bold;
                r.italic = f.charFormat().fontItalic();
                r.underline = f.charFormat().fontUnderline() && !f.charFormat().isAnchor(); // a link is shown as its words
                p.runs << r;
            }
            if (!p.runs.isEmpty())
                out << p;
        }
        return out;
    };
    auto label = [](const QString &l) {
        Run r;
        r.text = l + QStringLiteral(": ");
        r.bold = true;
        return r;
    };
    bool firstCategory = true;
    for (const objects::Category &cat : store->schema().categories) {
        if (!m_settings.objectCategories.isEmpty() && !m_settings.objectCategories.contains(cat.id))
            continue;
        const QList<const objects::Object *> list = store->objects(cat.id);
        if (list.isEmpty())
            continue;
        // the category: a heading on a new page (an entry of the contents)
        Paragraph head;
        head.style = Paragraph::Heading1;
        head.pageBreakBefore = !m.paragraphs.isEmpty();
        head.keepWithNext = true;
        head.noIndent = true;
        head.outlineLevel = 1;
        head.tocText = cat.name;
        head.anchor = QStringLiteral("sec%1").arg(++m_anchorCounter);
        Run hr;
        hr.text = cat.name;
        head.runs << hr;
        m.paragraphs << head;
        firstCategory = false;
        for (const objects::Object *o : list) {
            Paragraph name;
            name.style = Paragraph::Heading2;
            name.keepWithNext = true;
            name.noIndent = true;
            name.outlineLevel = 2;
            name.tocText = o->name;
            name.anchor = QStringLiteral("sec%1").arg(++m_anchorCounter);
            Run nr;
            nr.text = o->name.isEmpty() ? QObject::tr("Unnamed") : o->name;
            name.runs << nr;
            m.paragraphs << name;
            for (const objects::FieldDef &f : cat.fields) {
                Paragraph p;
                p.noIndent = true;
                switch (f.type) {
                case objects::FieldType::Text:
                case objects::FieldType::Choice:
                case objects::FieldType::Date:
                case objects::FieldType::Number: {
                    QString v = o->text(f.id).trimmed();
                    if (v.isEmpty())
                        continue;
                    if (f.type == objects::FieldType::Number && !f.unit.isEmpty())
                        v += QLatin1Char(' ') + f.unit;
                    Run r;
                    r.text = v;
                    p.runs << label(f.label) << r;
                    m.paragraphs << p;
                    break;
                }
                case objects::FieldType::YesNo: {
                    if (!o->values.contains(f.id))
                        continue;
                    Run r;
                    r.text = o->values.value(f.id).toBool() ? QObject::tr("Yes") : QObject::tr("No");
                    p.runs << label(f.label) << r;
                    m.paragraphs << p;
                    break;
                }
                case objects::FieldType::Objects:
                case objects::FieldType::Documents: {
                    QStringList names;
                    for (const QString &id : o->ids(f.id)) {
                        if (f.type == objects::FieldType::Objects) {
                            if (const objects::Object *t = store->object(id))
                                names << t->name;
                        } else if (BinderItem *it = m_project->item(id)) {
                            names << it->title;
                        }
                    }
                    if (names.isEmpty())
                        continue;
                    Run r;
                    r.text = names.join(QStringLiteral(", "));
                    p.runs << label(f.label) << r;
                    m.paragraphs << p;
                    break;
                }
                case objects::FieldType::LongText: {
                    const QString md = o->text(f.id).trimmed();
                    if (md.isEmpty())
                        continue;
                    p.runs << label(f.label);
                    p.keepWithNext = true;
                    m.paragraphs << p;
                    m.paragraphs << longText(md, QString());
                    break;
                }
                case objects::FieldType::Picture: {
                    const QString file = o->text(f.id);
                    const QImage img(file.isEmpty() ? QString() : store->picturePath(file));
                    if (img.isNull())
                        continue;
                    Run r;
                    r.kind = Run::Image;
                    r.image = img;
                    // at most 8 cm wide (about 227 pt), in proportion
                    const double w = qMin(227.0, img.width() * 0.75);
                    r.imageSize = QSizeF(w, w * img.height() / qMax(1, img.width()));
                    p.runs << r;
                    m.paragraphs << p;
                    break;
                }
                }
            }
        }
    }
    Q_UNUSED(firstCategory);
}

Manuscript Compiler::buildPrint(const QList<PrintPart> &parts, const PrintOptions &o)
{
    Manuscript m;
    m_ph.reset();
    m_listCounter = 0;
    m_editorLook = true;
    m.title = m_project->title();
    m.author = m_author.fullName();
    m_ph.projectTitle = m.title;
    m_ph.author = m.author;
    m_ph.forename = m_author.forename;
    m_ph.surname = m_author.surname.isEmpty() ? m.author.section(QLatin1Char(' '), -1) : m_author.surname;
    const Formatting &f = m_project->formatting;
    m.fontFamily = display::resolvedFontFamily(f);
    m.fontSize = f.fontSize;
    m.lineSpacing = f.lineSpacing;
    m.firstLineIndentMm = f.firstLineIndent * 25.4 / 96.0;
    m.paragraphSpacingPt = f.paragraphSpacing * 0.75;
    m.justify = f.align == Qt::AlignJustify;
    m.headingScale = 1.5;  // the titles of the documents
    m.headingBold = true;
    m.page = m_format.page;
    int wc = 0;
    for (const PrintPart &part : parts)
        if (part.item)
            wc += part.item->wordCount;
    m_ph.wordCount = wc;
    bool lastHadText = false;
    for (const PrintPart &part : parts) {
        BinderItem *item = part.item;
        if (!item)
            continue;
        m_ph.sectionTitle = item->title;
        const int level = qBound(1, part.level, 3);
        if (o.titles) {
            Paragraph p;
            p.sourceId = item->id;
            p.style = level == 1 ? Paragraph::Heading1 : level == 2 ? Paragraph::Heading2 : Paragraph::Heading3;
            p.direct |= Paragraph::DAlign;
            p.align = Qt::AlignLeft;
            p.keepWithNext = true;
            p.noIndent = true;
            p.outlineLevel = level;
            p.anchor = QStringLiteral("sec%1").arg(++m_anchorCounter);
            Run r;
            r.text = item->title.isEmpty() ? QObject::tr("Untitled") : item->title;
            p.runs << r;
            m.paragraphs << p;
        } else if (lastHadText) {
            // documents follow one another with an empty line between them
            Paragraph blank;
            blank.sourceId = item->id;
            m.paragraphs << blank;
        }
        if (o.synopses && !item->synopsis.trimmed().isEmpty()) {
            Paragraph p;
            p.sourceId = item->id;
            p.style = Paragraph::Synopsis;
            Run r;
            r.text = item->synopsis;
            p.runs << r;
            m.paragraphs << p;
        }
        const int beforeText = m.paragraphs.size();
        if (part.doc) {
            m_docBaseLevel = o.titles ? level : level - 1;
            appendDocument(m, part.doc, item->id, o.titles, false);
        }
        lastHadText = m.paragraphs.size() > beforeText;
        if (o.notes) {
            QTextDocument *nd = m_project->docs()->notes(item->id);
            if (nd && !nd->toPlainText().trimmed().isEmpty()) {
                Paragraph h;
                h.sourceId = item->id;
                h.style = Paragraph::Heading3;
                h.direct |= Paragraph::DAlign;
                h.align = Qt::AlignLeft;
                h.keepWithNext = true;
                h.noIndent = true;
                Run r;
                r.text = QObject::tr("Notes");
                h.runs << r;
                m.paragraphs << h;
                appendDocument(m, nd, item->id, true, false, Paragraph::Synopsis);
                lastHadText = true;
            }
        }
    }
    if (!m.paragraphs.isEmpty())
        m.paragraphs.first().pageBreakBefore = false;
    finish(m);
    m_editorLook = false;
    return m;
}

void commentsAsFootnotes(Manuscript &m, const QString &prefix)
{
    if (m.comments.isEmpty())
        return;
    // the last run of each comment
    QHash<int, QPair<int, int>> last;
    for (int pi = 0; pi < m.paragraphs.size(); ++pi)
        for (int ri = 0; ri < m.paragraphs[pi].runs.size(); ++ri) {
            const int c = m.paragraphs[pi].runs[ri].comment;
            if (c >= 0 && c < m.comments.size())
                last.insert(c, {pi, ri});
        }
    // insert from the end, so that the places found stay valid
    QList<int> order = last.keys();
    std::sort(order.begin(), order.end(), [&last](int a, int b) { return last.value(a) > last.value(b); });
    QHash<int, int> noteOf;
    for (int c : order) {
        Footnote fn;
        Paragraph fp;
        Run r;
        r.text = prefix + m.comments[c].text.simplified();
        r.italic = true;
        fp.runs << r;
        fn.paragraphs << fp;
        m.footnotes << fn;
        noteOf.insert(c, m.footnotes.size() - 1);
    }
    for (int c : order) {
        const auto at = last.value(c);
        Run ref;
        ref.kind = Run::FootnoteRef;
        ref.footnote = noteOf.value(c);
        m.paragraphs[at.first].runs.insert(at.second + 1, ref);
    }
    for (Paragraph &p : m.paragraphs)
        for (Run &r : p.runs)
            r.comment = -1;
    m.comments.clear();
}

// ---------------------------------------------------------------- after the text: replacements,
// transformations, output settings, the styles that are named

static void transformText(QString &t, const Transformations &tr)
{
    if (tr.straightQuotes) {
        t.replace(QChar(0x201C), QLatin1Char('"')).replace(QChar(0x201D), QLatin1Char('"'));
        t.replace(QChar(0x201E), QLatin1Char('"')).replace(QChar(0x2018), QLatin1Char('\'')).replace(QChar(0x2019), QLatin1Char('\''));
    }
    if (tr.smartQuotes && !tr.straightQuotes) {
        QString o;
        o.reserve(t.size());
        for (int i = 0; i < t.size(); ++i) {
            const QChar c = t.at(i);
            const bool open = i == 0 || t.at(i - 1).isSpace() || QStringLiteral("([{\u2014\u2013-").contains(t.at(i - 1));
            if (c == QLatin1Char('"'))
                o += open ? QChar(0x201C) : QChar(0x201D);
            else if (c == QLatin1Char('\''))
                o += open ? QChar(0x2018) : QChar(0x2019);
            else
                o += c;
        }
        t = o;
    }
    if (tr.dashesToHyphens) {
        t.replace(QChar(0x2014), QStringLiteral("--"));
        t.replace(QChar(0x2013), QLatin1Char('-'));
    }
    if (tr.collapseSpaces) {
        static const QRegularExpression spaces(QStringLiteral(" {2,}"));
        t.replace(spaces, QStringLiteral(" "));
    }
}

struct Repl {
    QRegularExpression re;
    QString with;
    bool literal;
};

static void replaceIn(QString &t, const Repl &x)
{
    if (!x.literal) {
        t.replace(x.re, x.with);
        return;
    }
    // the new text as it is (no \1 references)
    QString out;
    int last = 0;
    auto it = x.re.globalMatch(t);
    bool any = false;
    while (it.hasNext()) {
        const auto m = it.next();
        if (m.capturedLength() == 0)
            continue;
        out += t.mid(last, m.capturedStart() - last) + x.with;
        last = m.capturedEnd();
        any = true;
    }
    if (any)
        t = out + t.mid(last);
}

static void finishParagraph(Paragraph &p, const Format &f, const QList<Repl> &repl)
{
    for (Run &r : p.runs) {
        if (r.kind != Run::Text)
            continue;
        for (const Repl &x : repl)
            replaceIn(r.text, x);
        transformText(r.text, f.transform);
        if (f.transform.italicsToUnderline && r.italic) {
            r.italic = false;
            r.underline = true;
        }
        if (f.transform.plainText) {
            r.bold = r.italic = r.underline = r.strike = r.superscript = r.subscript = false;
            r.fontFamily.clear();
            r.fontSize = 0;
            r.fontWeight = 0;
            r.color = QColor();
            r.highlight = QColor();
            r.letterSpacingEm = 0;
            r.capitals = 0;
            r.charStyle.clear();
        }
    }
    if (f.transform.removeTrailingSpaces) {
        for (int i = p.runs.size() - 1; i >= 0; --i) {
            Run &r = p.runs[i];
            if (r.kind != Run::Text)
                break;
            while (!r.text.isEmpty() && r.text.back() == QLatin1Char(' '))
                r.text.chop(1);
            if (!r.text.isEmpty())
                break;
        }
    }
    if (f.transform.plainText) {
        p.styleName.clear();
    }
}

static NamedStyle namedStyle(const Style &s)
{
    NamedStyle n;
    n.name = s.name;
    n.character = s.character;
    n.fontFamily = exportFamily(s.chars.family);
    n.fontSize = s.chars.size;
    if (s.bold)
        n.bold = 1;
    if (s.italic)
        n.italic = 1;
    if (s.underline)
        n.underline = 1;
    n.color = s.chars.color;
    n.capitals = s.chars.caps;
    const direct::ParaSpec &ps = s.para;
    if (ps.bits & direct::Align)
        n.align = (ps.align & Qt::AlignHCenter) ? 1 : (ps.align & Qt::AlignRight) ? 2 : (ps.align & Qt::AlignJustify) ? 3 : 0;
    if (ps.bits & direct::Left)
        n.leftIndentMm = direct::pxToMm(ps.left);
    if (ps.bits & direct::Right)
        n.rightIndentMm = direct::pxToMm(ps.right);
    if (ps.bits & direct::First)
        n.firstLineMm = direct::pxToMm(ps.first);
    if (ps.bits & direct::Before)
        n.spaceBeforePt = direct::pxToPt(ps.before);
    if (ps.bits & direct::After)
        n.spaceAfterPt = direct::pxToPt(ps.after);
    if ((ps.bits & direct::Line) && ps.lineType == QTextBlockFormat::ProportionalHeight)
        n.lineSpacing = ps.line / 100.0;
    return n;
}

void Compiler::finish(Manuscript &m)
{
    QList<Repl> repl;
    for (const Replacement &r : m_format.replacements) {
        if (!r.enabled || r.find.isEmpty())
            continue;
        QString pat = r.regex ? r.find : QRegularExpression::escape(r.find);
        if (r.wholeWords)
            pat = QStringLiteral("(?<![\\w])(?:") + pat + QStringLiteral(")(?![\\w])");
        QRegularExpression re(pat, (r.matchCase ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption)
                                       | QRegularExpression::UseUnicodePropertiesOption);
        if (!re.isValid())
            continue;
        repl.append({re, r.replace, !r.regex});
    }
    // the project's own replacements (0.21, the Replacements tab of the compile window) after the format's
    for (const CompileReplacement &r : m_settings.replacements) {
        if (!r.enabled || r.find.isEmpty())
            continue;
        QString pat = r.regex ? r.find : QRegularExpression::escape(r.find);
        if (r.wholeWords)
            pat = QStringLiteral("(?<![\\w])(?:") + pat + QStringLiteral(")(?![\\w])");
        QRegularExpression re(pat, (r.matchCase ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption)
                                       | QRegularExpression::UseUnicodePropertiesOption);
        if (!re.isValid())
            continue;
        repl.append({re, r.replace, !r.regex});
    }
    if (!m_settings.fontOverride.isEmpty()) {
        // one font for everything: fonts set by hand in the text give way
        auto plain = [](QList<Paragraph> &ps) {
            for (Paragraph &p : ps)
                for (Run &r : p.runs)
                    r.fontFamily.clear();
        };
        plain(m.paragraphs);
        for (Footnote &fn : m.footnotes)
            plain(fn.paragraphs);
    }
    for (Paragraph &p : m.paragraphs)
        finishParagraph(p, m_format, repl);
    for (Footnote &fn : m.footnotes)
        for (Paragraph &p : fn.paragraphs)
            finishParagraph(p, m_format, repl);

    OutputOptions &o = m.options;
    o.toc = m_format.toc;
    o.tocTitle = m_format.tocTitle;
    o.tocLevels = m_format.tocLevels;
    o.footnotePlacement = m_format.footnotePlacement;
    o.endnotesTitle = m_format.endnotesTitle;
    o.footnoteSize = m_format.footnoteSize;
    o.pdfBookmarks = m_format.pdfBookmarks;
    o.pdfLinks = m_format.pdfLinks;
    o.splitAtPageBreaks = m_format.splitAtPageBreaks;
    o.tocPage = m_format.tocPage;
    o.publisher = m_ph.apply(m_settings.publisher);
    o.description = m_ph.apply(m_settings.description);
    o.subject = m_ph.apply(m_settings.subject);
    o.identifier = m_settings.identifier;
    o.date = m_settings.date.isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : m_settings.date;
    if (BinderItem *cover = m_project->item(m_settings.coverId))
        if (cover->type == ItemType::Image)
            o.cover = QImage(m_project->mediaFilePath(cover));
    if (!m_settings.language.isEmpty())
        m.language = m_settings.language;
    if (!m_settings.subtitle.isEmpty())
        m.metadata.insert(QStringLiteral("subtitle"), m_ph.apply(m_settings.subtitle));
    if (!m_settings.subject.isEmpty())
        m.metadata.insert(QStringLiteral("subject"), o.subject);
    if (!m_settings.keywords.isEmpty())
        m.metadata.insert(QStringLiteral("keywords"), m_ph.apply(m_settings.keywords));

    // the own styles the text uses, with their look (DOCX names them, EPUB makes classes)
    QSet<QString> used;
    for (const Paragraph &p : std::as_const(m.paragraphs)) {
        if (!p.styleName.isEmpty() && p.style == Paragraph::Normal)
            used.insert(p.styleName);
        for (const Run &r : p.runs)
            if (!r.charStyle.isEmpty())
                used.insert(r.charStyle);
    }
    for (const QString &n : std::as_const(used))
        if (const Style *s = styles::sheet(m_project->formatting).find(n)) // paragraph or character style
            m.styles << namedStyle(*s);
    std::sort(m.styles.begin(), m.styles.end(), [](const NamedStyle &a, const NamedStyle &b) { return a.name < b.name; });
}

} // namespace compile
} // namespace qb
