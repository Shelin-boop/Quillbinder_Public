#include "text/MarkdownIO.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QMap>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QUuid>

namespace qb {
namespace md {

namespace {

bool isAsciiPunct(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 33 && u <= 47) || (u >= 58 && u <= 64) || (u >= 91 && u <= 96) || (u >= 123 && u <= 126);
}

bool isEscaped(const QString &s, int pos)
{
    int n = 0;
    for (int i = pos - 1; i >= 0 && s.at(i) == QLatin1Char('\\'); --i)
        ++n;
    return (n % 2) == 1;
}

// index of the ']' that closes the '[' at openPos, or -1
int matchBracket(const QString &s, int openPos, int end)
{
    int depth = 0;
    for (int i = openPos; i < end; ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\')) {
            ++i;
            continue;
        }
        if (c == QLatin1Char('['))
            ++depth;
        else if (c == QLatin1Char(']')) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

int matchParen(const QString &s, int openPos, int end)
{
    int depth = 0;
    for (int i = openPos; i < end; ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\')) {
            ++i;
            continue;
        }
        if (c == QLatin1Char('('))
            ++depth;
        else if (c == QLatin1Char(')')) {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return -1;
}

int findUnescaped(const QString &s, const QString &marker, int from, int end)
{
    int i = from;
    while (true) {
        i = s.indexOf(marker, i);
        if (i < 0 || i + marker.size() > end)
            return -1;
        if (!isEscaped(s, i))
            return i;
        ++i;
    }
}

QString decodeNote(const QString &raw, QColor *color)
{
    QString s = raw;
    static const QRegularExpression colorRe(QStringLiteral("^@(#[0-9A-Fa-f]{6})\\s"));
    const auto m = colorRe.match(s);
    if (m.hasMatch()) {
        if (color)
            *color = QColor(m.captured(1));
        s = s.mid(m.capturedLength());
    }
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar n = s.at(i + 1);
            if (n == QLatin1Char('n'))
                out += QLatin1Char('\n');
            else
                out += n;
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

QString encodeNote(const QString &text, const QColor &color, const QColor &defaultColor)
{
    QString out;
    if (color.isValid() && color != defaultColor)
        out = QLatin1Char('@') + color.name() + QLatin1Char(' ');
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '\\': out += QLatin1String("\\\\"); break;
        case '\n': out += QLatin1String("\\n"); break;
        case 0x2029: out += QLatin1String("\\n"); break;
        case 0x2028: out += QLatin1String("\\n"); break;
        case ']': out += QLatin1String("\\]"); break;
        case '<': out += QLatin1String("\\<"); break;
        case '{': out += QLatin1String("\\{"); break;
        case '\r': break;
        default: out += c;
        }
    }
    return out;
}

// ------------------------------------------------------------------ reader

enum class Kind { Bold, Italic, Strike, Highlight };

enum class SpanKind { Link, InlineFootnote, Annotation, Comment, LinkedFootnote, CritHighlight };

struct Action {
    bool open;
    Kind kind;
};

struct Tok {
    enum Type { Text, Delim, OpenSpan, CloseSpan, Image, Tag } type = Text;
    QString text;
    // delimiter
    QChar ch;
    int len = 0;
    bool canOpen = false, canClose = false;
    QString literalBefore, literalAfter;
    QList<Action> actions;
    // span
    SpanKind span = SpanKind::Link;
    QString href, noteText;
    QColor color;
    // tag
    QString tag;
    bool tagClose = false;
    QString css; // <span style="...">
    QString cls; // <span class="Character Style">
    // image
    QString src, alt;
    int width = 0, height = 0;
};

class InlineParser
{
public:
    explicit InlineParser(const QString &s) : m_s(s) {}

    QList<Tok> tokens()
    {
        QList<Tok> out;
        tokenize(0, m_s.size(), out);
        match(out);
        return out;
    }

private:
    const QString &m_s;

    QChar at(int i) const { return (i >= 0 && i < m_s.size()) ? m_s.at(i) : QChar(); }
    bool startsAt(int i, const char *lit) const { return m_s.mid(i).startsWith(QLatin1String(lit)); }

    void tokenize(int from, int to, QList<Tok> &out)
    {
        QString buf;
        auto flush = [&] {
            if (!buf.isEmpty()) {
                Tok t;
                t.type = Tok::Text;
                t.text = buf;
                out.append(t);
                buf.clear();
            }
        };
        int i = from;
        while (i < to) {
            const QChar c = m_s.at(i);
            if (c == QLatin1Char('\\') && i + 1 < to) {
                const QChar n = m_s.at(i + 1);
                if (isAsciiPunct(n)) {
                    buf += n;
                    i += 2;
                    continue;
                }
                buf += c;
                ++i;
                continue;
            }
            // image ![alt](src){width=..}
            if (c == QLatin1Char('!') && at(i + 1) == QLatin1Char('[')) {
                const int j = matchBracket(m_s, i + 1, to);
                if (j > 0 && at(j + 1) == QLatin1Char('(')) {
                    const int k = matchParen(m_s, j + 1, to);
                    if (k > 0) {
                        flush();
                        Tok t;
                        t.type = Tok::Image;
                        t.alt = m_s.mid(i + 2, j - i - 2);
                        t.src = m_s.mid(j + 2, k - j - 2).trimmed();
                        int next = k + 1;
                        if (at(next) == QLatin1Char('{')) {
                            const int e = m_s.indexOf(QLatin1Char('}'), next);
                            if (e > 0 && e < to) {
                                const QString attrs = m_s.mid(next + 1, e - next - 1);
                                static const QRegularExpression wre(QStringLiteral("width=(\\d+)"));
                                static const QRegularExpression hre(QStringLiteral("height=(\\d+)"));
                                auto wm = wre.match(attrs);
                                auto hm = hre.match(attrs);
                                if (wm.hasMatch())
                                    t.width = wm.captured(1).toInt();
                                if (hm.hasMatch())
                                    t.height = hm.captured(1).toInt();
                                next = e + 1;
                            }
                        }
                        out.append(t);
                        i = next;
                        continue;
                    }
                }
            }
            // link [text](url)
            if (c == QLatin1Char('[')) {
                const int j = matchBracket(m_s, i, to);
                if (j > 0 && at(j + 1) == QLatin1Char('(')) {
                    const int k = matchParen(m_s, j + 1, to);
                    if (k > 0) {
                        flush();
                        Tok o;
                        o.type = Tok::OpenSpan;
                        o.span = SpanKind::Link;
                        o.href = m_s.mid(j + 2, k - j - 2).trimmed();
                        o.href.replace(QLatin1String("%29"), QLatin1String(")"));
                        o.href.replace(QLatin1String("%28"), QLatin1String("("));
                        o.href.replace(QLatin1String("%20"), QLatin1String(" "));
                        out.append(o);
                        tokenize(i + 1, j, out);
                        Tok cl;
                        cl.type = Tok::CloseSpan;
                        out.append(cl);
                        i = k + 1;
                        continue;
                    }
                }
            }
            // inline footnote ^[...]
            if (c == QLatin1Char('^') && at(i + 1) == QLatin1Char('[')) {
                const int j = matchBracket(m_s, i + 1, to);
                if (j > 0) {
                    flush();
                    Tok o;
                    o.type = Tok::OpenSpan;
                    o.span = SpanKind::InlineFootnote;
                    out.append(o);
                    tokenize(i + 2, j, out);
                    Tok cl;
                    cl.type = Tok::CloseSpan;
                    out.append(cl);
                    i = j + 1;
                    continue;
                }
            }
            // {==anchor==} optionally followed by {>>comment<<} or ^[footnote]
            if (c == QLatin1Char('{') && startsAt(i, "{==")) {
                const int j = findUnescaped(m_s, QStringLiteral("==}"), i + 3, to);
                if (j > 0) {
                    const int after = j + 3;
                    Tok o;
                    o.type = Tok::OpenSpan;
                    int resume = after;
                    if (startsAt(after, "{>>")) {
                        const int k = findUnescaped(m_s, QStringLiteral("<<}"), after + 3, to);
                        if (k > 0) {
                            o.span = SpanKind::Comment;
                            o.noteText = decodeNote(m_s.mid(after + 3, k - after - 3), &o.color);
                            resume = k + 3;
                        } else {
                            o.span = SpanKind::CritHighlight;
                        }
                    } else if (startsAt(after, "^[")) {
                        const int k = matchBracket(m_s, after + 1, to);
                        if (k > 0) {
                            o.span = SpanKind::LinkedFootnote;
                            o.noteText = decodeNote(m_s.mid(after + 2, k - after - 2), nullptr);
                            resume = k + 1;
                        } else {
                            o.span = SpanKind::CritHighlight;
                        }
                    } else {
                        o.span = SpanKind::CritHighlight;
                    }
                    flush();
                    out.append(o);
                    tokenize(i + 3, j, out);
                    Tok cl;
                    cl.type = Tok::CloseSpan;
                    out.append(cl);
                    i = resume;
                    continue;
                }
            }
            // {>>annotation<<}
            if (c == QLatin1Char('{') && startsAt(i, "{>>")) {
                const int k = findUnescaped(m_s, QStringLiteral("<<}"), i + 3, to);
                if (k > 0) {
                    flush();
                    Tok o;
                    o.type = Tok::OpenSpan;
                    o.span = SpanKind::Annotation;
                    int innerStart = i + 3;
                    static const QRegularExpression colorRe(QStringLiteral("^@(#[0-9A-Fa-f]{6})\\s"));
                    const auto m = colorRe.match(m_s.mid(innerStart, 9));
                    if (m.hasMatch()) {
                        o.color = QColor(m.captured(1));
                        innerStart += m.capturedLength();
                    }
                    out.append(o);
                    tokenize(innerStart, k, out);
                    Tok cl;
                    cl.type = Tok::CloseSpan;
                    out.append(cl);
                    i = k + 3;
                    continue;
                }
            }
            // direct formatting: <span style="font-family:'X'; font-size:14pt">
            // character style: <span class="Emphasis">, also with style="..."
            if (c == QLatin1Char('<') && (startsAt(i, "<span style=\"") || startsAt(i, "<span class=\""))) {
                const int e = m_s.indexOf(QLatin1String("\">"), i + 13);
                if (e > 0 && e < to) {
                    flush();
                    Tok t;
                    t.type = Tok::Tag;
                    t.tag = QStringLiteral("span");
                    const QString attrs = m_s.mid(i + 6, e + 1 - (i + 6));
                    static const QRegularExpression attrRe(QStringLiteral("(class|style)=\"([^\"]*)\""));
                    auto it = attrRe.globalMatch(attrs);
                    while (it.hasNext()) {
                        const QRegularExpressionMatch m = it.next();
                        if (m.captured(1) == QLatin1String("class"))
                            t.cls = m.captured(2).trimmed();
                        else
                            t.css = m.captured(2);
                    }
                    out.append(t);
                    i = e + 2;
                    continue;
                }
            }
            if (c == QLatin1Char('<') && startsAt(i, "</span>")) {
                flush();
                Tok t;
                t.type = Tok::Tag;
                t.tag = QStringLiteral("span");
                t.tagClose = true;
                out.append(t);
                i += 7;
                continue;
            }
            // simple html-ish tags
            if (c == QLatin1Char('<')) {
                static const char *tags[] = {"<u>", "</u>", "<sup>", "</sup>", "<sub>", "</sub>", "<mark>",
                                             "</mark>", "<br>", "<br/>", "<br />", "<s>", "</s>", "<i>",
                                             "</i>", "<b>", "</b>", "<em>", "</em>", "<strong>", "</strong>",
                                             "<rev1>", "</rev1>", "<rev2>", "</rev2>", "<rev3>", "</rev3>",
                                             "<rev4>", "</rev4>", "<rev5>", "</rev5>"};
                bool matched = false;
                for (const char *tg : tags) {
                    if (startsAt(i, tg)) {
                        flush();
                        Tok t;
                        t.type = Tok::Tag;
                        QString name = QString::fromLatin1(tg);
                        t.tagClose = name.startsWith(QLatin1String("</"));
                        name.remove(QLatin1Char('<')).remove(QLatin1Char('>')).remove(QLatin1Char('/'));
                        t.tag = name.trimmed();
                        out.append(t);
                        i += int(qstrlen(tg));
                        matched = true;
                        break;
                    }
                }
                if (matched)
                    continue;
            }
            // emphasis delimiters
            if (c == QLatin1Char('*') || c == QLatin1Char('_') || c == QLatin1Char('~') || c == QLatin1Char('=')) {
                int n = 0;
                while (i + n < to && m_s.at(i + n) == c)
                    ++n;
                const bool pairOnly = (c == QLatin1Char('~') || c == QLatin1Char('='));
                if ((pairOnly && n != 2) || n > 3) {
                    buf += m_s.mid(i, n);
                    i += n;
                    continue;
                }
                const QChar prev = (i > 0) ? m_s.at(i - 1) : QChar();
                const QChar next = (i + n < m_s.size()) ? m_s.at(i + n) : QChar();
                Tok t;
                t.type = Tok::Delim;
                t.ch = c;
                t.len = n;
                t.canOpen = !next.isNull() && !next.isSpace();
                t.canClose = !prev.isNull() && !prev.isSpace();
                if (c == QLatin1Char('_')) {
                    if (!prev.isNull() && prev.isLetterOrNumber())
                        t.canOpen = false;
                    if (!next.isNull() && next.isLetterOrNumber())
                        t.canClose = false;
                }
                if (!t.canOpen && !t.canClose) {
                    buf += m_s.mid(i, n);
                    i += n;
                    continue;
                }
                flush();
                out.append(t);
                i += n;
                continue;
            }
            buf += c;
            ++i;
        }
        flush();
    }

    struct Entry {
        int tok;
        Kind kind;
        int chars;
        bool matched;
        bool barrier;
    };

    static int need(Kind k) { return k == Kind::Italic ? 1 : 2; }

    void match(QList<Tok> &toks)
    {
        QList<Entry> stack;
        QList<Entry> finished; // matched or dropped opener entries (for literal generation)
        auto dropAbove = [&](int p) {
            while (stack.size() > p + 1) {
                Entry e = stack.takeLast();
                if (!e.barrier)
                    finished.append(e);
            }
        };
        for (int idx = 0; idx < toks.size(); ++idx) {
            Tok &t = toks[idx];
            if (t.type == Tok::OpenSpan) {
                stack.append({idx, Kind::Bold, 0, false, true});
                continue;
            }
            if (t.type == Tok::CloseSpan) {
                while (!stack.isEmpty()) {
                    Entry e = stack.takeLast();
                    if (e.barrier)
                        break;
                    finished.append(e);
                }
                continue;
            }
            if (t.type != Tok::Delim)
                continue;
            int remaining = t.len;
            if (t.canClose) {
                while (remaining > 0) {
                    int p = -1;
                    for (int s = stack.size() - 1; s >= 0; --s) {
                        if (stack[s].barrier)
                            break;
                        if (toks[stack[s].tok].ch == t.ch) {
                            p = s;
                            break;
                        }
                    }
                    if (p < 0 || need(stack[p].kind) > remaining)
                        break;
                    dropAbove(p);
                    Entry e = stack.takeLast();
                    e.matched = true;
                    finished.append(e);
                    t.actions.append({false, e.kind});
                    remaining -= need(e.kind);
                }
            }
            if (remaining > 0 && t.canOpen) {
                if (t.ch == QLatin1Char('~')) {
                    stack.append({idx, Kind::Strike, 2, false, false});
                } else if (t.ch == QLatin1Char('=')) {
                    stack.append({idx, Kind::Highlight, 2, false, false});
                } else {
                    if (remaining >= 2)
                        stack.append({idx, Kind::Bold, 2, false, false});
                    if (remaining == 1 || remaining == 3)
                        stack.append({idx, Kind::Italic, 1, false, false});
                }
                remaining = 0;
            }
            if (remaining > 0)
                t.literalAfter = QString(remaining, t.ch);
        }
        for (const Entry &e : stack)
            if (!e.barrier)
                finished.append(e);
        // opener actions / literals, in push order per token
        std::sort(finished.begin(), finished.end(), [](const Entry &a, const Entry &b) {
            if (a.tok != b.tok)
                return a.tok < b.tok;
            return a.kind == Kind::Bold && b.kind == Kind::Italic; // bold pushed first
        });
        for (const Entry &e : finished) {
            Tok &t = toks[e.tok];
            if (e.matched)
                t.actions.append({true, e.kind});
            else
                t.literalBefore += QString(e.chars, t.ch);
        }
    }
};

struct Run {
    QString text;
    QTextCharFormat fmt;
    bool image = false;
    QString src;
    int width = 0, height = 0;
};

QList<Run> parseInline(const QString &content, const QTextCharFormat &base, const Formatting &fmt)
{
    InlineParser p(content);
    const QList<Tok> toks = p.tokens();
    QList<Run> runs;

    int bold = 0, italic = 0, strike = 0, highlight = 0, underline = 0, sup = 0, sub = 0;
    int revision = 0; // revision mode colour level (1..5), 0 = none
    struct Span {
        SpanKind kind;
        QString href, text, id;
        QColor color;
    };
    QList<Span> spans;
    QList<direct::CharSpec> fonts; // open <span style> tags, innermost last
    QStringList classes;           // character style of each open <span>

    auto current = [&]() {
        QTextCharFormat f = base;
        if (!classes.isEmpty() && !classes.last().isEmpty()) {
            if (const Style *cs = styles::findChar(fmt, classes.last())) {
                styles::applyChars(f, *cs);
                f.setProperty(props::CharStyle, cs->name);
            }
        }
        if (bold > 0)
            f.setFontWeight(QFont::Bold);
        if (italic > 0)
            f.setFontItalic(true);
        if (strike > 0)
            f.setFontStrikeOut(true);
        if (underline > 0)
            f.setFontUnderline(true);
        if (sup > 0)
            f.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        else if (sub > 0)
            f.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        if (highlight > 0)
            f.merge(text::highlightFormat());
        if (revision > 0)
            f.merge(text::revisionFormat(revision));
        for (const Span &s : spans) {
            switch (s.kind) {
            case SpanKind::Link:
                f.merge(text::linkFormat(s.href));
                break;
            case SpanKind::CritHighlight:
                f.merge(text::highlightFormat());
                break;
            case SpanKind::InlineFootnote:
                f.merge(text::noteFormat(NoteKind::InlineFootnote, s.id));
                break;
            case SpanKind::Annotation:
                f.merge(text::noteFormat(NoteKind::InlineAnnotation, s.id, QString(), s.color));
                break;
            case SpanKind::Comment:
                f.merge(text::noteFormat(NoteKind::Comment, s.id, s.text, s.color));
                break;
            case SpanKind::LinkedFootnote:
                f.merge(text::noteFormat(NoteKind::LinkedFootnote, s.id, s.text));
                break;
            }
        }
        if (!fonts.isEmpty())
            direct::mergeCharSpec(f, fonts.last());
        return f;
    };
    auto emitText = [&](const QString &t) {
        if (t.isEmpty())
            return;
        const QTextCharFormat f = current();
        if (!runs.isEmpty() && !runs.last().image && runs.last().fmt == f)
            runs.last().text += t;
        else
            runs.append({t, f});
    };

    for (const Tok &t : toks) {
        switch (t.type) {
        case Tok::Text:
            emitText(t.text);
            break;
        case Tok::Delim: {
            emitText(t.literalBefore);
            for (const Action &a : t.actions) {
                const int d = a.open ? 1 : -1;
                switch (a.kind) {
                case Kind::Bold: bold += d; break;
                case Kind::Italic: italic += d; break;
                case Kind::Strike: strike += d; break;
                case Kind::Highlight: highlight += d; break;
                }
            }
            emitText(t.literalAfter);
            break;
        }
        case Tok::OpenSpan: {
            Span s{t.span, t.href, t.noteText, QString(), t.color};
            if (t.span != SpanKind::Link && t.span != SpanKind::CritHighlight)
                s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            spans.append(s);
            break;
        }
        case Tok::CloseSpan:
            if (!spans.isEmpty())
                spans.removeLast();
            break;
        case Tok::Tag: {
            const int d = t.tagClose ? -1 : 1;
            if (t.tag == QLatin1String("span")) {
                if (t.tagClose) {
                    if (!fonts.isEmpty())
                        fonts.removeLast();
                    if (!classes.isEmpty())
                        classes.removeLast();
                } else {
                    classes.append(!t.cls.isEmpty() ? t.cls : classes.isEmpty() ? QString() : classes.last());
                    // an inner span inherits what it does not set itself
                    const direct::CharSpec in = direct::charSpecFromCss(t.css);
                    direct::CharSpec sp = fonts.isEmpty() ? direct::CharSpec() : fonts.last();
                    if (!in.family.isEmpty())
                        sp.family = in.family;
                    if (in.size > 0)
                        sp.size = in.size;
                    if (in.weight > 0)
                        sp.weight = in.weight;
                    if (in.color.isValid())
                        sp.color = in.color;
                    if (in.spacing > 0)
                        sp.spacing = in.spacing;
                    if (in.caps > 0)
                        sp.caps = in.caps;
                    fonts.append(sp);
                }
            } else if (t.tag == QLatin1String("u"))
                underline = qMax(0, underline + d);
            else if (t.tag == QLatin1String("sup"))
                sup = qMax(0, sup + d);
            else if (t.tag == QLatin1String("sub"))
                sub = qMax(0, sub + d);
            else if (t.tag == QLatin1String("mark"))
                highlight = qMax(0, highlight + d);
            else if (t.tag == QLatin1String("s"))
                strike = qMax(0, strike + d);
            else if (t.tag == QLatin1String("i") || t.tag == QLatin1String("em"))
                italic = qMax(0, italic + d);
            else if (t.tag == QLatin1String("b") || t.tag == QLatin1String("strong"))
                bold = qMax(0, bold + d);
            else if (t.tag.startsWith(QLatin1String("br")))
                emitText(QString(QChar::LineSeparator));
            else if (t.tag.startsWith(QLatin1String("rev")))
                revision = t.tagClose ? 0 : t.tag.mid(3).toInt();
            break;
        }
        case Tok::Image: {
            Run r;
            r.image = true;
            r.src = t.src;
            r.text = t.alt;
            r.width = t.width;
            r.height = t.height;
            r.fmt = current();
            runs.append(r);
            break;
        }
        }
    }
    return runs;
}

struct BlockInfo {
    QString style;
    QString content;
    int listType = 0; // 1 bullet, 2 numbered
    int listLevel = 0;
    bool centered = false;
    bool literal = false;
    direct::ParaSpec para; // "{: left=10mm ...}" prefix
    bool pageBreak = false; // "{: page=before}": the paragraph starts a new page
};

bool isSeparatorLine(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    for (const QChar c : t) {
        if (!(c == QLatin1Char('*') || c == QLatin1Char('-') || c == QLatin1Char('_') || c == QLatin1Char('#')
              || c == QLatin1Char('=') || c == QLatin1Char('~') || c == QLatin1Char(' ')
              || c == QLatin1Char('+') || c == QChar(0x2022)))
            return false;
    }
    return true;
}

BlockInfo parseBlock(const QString &line, const Formatting &f)
{
    // paragraph formatting set by hand: "{: align=right left=10mm} text"
    static const QRegularExpression paraRe(QStringLiteral("^\\{:((?:\\s+[a-z]+=[^\\s}]+)+)\\s*\\}\\s?(.*)$"));
    const QRegularExpressionMatch pm = paraRe.match(line);
    if (pm.hasMatch()) {
        BlockInfo inner = parseBlock(pm.captured(2), f);
        const direct::ParaSpec outer = direct::paraSpecFromText(pm.captured(1));
        if (!inner.para.bits)
            inner.para = outer;
        static const QRegularExpression pageRe(QStringLiteral("(^|\\s)page=before(\\s|$)"));
        if (pageRe.match(pm.captured(1)).hasMatch())
            inner.pageBreak = true;
        return inner;
    }
    BlockInfo bi;
    if (isSeparatorLine(line)) {
        bi.content = line;
        bi.literal = true;
        return bi;
    }
    static const QRegularExpression styleRe(QStringLiteral("^::([^:]{1,60})::\\s?(.*)$"));
    static const QRegularExpression headRe(QStringLiteral("^(#{1,6})\\s+(.*)$"));
    static const QRegularExpression quoteRe(QStringLiteral("^>\\s?(.*)$"));
    static const QRegularExpression bulletRe(QStringLiteral("^(\\s*)([-*+])\\s+(.*)$"));
    static const QRegularExpression numRe(QStringLiteral("^(\\s*)(\\d{1,9})[.)]\\s+(.*)$"));
    static const QRegularExpression centerRe(QStringLiteral("^->(.*)<-\\s*$"));

    QRegularExpressionMatch m;
    if ((m = styleRe.match(line)).hasMatch()) {
        bi.style = m.captured(1).trimmed();
        bi.content = m.captured(2);
        if (const Style *s = styles::find(f, bi.style))
            bi.style = s->name; // a former name gives the current one
        else
            bi.style.clear();
        return bi;
    }
    if ((m = headRe.match(line)).hasMatch()) {
        bi.style = styles::forHeadingLevel(m.captured(1).size());
        bi.content = m.captured(2);
        return bi;
    }
    if ((m = quoteRe.match(line)).hasMatch()) {
        bi.style = QStringLiteral("Block Quote");
        bi.content = m.captured(1);
        return bi;
    }
    if ((m = bulletRe.match(line)).hasMatch()) {
        bi.listType = 1;
        bi.listLevel = m.captured(1).size() / 2;
        bi.content = m.captured(3);
        return bi;
    }
    if ((m = numRe.match(line)).hasMatch()) {
        bi.listType = 2;
        bi.listLevel = m.captured(1).size() / 2;
        bi.content = m.captured(3);
        return bi;
    }
    if ((m = centerRe.match(line)).hasMatch()) {
        bi.centered = true;
        bi.content = m.captured(1).trimmed();
        return bi;
    }
    bi.content = line;
    return bi;
}

// ------------------------------------------------------------------ writer

enum Attr : int {
    A_B = 1, A_I = 2, A_S = 4, A_H = 8, A_U = 16, A_SUP = 32, A_SUB = 64,
    A_R1 = 128, A_R2 = 256, A_R3 = 512, A_R4 = 1024, A_R5 = 2048 // revision mode colours
};
const int kAttrOrder[] = {A_R1, A_R2, A_R3, A_R4, A_R5, A_B, A_I, A_S, A_H, A_U, A_SUP, A_SUB};

QString openMarker(int a)
{
    switch (a) {
    case A_B: return QStringLiteral("**");
    case A_I: return QStringLiteral("*");
    case A_S: return QStringLiteral("~~");
    case A_H: return QStringLiteral("==");
    case A_U: return QStringLiteral("<u>");
    case A_SUP: return QStringLiteral("<sup>");
    case A_SUB: return QStringLiteral("<sub>");
    case A_R1: return QStringLiteral("<rev1>");
    case A_R2: return QStringLiteral("<rev2>");
    case A_R3: return QStringLiteral("<rev3>");
    case A_R4: return QStringLiteral("<rev4>");
    case A_R5: return QStringLiteral("<rev5>");
    }
    return QString();
}

QString closeMarker(int a)
{
    switch (a) {
    case A_U: return QStringLiteral("</u>");
    case A_SUP: return QStringLiteral("</sup>");
    case A_SUB: return QStringLiteral("</sub>");
    case A_R1: return QStringLiteral("</rev1>");
    case A_R2: return QStringLiteral("</rev2>");
    case A_R3: return QStringLiteral("</rev3>");
    case A_R4: return QStringLiteral("</rev4>");
    case A_R5: return QStringLiteral("</rev5>");
    default: return openMarker(a);
    }
}

struct Seg {
    QString text;       // raw text (unescaped)
    bool image = false;
    QTextImageFormat imgFmt;
    int attrs = 0;
    NoteKind note = NoteKind::None;
    QString noteId;
    QString noteText;
    QColor noteColor;
    QString href;
    QString css;        // direct font formatting
    QString cls;        // character style
};

struct Item {
    enum T { Text, Open, Close, Raw } t;
    int attr = 0;
    QString s;
};

class Writer
{
public:
    Writer(const ImageSaver &saver, const Formatting &f) : m_saver(saver), m_f(f) {}

    QString block(const QTextBlock &b, const ParagraphStyle *ps, bool escapeBrackets)
    {
        m_escapeBrackets = escapeBrackets;
        QList<Seg> segs = segments(b, ps);
        return emitNotes(segs);
    }

private:
    const ImageSaver &m_saver;
    const Formatting &m_f;
    bool m_escapeBrackets = false;

    QList<Seg> segments(const QTextBlock &b, const ParagraphStyle *ps)
    {
        QList<Seg> segs;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid())
                continue;
            // what the styles give is not written: the style names say it
            const QTextCharFormat cf = styles::withoutStyles(f.charFormat(), ps, m_f);
            Seg base;
            if (const Style *cs = styles::findChar(m_f, f.charFormat().stringProperty(props::CharStyle)))
                base.cls = cs->name;
            if (cf.fontWeight() >= QFont::DemiBold)
                base.attrs |= A_B;
            if (cf.fontItalic())
                base.attrs |= A_I;
            if (cf.fontStrikeOut())
                base.attrs |= A_S;
            if (cf.property(props::Highlight).toBool())
                base.attrs |= A_H;
            if (cf.fontUnderline() && !cf.isAnchor())
                base.attrs |= A_U;
            if (cf.verticalAlignment() == QTextCharFormat::AlignSuperScript)
                base.attrs |= A_SUP;
            else if (cf.verticalAlignment() == QTextCharFormat::AlignSubScript)
                base.attrs |= A_SUB;
            const int rev = cf.intProperty(props::Revision);
            if (rev >= 1 && rev <= 5)
                base.attrs |= (A_R1 << (rev - 1));
            base.note = NoteKind(cf.property(props::NoteKind).toInt());
            if (base.note != NoteKind::None) {
                base.noteId = cf.property(props::NoteId).toString();
                base.noteText = cf.property(props::NoteText).toString();
                base.noteColor = cf.property(props::NoteColor).value<QColor>();
                if (base.note == NoteKind::Comment || base.note == NoteKind::InlineAnnotation)
                    base.attrs &= ~A_H;
            }
            if (cf.isAnchor())
                base.href = cf.anchorHref();
            if (!cf.isImageFormat())
                base.css = direct::toCss(direct::charSpec(cf));

            const QString txt = f.text();
            if (cf.isImageFormat()) {
                for (int i = 0; i < txt.size(); ++i) {
                    Seg s = base;
                    s.image = true;
                    s.imgFmt = cf.toImageFormat();
                    segs.append(s);
                }
                continue;
            }
            Seg s = base;
            s.text = txt;
            segs.append(s);
        }
        return segs;
    }

    static bool sameNote(const Seg &a, const Seg &b)
    {
        if (a.note != b.note)
            return false;
        if (a.note == NoteKind::None)
            return true;
        if (!a.noteId.isEmpty() || !b.noteId.isEmpty())
            return a.noteId == b.noteId;
        return a.noteText == b.noteText && a.noteColor == b.noteColor;
    }

    QString emitNotes(const QList<Seg> &segs)
    {
        QString out;
        int i = 0;
        while (i < segs.size()) {
            int j = i + 1;
            while (j < segs.size() && sameNote(segs[i], segs[j]))
                ++j;
            const QList<Seg> group = segs.mid(i, j - i);
            const Seg &s = segs[i];
            const QString inner = emitLinks(group);
            switch (s.note) {
            case NoteKind::None:
                out += inner;
                break;
            case NoteKind::Comment:
                out += QLatin1String("{==") + inner + QLatin1String("==}{>>")
                       + encodeNote(s.noteText, s.noteColor, defaultCommentColor()) + QLatin1String("<<}");
                break;
            case NoteKind::LinkedFootnote:
                out += QLatin1String("{==") + inner + QLatin1String("==}^[")
                       + encodeNote(s.noteText, QColor(), QColor()) + QLatin1String("]");
                break;
            case NoteKind::InlineFootnote:
                out += QLatin1String("^[") + inner + QLatin1String("]");
                break;
            case NoteKind::InlineAnnotation: {
                QString prefix;
                if (s.noteColor.isValid() && s.noteColor != defaultAnnotationColor())
                    prefix = QLatin1Char('@') + s.noteColor.name() + QLatin1Char(' ');
                out += QLatin1String("{>>") + prefix + inner + QLatin1String("<<}");
                break;
            }
            }
            i = j;
        }
        return out;
    }

    QString emitLinks(const QList<Seg> &segs)
    {
        QString out;
        int i = 0;
        while (i < segs.size()) {
            int j = i + 1;
            while (j < segs.size() && segs[j].href == segs[i].href)
                ++j;
            const QList<Seg> group = segs.mid(i, j - i);
            const QString inner = emitSpans(group);
            if (segs[i].href.isEmpty()) {
                out += inner;
            } else {
                QString h = segs[i].href;
                h.replace(QLatin1Char('('), QLatin1String("%28"));
                h.replace(QLatin1Char(')'), QLatin1String("%29"));
                h.replace(QLatin1Char(' '), QLatin1String("%20"));
                out += QLatin1Char('[') + inner + QLatin1String("](") + h + QLatin1Char(')');
            }
            i = j;
        }
        return out;
    }

    QString emitSpans(const QList<Seg> &segs)
    {
        QString out;
        int i = 0;
        while (i < segs.size()) {
            int j = i + 1;
            while (j < segs.size() && segs[j].css == segs[i].css && segs[j].cls == segs[i].cls)
                ++j;
            const QString inner = emitFormatting(segs.mid(i, j - i));
            if ((segs[i].css.isEmpty() && segs[i].cls.isEmpty()) || inner.isEmpty()) {
                out += inner;
            } else {
                out += QLatin1String("<span");
                if (!segs[i].cls.isEmpty())
                    out += QLatin1String(" class=\"") + segs[i].cls + QLatin1Char('"');
                if (!segs[i].css.isEmpty())
                    out += QLatin1String(" style=\"") + segs[i].css + QLatin1Char('"');
                out += QLatin1Char('>') + inner + QLatin1String("</span>");
            }
            i = j;
        }
        return out;
    }

    QString imageMarkdown(const Seg &s)
    {
        QString path = m_saver ? m_saver(s.imgFmt) : s.imgFmt.name();
        if (path.isEmpty())
            return QString();
        path.replace(QLatin1Char(' '), QLatin1String("%20"));
        QString md = QLatin1String("![](") + path + QLatin1Char(')');
        const int w = qRound(s.imgFmt.width());
        const int h = qRound(s.imgFmt.height());
        if (w > 0 || h > 0) {
            md += QLatin1Char('{');
            if (w > 0)
                md += QStringLiteral("width=%1").arg(w);
            if (h > 0)
                md += QStringLiteral("%1height=%2").arg(w > 0 ? QStringLiteral(" ") : QString()).arg(h);
            md += QLatin1Char('}');
        }
        return md;
    }

    QString emitFormatting(const QList<Seg> &segs)
    {
        QList<Item> items;
        QList<int> open;
        for (const Seg &s : segs) {
            int k = 0;
            while (k < open.size() && (s.attrs & open[k]))
                ++k;
            for (int x = open.size() - 1; x >= k; --x)
                items.append({Item::Close, open[x], QString()});
            open.erase(open.begin() + k, open.end());
            for (int a : kAttrOrder) {
                if ((s.attrs & a) && !open.contains(a)) {
                    items.append({Item::Open, a, QString()});
                    open.append(a);
                }
            }
            if (s.image)
                items.append({Item::Raw, 0, imageMarkdown(s)});
            else
                items.append({Item::Text, 0, s.text});
        }
        for (int x = open.size() - 1; x >= 0; --x)
            items.append({Item::Close, open[x], QString()});

        // move whitespace out of markers
        for (int i = 0; i < items.size(); ++i) {
            if (items[i].t != Item::Text)
                continue;
            // leading whitespace before a run of Open items
            int p = i - 1;
            while (p >= 0 && items[p].t == Item::Open)
                --p;
            if (p < i - 1) {
                int n = 0;
                while (n < items[i].s.size() && items[i].s.at(n).isSpace())
                    ++n;
                if (n > 0) {
                    const QString ws = items[i].s.left(n);
                    items[i].s.remove(0, n);
                    items.insert(p + 1, {Item::Text, 0, ws});
                    ++i;
                }
            }
            // trailing whitespace after a run of Close items
            int q = i + 1;
            while (q < items.size() && items[q].t == Item::Close)
                ++q;
            if (q > i + 1) {
                int n = 0;
                const QString &str = items[i].s;
                while (n < str.size() && str.at(str.size() - 1 - n).isSpace())
                    ++n;
                if (n > 0 && n < str.size()) {
                    const QString ws = str.right(n);
                    items[i].s.chop(n);
                    items.insert(q, {Item::Text, 0, ws});
                }
            }
        }
        // remove empty open/close pairs
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i + 1 < items.size(); ++i) {
                if (items[i].t == Item::Text && items[i].s.isEmpty()) {
                    items.removeAt(i);
                    changed = true;
                    break;
                }
                if (items[i].t == Item::Open && items[i + 1].t == Item::Close && items[i].attr == items[i + 1].attr) {
                    items.removeAt(i + 1);
                    items.removeAt(i);
                    changed = true;
                    break;
                }
            }
        }
        QString out;
        for (const Item &it : items) {
            switch (it.t) {
            case Item::Text: out += escapeInline(it.s, m_escapeBrackets); break;
            case Item::Raw: out += it.s; break;
            case Item::Open: out += openMarker(it.attr); break;
            case Item::Close: out += closeMarker(it.attr); break;
            }
        }
        return out;
    }
};

QString escapeBlockStart(const QString &body, const QString &raw)
{
    // Only needed for unstyled paragraphs: make sure the reader does not
    // mistake the text for a block prefix.
    int ws = 0;
    while (ws < body.size() && (body.at(ws) == QLatin1Char(' ') || body.at(ws) == QLatin1Char('\t')))
        ++ws;
    const QString rest = body.mid(ws);
    static const QRegularExpression headRe(QStringLiteral("^#{1,6}\\s"));
    static const QRegularExpression listRe(QStringLiteral("^[-+]\\s"));
    static const QRegularExpression numRe(QStringLiteral("^(\\d{1,9})([.)])\\s"));
    if (headRe.match(rest).hasMatch() || rest.startsWith(QLatin1Char('>')) || listRe.match(rest).hasMatch()
        || rest.startsWith(QLatin1String("::")) || rest.startsWith(QLatin1String("{:")))
        return body.left(ws) + QLatin1Char('\\') + rest;
    auto nm = numRe.match(rest);
    if (nm.hasMatch())
        return body.left(ws) + nm.captured(1) + QLatin1Char('\\') + rest.mid(nm.captured(1).size());
    if (rest.startsWith(QLatin1String("->")) && raw.trimmed().endsWith(QLatin1String("<-")))
        return body.left(ws) + QLatin1Char('\\') + rest;
    return body;
}

} // namespace

QString escapeInline(const QString &text, bool escapeBrackets)
{
    QString out;
    out.reserve(text.size() + 8);
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        const QChar next = (i + 1 < n) ? text.at(i + 1) : QChar();
        switch (c.unicode()) {
        case '\\':
        case '*':
        case '_':
            out += QLatin1Char('\\');
            out += c;
            break;
        case '~':
        case '=':
            if (next == c)
                out += QLatin1Char('\\');
            out += c;
            break;
        case '[':
            if (escapeBrackets)
                out += QLatin1Char('\\');
            out += c;
            break;
        case '^':
            if (next == QLatin1Char('['))
                out += QLatin1Char('\\');
            out += c;
            break;
        case '{':
            if (next == QLatin1Char('=') || next == QLatin1Char('>') || next == QLatin1Char('+')
                || next == QLatin1Char('-') || next == QLatin1Char('~'))
                out += QLatin1Char('\\');
            out += c;
            break;
        case '<': {
            static const char *tags[] = {"u>", "/u>", "sup>", "/sup>", "sub>", "/sub>", "mark>", "/mark>",
                                         "br", "s>", "/s>", "i>", "/i>", "b>", "/b>", "em>", "/em>",
                                         "strong>", "/strong>", "span ", "/span>"};
            bool esc = false;
            const QString tail = text.mid(i + 1, 8);
            for (const char *t : tags)
                if (tail.startsWith(QLatin1String(t))) {
                    esc = true;
                    break;
                }
            if (esc)
                out += QLatin1Char('\\');
            out += c;
            break;
        }
        case 0x2028:
            out += QLatin1String("<br>");
            break;
        case 0x2029:
        case '\n':
        case '\r':
            out += QLatin1String("<br>");
            break;
        default:
            if (c.unicode() < 32 && c != QLatin1Char('\t'))
                break;
            out += c;
        }
    }
    return out;
}

void read(const QString &markdown, QTextDocument *doc, const Formatting &f)
{
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    QString text = markdown;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    if (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    const QStringList lines = text.split(QLatin1Char('\n'));

    QTextCursor cur(doc);
    bool first = true;
    QMap<int, QTextList *> lists;
    QMap<int, int> listTypes;
    bool prevWasList = false;

    for (const QString &line : lines) {
        const BlockInfo bi = parseBlock(line, f);
        QTextBlockFormat bf = display::blockFormatFor(f, bi.style);
        if (bi.centered) {
            direct::ParaSpec c;
            c.bits = direct::Align;
            c.align = Qt::AlignHCenter;
            direct::applyParaSpec(bf, c);
        }
        direct::applyParaSpec(bf, bi.para);
        if (bi.pageBreak) {
            bf.setProperty(props::PageBreak, true);
            bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        }
        const int own = bf.intProperty(props::ParaDirect);
        if (!(own & direct::First)
            && (bi.listType || ((own & direct::Align) && (bf.alignment() & (Qt::AlignHCenter | Qt::AlignRight)))))
            bf.setTextIndent(0);
        const QTextCharFormat cf = display::charFormatFor(f, bi.style);
        if (first) {
            cur.setBlockFormat(bf);
            cur.setBlockCharFormat(cf);
            first = false;
        } else {
            cur.insertBlock(bf, cf);
        }
        if (bi.listType) {
            // drop deeper lists
            for (auto it = lists.begin(); it != lists.end();) {
                if (it.key() > bi.listLevel || !prevWasList)
                    it = lists.erase(it);
                else
                    ++it;
            }
            QTextList *l = lists.value(bi.listLevel, nullptr);
            if (l && listTypes.value(bi.listLevel) == bi.listType) {
                l->add(cur.block());
            } else {
                QTextListFormat lf;
                if (bi.listType == 2)
                    lf.setStyle(QTextListFormat::ListDecimal);
                else
                    lf.setStyle(bi.listLevel % 3 == 0 ? QTextListFormat::ListDisc
                                : bi.listLevel % 3 == 1 ? QTextListFormat::ListCircle
                                                        : QTextListFormat::ListSquare);
                lf.setIndent(bi.listLevel + 1);
                l = cur.createList(lf);
                lists[bi.listLevel] = l;
                listTypes[bi.listLevel] = bi.listType;
            }
            prevWasList = true;
        } else {
            lists.clear();
            prevWasList = false;
        }

        if (bi.literal) {
            cur.insertText(bi.content, cf);
            continue;
        }
        const QList<Run> runs = parseInline(bi.content, cf, f);
        for (const Run &r : runs) {
            if (r.image) {
                QTextImageFormat img;
                QString src = r.src;
                src.replace(QLatin1String("%20"), QLatin1String(" "));
                img.setName(src);
                if (r.width > 0)
                    img.setWidth(r.width);
                if (r.height > 0)
                    img.setHeight(r.height);
                if (!r.text.isEmpty())
                    img.setToolTip(r.text);
                cur.insertImage(img);
            } else {
                cur.insertText(r.text, r.fmt);
            }
        }
    }
    doc->setUndoRedoEnabled(undo);
    doc->setModified(false);
}

QString write(const QTextDocument *doc, const Formatting &f, const ImageSaver &saver)
{
    Writer w(saver, f);
    QStringList out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString style = b.blockFormat().property(props::Style).toString();
        const ParagraphStyle *ps = styles::find(f, style);
        const QString raw = b.text();
        QString prefix, suffix;
        QTextList *list = b.textList();
        bool unstyled = false;
        direct::ParaSpec own = direct::paraSpec(b.blockFormat());
        if (list) {
            const QTextListFormat lf = list->format();
            const int level = qMax(0, lf.indent() - 1);
            const auto st = lf.style();
            const bool numbered = st == QTextListFormat::ListDecimal || st == QTextListFormat::ListLowerAlpha
                                  || st == QTextListFormat::ListUpperAlpha
                                  || st == QTextListFormat::ListLowerRoman
                                  || st == QTextListFormat::ListUpperRoman;
            prefix = QString(level * 2, QLatin1Char(' '))
                     + (numbered ? QString::number(list->itemNumber(b) + 1) + QLatin1String(". ")
                                 : QStringLiteral("- "));
        } else if (ps && ps->headingLevel > 0) {
            prefix = QString(ps->headingLevel, QLatin1Char('#')) + QLatin1Char(' ');
        } else if (ps && ps->name == QLatin1String("Block Quote")) {
            prefix = QStringLiteral("> ");
        } else if (ps && ps->name == QLatin1String("Centered")) {
            prefix = QStringLiteral("->");
            suffix = QStringLiteral("<-");
        } else if (ps) {
            prefix = QLatin1String("::") + ps->name + QLatin1String(":: ");
        } else if (b.blockFormat().alignment() & Qt::AlignHCenter) {
            prefix = QStringLiteral("->");
            suffix = QStringLiteral("<-");
            own.bits &= ~direct::Align; // "->text<-" says it already
        } else {
            unstyled = true;
        }
        // a page break before the paragraph (from imported scripts and RTF files)
        const bool pageBreak = b.blockFormat().property(props::PageBreak).toBool()
                               || (b.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore);
        QStringList paraAttrs;
        if (own.bits)
            paraAttrs << direct::toText(own);
        if (pageBreak)
            paraAttrs << QStringLiteral("page=before");
        const QString paraPrefix = paraAttrs.isEmpty() ? QString()
                                                       : QLatin1String("{: ") + paraAttrs.join(QLatin1Char(' ')) + QLatin1String("} ");

        // Plain separator lines ("* * *", "#") are written verbatim.
        bool plainFormatting = true;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = styles::withoutStyles(it.fragment().charFormat(), ps, f);
            if (cf.fontWeight() >= QFont::DemiBold || cf.fontItalic() || cf.hasProperty(props::NoteKind)
                || it.fragment().charFormat().hasProperty(props::CharStyle)
                || cf.isAnchor() || cf.isImageFormat() || !direct::charSpec(cf).isEmpty())
                plainFormatting = false;
        }
        QString body;
        if (plainFormatting && isSeparatorLine(raw) && (unstyled || suffix == QLatin1String("<-"))) {
            body = raw;
        } else {
            const bool escBrackets = raw.contains(QLatin1String("](")) || raw.contains(QLatin1String("!["))
                                     || raw.contains(QLatin1String("^["));
            body = w.block(b, ps, escBrackets);
            if (unstyled)
                body = escapeBlockStart(body, raw);
            else if (!suffix.isEmpty())
                body = body.trimmed();
            else if (prefix.endsWith(QLatin1Char(' ')))
                ; // prefix already separates
        }
        out << paraPrefix + prefix + body + suffix;
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString toPlainText(const QString &markdown, const Formatting &f)
{
    QTextDocument doc;
    read(markdown, &doc, f);
    return text::plainText(&doc);
}

QString fromStandardMarkdown(const QString &standard)
{
    QString text = standard;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList out;
    QString para;
    static const QRegularExpression blockStart(
        QStringLiteral("^(\\s*([-*+]|\\d{1,9}[.)])\\s+|#{1,6}\\s|>|->.*<-\\s*$|::[^:]+::)"));
    bool inFence = false;
    auto flush = [&] {
        if (!para.isEmpty()) {
            out << para;
            para.clear();
        }
    };
    for (const QString &rawLine : lines) {
        QString line = rawLine;
        if (line.trimmed().startsWith(QLatin1String("```"))) {
            flush();
            inFence = !inFence;
            continue;
        }
        if (inFence) {
            out << escapeInline(line, false);
            continue;
        }
        if (line.trimmed().isEmpty()) {
            flush();
            continue;
        }
        const bool hardBreak = line.endsWith(QLatin1String("  "));
        if (blockStart.match(line).hasMatch() || isSeparatorLine(line)) {
            flush();
            para = line.trimmed().isEmpty() ? QString() : line;
            if (line.startsWith(QLatin1Char('#')) || isSeparatorLine(line))
                flush();
        } else if (para.isEmpty()) {
            para = line.trimmed();
        } else {
            para += (para.endsWith(QLatin1String("<br>")) ? QString() : QStringLiteral(" ")) + line.trimmed();
        }
        if (hardBreak && !para.isEmpty())
            para += QStringLiteral("<br>");
    }
    flush();
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString toStandardMarkdown(const QString &dialect)
{
    QString text = dialect;
    if (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    const QStringList lines = text.split(QLatin1Char('\n'));
    static const QRegularExpression listRe(QStringLiteral("^\\s*([-*+]|\\d{1,9}[.)])\\s+"));
    static const QRegularExpression styleRe(QStringLiteral("^::[^:]{1,60}::\\s?"));
    QString out;
    bool prevList = false;
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        line.remove(styleRe);
        const bool isList = listRe.match(line).hasMatch();
        if (i > 0)
            out += (isList && prevList) ? QStringLiteral("\n") : QStringLiteral("\n\n");
        if (line.trimmed().isEmpty())
            line = QStringLiteral("&nbsp;");
        out += line;
        prevList = isList;
    }
    return out + QLatin1Char('\n');
}

QString fromPlainText(const QString &plain)
{
    QStringList lines = QString(plain).replace(QLatin1String("\r\n"), QLatin1String("\n")).split(QLatin1Char('\n'));
    for (QString &line : lines)
        line = escapeBlockStart(escapeInline(line, true), line);
    return lines.join(QLatin1Char('\n'));
}

} // namespace md
} // namespace qb
