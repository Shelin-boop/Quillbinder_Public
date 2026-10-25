#include "text/ScriptIO.h"

#include "compile/ExporterApi.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QDomDocument>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <functional>

namespace qb {
namespace script {

QString Para::text() const
{
    QString t;
    for (const Span &s : spans)
        if (!s.note)
            t += s.text;
    return t;
}

bool Para::isEmpty() const
{
    for (const Span &s : spans)
        if (!s.text.trimmed().isEmpty())
            return false;
    return true;
}

QString Screenplay::titleValue(const QString &key) const
{
    for (const auto &kv : titlePage)
        if (kv.first.compare(key, Qt::CaseInsensitive) == 0)
            return kv.second;
    return QString();
}

QString titlePageText(const Screenplay &s)
{
    QStringList lines;
    for (const auto &kv : s.titlePage)
        lines << kv.first + QLatin1String(": ") + QString(kv.second).replace(QLatin1Char('\n'), QLatin1String(" / "));
    return lines.join(QLatin1Char('\n'));
}

namespace {

// ------------------------------------------------------------------ helpers

bool hasLetter(const QString &t)
{
    for (const QChar c : t)
        if (c.isLetter())
            return true;
    return false;
}

bool isUpper(const QString &t) { return hasLetter(t) && t == t.toUpper(); }

const QRegularExpression &sceneRe()
{
    static const QRegularExpression re(QStringLiteral("^(INT\\.?/EXT|EXT\\.?/INT|INT|EXT|EST|I/E)[\\. ]"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// "ANNA", "ANNA (V.O.)", "ANNA (cont'd) ^"
bool isCharacterLine(QString t)
{
    t = t.trimmed();
    if (t.endsWith(QLatin1Char('^')))
        t.chop(1);
    static const QRegularExpression ext(QStringLiteral("\\s*\\([^)]*\\)\\s*$"));
    t.remove(ext);
    t = t.trimmed();
    return !t.isEmpty() && isUpper(t);
}

bool isMarkerChar(QChar c)
{
    return c == QLatin1Char('.') || c == QLatin1Char('>') || c == QLatin1Char('#') || c == QLatin1Char('=')
           || c == QLatin1Char('~') || c == QLatin1Char('@') || c == QLatin1Char('!');
}

// Emphasis and notes of one line: *italic*, **bold**, ***both***,
// _underline_, [[note]], \ escapes.
QList<Span> parseInline(const QString &line)
{
    QList<Span> out;
    bool bold = false, italic = false, underline = false;
    QString buf;
    auto flush = [&](bool note = false) {
        if (buf.isEmpty())
            return;
        Span s;
        s.text = buf;
        s.bold = bold;
        s.italic = italic;
        s.underline = underline;
        s.note = note;
        if (note)
            s.bold = s.italic = s.underline = false;
        out.append(s);
        buf.clear();
    };
    // a marker opens only if it closes again later in the line
    auto closes = [&](int from, const QString &marker) {
        int at = from;
        while ((at = line.indexOf(marker, at)) >= 0) {
            if (at > 0 && line.at(at - 1) == QLatin1Char('\\')) {
                ++at;
                continue;
            }
            // "**" must not be the start of "***" when looking for "*"
            if (at > from && !line.at(at - 1).isSpace())
                return true;
            ++at;
        }
        return false;
    };
    const int n = int(line.size());
    for (int i = 0; i < n;) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\') && i + 1 < n) {
            buf += line.at(i + 1);
            i += 2;
            continue;
        }
        if (c == QLatin1Char('[') && i + 1 < n && line.at(i + 1) == QLatin1Char('[')) {
            const int end = line.indexOf(QLatin1String("]]"), i + 2);
            if (end > 0) {
                flush();
                buf = line.mid(i + 2, end - i - 2).trimmed();
                flush(true);
                i = end + 2;
                continue;
            }
        }
        if (c == QLatin1Char('*')) {
            int run = 1;
            while (i + run < n && line.at(i + run) == QLatin1Char('*') && run < 3)
                ++run;
            const QString marker(run, QLatin1Char('*'));
            const bool wantBold = run >= 2, wantItalic = run == 1 || run == 3;
            const bool closing = (!wantBold || bold) && (!wantItalic || italic) && !buf.isEmpty()
                                 && !buf.at(buf.size() - 1).isSpace();
            const bool opening = !closing && i + run < n && !line.at(i + run).isSpace()
                                 && closes(i + run, marker);
            if (closing || opening) {
                flush();
                if (wantBold)
                    bold = opening;
                if (wantItalic)
                    italic = opening;
                i += run;
                continue;
            }
            buf += marker;
            i += run;
            continue;
        }
        if (c == QLatin1Char('_')) {
            const bool closing = underline && !buf.isEmpty() && !buf.at(buf.size() - 1).isSpace();
            const bool boundary = i == 0 || !line.at(i - 1).isLetterOrNumber();
            const bool opening = !underline && boundary && i + 1 < n && !line.at(i + 1).isSpace()
                                 && closes(i + 1, QStringLiteral("_"));
            if (closing || opening) {
                flush();
                underline = opening;
                ++i;
                continue;
            }
        }
        buf += c;
        ++i;
    }
    flush();
    return out;
}

QList<Span> parseLines(const QStringList &lines)
{
    QList<Span> out;
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            Span br;
            br.text = QStringLiteral("\n");
            out.append(br);
        }
        out << parseInline(lines.at(i));
    }
    return out;
}

QString escapeFountain(const QString &t)
{
    QString out;
    for (const QChar c : t) {
        if (c == QLatin1Char('*') || c == QLatin1Char('_') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

// The text of a paragraph in Fountain markup, one string per line.
QStringList fountainLines(const Para &p, bool upper)
{
    QString all;
    for (const Span &s : p.spans) {
        if (s.text.isEmpty())
            continue;
        if (s.note) {
            all += QLatin1String("[[") + QString(s.text).replace(QLatin1Char('\n'), QLatin1Char(' ')) + QLatin1String("]]");
            continue;
        }
        // markers enclose text on one line, without spaces at their inner edges
        const QStringList parts = s.text.split(QLatin1Char('\n'));
        for (int i = 0; i < parts.size(); ++i) {
            if (i > 0)
                all += QLatin1Char('\n');
            QString t = upper ? parts.at(i).toUpper() : parts.at(i);
            if (t.isEmpty())
                continue;
            int lead = 0, trail = 0;
            while (lead < t.size() && t.at(lead).isSpace())
                ++lead;
            while (trail < t.size() - lead && t.at(t.size() - 1 - trail).isSpace())
                ++trail;
            const QString core = escapeFountain(t.mid(lead, t.size() - lead - trail));
            QString open, close;
            if (!core.isEmpty()) {
                if (s.underline) {
                    open += QLatin1Char('_');
                    close.prepend(QLatin1Char('_'));
                }
                const QString stars(s.bold && s.italic ? 3 : s.bold ? 2 : s.italic ? 1 : 0, QLatin1Char('*'));
                open += stars;
                close.prepend(stars);
            }
            all += t.left(lead) + open + core + close + t.right(trail);
        }
    }
    return all.split(QLatin1Char('\n'));
}

QString xmlEscape(const QString &t)
{
    QString o;
    for (const QChar c : t) {
        switch (c.unicode()) {
        case '&': o += QLatin1String("&amp;"); break;
        case '<': o += QLatin1String("&lt;"); break;
        case '>': o += QLatin1String("&gt;"); break;
        case '"': o += QLatin1String("&quot;"); break;
        default:
            if (c.unicode() >= 32 || c == QLatin1Char('\t') || c == QLatin1Char('\n'))
                o += c;
        }
    }
    return o;
}

struct FdxType {
    Role role;
    const char *name;
};
const FdxType kFdxTypes[] = {
    {Role::SceneHeading, "Scene Heading"}, {Role::Action, "Action"},         {Role::Character, "Character"},
    {Role::Parenthetical, "Parenthetical"}, {Role::Dialogue, "Dialogue"},    {Role::Transition, "Transition"},
    {Role::Shot, "Shot"},                   {Role::General, "General"},      {Role::Lyrics, "Lyrics"},
    {Role::Section, "New Act"},             {Role::Centered, "Action"},
};

} // namespace

// ------------------------------------------------------------------ Fountain: read

Screenplay fromFountain(const QString &input)
{
    Screenplay sp;
    QString text = input;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    // boneyard: text the writer has commented out
    static const QRegularExpression boneyard(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    text.remove(boneyard);
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int n = int(lines.size());
    int i = 0;

    // title page: "Key: value" lines at the very top, values may go on in indented lines
    static const QRegularExpression keyRe(QStringLiteral("^([A-Za-z][A-Za-z ]{0,30}):\\s*(.*)$"));
    static const QStringList knownKeys = {QStringLiteral("title"),   QStringLiteral("credit"),    QStringLiteral("author"),
                                          QStringLiteral("authors"), QStringLiteral("source"),    QStringLiteral("draft date"),
                                          QStringLiteral("contact"), QStringLiteral("copyright"), QStringLiteral("notes"),
                                          QStringLiteral("revision"), QStringLiteral("date"),     QStringLiteral("format")};
    {
        const QRegularExpressionMatch first = n > 0 ? keyRe.match(lines.at(0)) : QRegularExpressionMatch();
        if (first.hasMatch() && knownKeys.contains(first.captured(1).trimmed().toLower())) {
            while (i < n && !lines.at(i).trimmed().isEmpty()) {
                const QString &l = lines.at(i);
                const QRegularExpressionMatch m = keyRe.match(l);
                if (m.hasMatch() && !l.at(0).isSpace()) {
                    sp.titlePage.append({m.captured(1).trimmed(), m.captured(2).trimmed()});
                } else if (!sp.titlePage.isEmpty()) {
                    QString &v = sp.titlePage.last().second;
                    v += (v.isEmpty() ? QString() : QStringLiteral("\n")) + l.trimmed();
                }
                ++i;
            }
            // emphasis markers around title page values are dropped
            for (auto &kv : sp.titlePage) {
                QString v;
                for (const Span &s : parseLines(kv.second.split(QLatin1Char('\n'))))
                    if (!s.note)
                        v += s.text;
                kv.second = v;
            }
        }
    }

    auto blankAt = [&](int k) { return k < 0 || k >= n || lines.at(k).trimmed().isEmpty(); };
    bool inDialogue = false;
    bool pendingBreak = false;
    int lastActionLine = -2;   // line of the last action text (consecutive lines are one paragraph)
    int lastDialogueLine = -2;
    QStringList collected;     // lines of the paragraph being built (action, dialogue)
    auto add = [&](Role role, const QStringList &text, int depth = 1) -> Para & {
        Para p;
        p.role = role;
        p.depth = depth;
        p.spans = parseLines(text);
        p.pageBreak = pendingBreak;
        pendingBreak = false;
        sp.paras.append(p);
        return sp.paras.last();
    };
    static const QRegularExpression pageBreakRe(QStringLiteral("^={3,}$"));
    static const QRegularExpression sceneNumberRe(QStringLiteral("\\s*#[^#]+#\\s*$"));

    for (; i < n; ++i) {
        const QString raw = lines.at(i);
        const QString t = raw.trimmed();
        if (t.isEmpty()) {
            inDialogue = false;
            continue;
        }
        if (pageBreakRe.match(t).hasMatch()) {
            pendingBreak = true;
            inDialogue = false;
            continue;
        }
        if (inDialogue) {
            if (t.startsWith(QLatin1Char('(')) && t.endsWith(QLatin1Char(')'))) {
                add(Role::Parenthetical, {t});
            } else if (t.startsWith(QLatin1Char('~'))) {
                add(Role::Lyrics, {t.mid(1).trimmed()});
            } else if (lastDialogueLine == i - 1 && !sp.paras.isEmpty() && sp.paras.last().role == Role::Dialogue) {
                collected << t;
                sp.paras.last().spans = parseLines(collected);
                lastDialogueLine = i;
            } else {
                collected = QStringList{t};
                add(Role::Dialogue, collected);
                lastDialogueLine = i;
            }
            continue;
        }
        if (t.startsWith(QLatin1Char('=')) && !t.startsWith(QLatin1String("=="))) {
            // synopsis line: a note of the writer
            Para &p = add(Role::Action, {});
            Span s;
            s.text = t.mid(1).trimmed();
            s.note = true;
            p.spans = {s};
            continue;
        }
        if (t.startsWith(QLatin1Char('#'))) {
            int depth = 0;
            while (depth < t.size() && t.at(depth) == QLatin1Char('#'))
                ++depth;
            add(Role::Section, {t.mid(depth).trimmed()}, depth);
            continue;
        }
        if (t.startsWith(QLatin1Char('.')) && t.size() > 1 && t.at(1) != QLatin1Char('.')) {
            add(Role::SceneHeading, {QString(t.mid(1)).remove(sceneNumberRe).trimmed()});
            continue;
        }
        if (t.startsWith(QLatin1Char('!'))) {
            QString a = raw;
            a.remove(a.indexOf(QLatin1Char('!')), 1);
            while (!a.isEmpty() && a.at(a.size() - 1).isSpace())
                a.chop(1);
            if (lastActionLine == i - 1 && !sp.paras.isEmpty() && sp.paras.last().role == Role::Action) {
                collected << a;
                sp.paras.last().spans = parseLines(collected);
            } else {
                collected = QStringList{a};
                add(Role::Action, collected);
            }
            lastActionLine = i;
            continue;
        }
        const bool before = blankAt(i - 1), after = blankAt(i + 1);
        if (before && after && sceneRe().match(t).hasMatch()) {
            add(Role::SceneHeading, {QString(t).remove(sceneNumberRe).trimmed()});
            continue;
        }
        if (t.startsWith(QLatin1Char('>')) && t.endsWith(QLatin1Char('<')) && t.size() >= 2) {
            add(Role::Centered, {t.mid(1, t.size() - 2).trimmed()});
            continue;
        }
        if (t.startsWith(QLatin1Char('>'))) {
            add(Role::Transition, {t.mid(1).trimmed()});
            continue;
        }
        if (before && after && isUpper(t) && t.endsWith(QLatin1String("TO:"))) {
            add(Role::Transition, {t});
            continue;
        }
        if (t.startsWith(QLatin1Char('@')) && t.size() > 1) {
            QString name = t.mid(1).trimmed();
            if (name.endsWith(QLatin1Char('^')))
                name = name.left(name.size() - 1).trimmed();
            add(Role::Character, {name});
            inDialogue = true;
            lastDialogueLine = -2;
            continue;
        }
        if (before && !after && lastActionLine != i - 1 && isCharacterLine(t)) {
            QString name = t;
            if (name.endsWith(QLatin1Char('^')))
                name = name.left(name.size() - 1).trimmed();
            add(Role::Character, {name});
            inDialogue = true;
            lastDialogueLine = -2;
            continue;
        }
        if (t.startsWith(QLatin1Char('~'))) {
            add(Role::Lyrics, {t.mid(1).trimmed()});
            continue;
        }
        // action; lines that follow each other without a blank line are one paragraph
        QString a = raw;
        while (!a.isEmpty() && a.at(a.size() - 1).isSpace())
            a.chop(1);
        a.replace(QLatin1Char('\t'), QLatin1String("    "));
        if (lastActionLine == i - 1 && !sp.paras.isEmpty() && sp.paras.last().role == Role::Action) {
            collected << a;
            sp.paras.last().spans = parseLines(collected);
        } else {
            collected = QStringList{a};
            add(Role::Action, collected);
        }
        lastActionLine = i;
    }
    return sp;
}

// ------------------------------------------------------------------ Fountain: write

QString toFountain(const Screenplay &sp)
{
    QString out;
    if (!sp.titlePage.isEmpty()) {
        for (const auto &kv : sp.titlePage) {
            const QStringList vals = kv.second.split(QLatin1Char('\n'));
            if (vals.size() == 1) {
                out += kv.first + QLatin1String(": ") + vals.first() + QLatin1Char('\n');
            } else {
                out += kv.first + QLatin1String(":\n");
                for (const QString &v : vals)
                    out += QLatin1String("    ") + v + QLatin1Char('\n');
            }
        }
        out += QLatin1Char('\n');
    }
    Role prev = Role::General;
    bool first = true;
    for (const Para &p : sp.paras) {
        if (p.isEmpty())
            continue;
        // character names keep their case: a name that is not all capitals is marked with "@"
        const bool upper = p.role == Role::SceneHeading || p.role == Role::Transition || p.role == Role::Shot;
        QStringList lines = fountainLines(p, upper);
        const QString plain = (upper ? p.text().toUpper() : p.text()).trimmed();
        // character, parenthetical and dialogue follow each other without blank lines
        const bool speech = p.role == Role::Parenthetical || p.role == Role::Dialogue || p.role == Role::Lyrics;
        const bool afterSpeaker = prev == Role::Character || prev == Role::Parenthetical || prev == Role::Dialogue
                                  || prev == Role::Lyrics;
        const bool glued = speech && afterSpeaker && !p.pageBreak;
        if (!first && !glued)
            out += QLatin1Char('\n');
        if (p.pageBreak && !first)
            out += QLatin1String("===\n\n");
        switch (p.role) {
        case Role::SceneHeading:
            for (int i = 1; i < lines.size(); ++i)
                lines[0] += QLatin1Char(' ') + lines.at(i);
            lines = QStringList{sceneRe().match(plain).hasMatch() ? lines.first() : QLatin1Char('.') + lines.first()};
            break;
        case Role::Character:
            for (int i = 1; i < lines.size(); ++i)
                lines[0] += QLatin1Char(' ') + lines.at(i);
            lines = QStringList{isCharacterLine(plain) ? lines.first() : QLatin1Char('@') + lines.first()};
            break;
        case Role::Parenthetical:
            for (int i = 1; i < lines.size(); ++i)
                lines[0] += QLatin1Char(' ') + lines.at(i);
            lines = QStringList{lines.first().trimmed()};
            if (!lines.first().startsWith(QLatin1Char('(')))
                lines[0].prepend(QLatin1Char('('));
            if (!lines.first().endsWith(QLatin1Char(')')))
                lines[0].append(QLatin1Char(')'));
            break;
        case Role::Dialogue:
            break; // a speech without a speaker in front is action text to other programs
        case Role::Transition:
            for (int i = 1; i < lines.size(); ++i)
                lines[0] += QLatin1Char(' ') + lines.at(i);
            lines = QStringList{plain.endsWith(QLatin1String("TO:")) ? lines.first() : QLatin1String("> ") + lines.first()};
            break;
        case Role::Centered:
            for (QString &l : lines)
                l = QLatin1String("> ") + l.trimmed() + QLatin1String(" <");
            break;
        case Role::Lyrics:
            for (QString &l : lines)
                l = QLatin1Char('~') + l;
            break;
        case Role::Section:
            for (int i = 1; i < lines.size(); ++i)
                lines[0] += QLatin1Char(' ') + lines.at(i);
            lines = QStringList{QString(qBound(1, p.depth, 6), QLatin1Char('#')) + QLatin1Char(' ') + lines.first()};
            break;
        case Role::Shot:
        case Role::Action:
        case Role::General: {
            // text that would be read as something else is marked as action
            const QString firstLine = plain.section(QLatin1Char('\n'), 0, 0);
            const bool single = lines.size() == 1;
            bool force = sceneRe().match(firstLine).hasMatch()
                         || (single && isUpper(firstLine) && firstLine.endsWith(QLatin1String("TO:")))
                         || (!single && isCharacterLine(firstLine));
            for (int i = 0; i < lines.size(); ++i) {
                const QString lt = lines.at(i).trimmed();
                if ((i == 0 && force) || (!lt.isEmpty() && isMarkerChar(lt.at(0)) && !lt.startsWith(QLatin1String("..."))))
                    lines[i].prepend(QLatin1Char('!'));
            }
            break;
        }
        }
        out += lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
        prev = p.role;
        first = false;
    }
    return out;
}

// ------------------------------------------------------------------ Final Draft

bool fromFdx(const QByteArray &data, Screenplay *out, QString *error)
{
    QDomDocument dom;
    QString msg;
    if (!dom.setContent(data, &msg) || dom.documentElement().tagName() != QLatin1String("FinalDraft")) {
        if (error)
            *error = QObject::tr("This is not a Final Draft file (.fdx).");
        return false;
    }
    Screenplay sp;
    auto readParagraph = [](const QDomElement &pe) {
        Para p;
        const QString type = pe.attribute(QStringLiteral("Type"));
        p.role = Role::Action;
        for (const FdxType &ft : kFdxTypes)
            if (type.compare(QLatin1String(ft.name), Qt::CaseInsensitive) == 0) {
                p.role = ft.role;
                break;
            }
        if (type.compare(QLatin1String("End of Act"), Qt::CaseInsensitive) == 0)
            p.role = Role::Transition;
        if (type.compare(QLatin1String("Cast List"), Qt::CaseInsensitive) == 0)
            p.role = Role::General;
        if (type.compare(QLatin1String("Singing"), Qt::CaseInsensitive) == 0)
            p.role = Role::Lyrics;
        if (pe.attribute(QStringLiteral("Alignment")) == QLatin1String("Center")
            && (p.role == Role::Action || p.role == Role::General))
            p.role = Role::Centered;
        p.pageBreak = pe.attribute(QStringLiteral("StartsNewPage")) == QLatin1String("Yes");
        for (QDomElement te = pe.firstChildElement(QStringLiteral("Text")); !te.isNull();
             te = te.nextSiblingElement(QStringLiteral("Text"))) {
            Span s;
            s.text = te.text();
            const QString style = te.attribute(QStringLiteral("Style"));
            s.bold = style.contains(QLatin1String("Bold"));
            s.italic = style.contains(QLatin1String("Italic"));
            s.underline = style.contains(QLatin1String("Underline"));
            if (style.contains(QLatin1String("AllCaps")))
                s.text = s.text.toUpper();
            if (!s.text.isEmpty())
                p.spans.append(s);
        }
        return p;
    };
    std::function<void(const QDomElement &, QList<Para> &)> readContent = [&](const QDomElement &parent, QList<Para> &into) {
        for (QDomElement pe = parent.firstChildElement(); !pe.isNull(); pe = pe.nextSiblingElement()) {
            if (pe.tagName() != QLatin1String("Paragraph"))
                continue;
            const QDomElement dual = pe.firstChildElement(QStringLiteral("DualDialogue"));
            if (!dual.isNull()) {
                readContent(dual, into); // both speakers, one after the other
                continue;
            }
            into.append(readParagraph(pe));
        }
    };
    readContent(dom.documentElement().firstChildElement(QStringLiteral("Content")), sp.paras);
    // empty paragraphs are only spacing
    for (int i = int(sp.paras.size()) - 1; i >= 0; --i)
        if (sp.paras.at(i).isEmpty()) {
            if (sp.paras.at(i).pageBreak && i + 1 < sp.paras.size())
                sp.paras[i + 1].pageBreak = true;
            sp.paras.removeAt(i);
        }
    // title page: the first line is the title, the line after "by" the author
    QList<Para> titleParas;
    readContent(dom.documentElement().firstChildElement(QStringLiteral("TitlePage")).firstChildElement(QStringLiteral("Content")),
                titleParas);
    QStringList titleLines;
    for (const Para &p : titleParas)
        if (!p.isEmpty())
            titleLines << p.text().trimmed();
    if (!titleLines.isEmpty()) {
        sp.titlePage.append({QStringLiteral("Title"), titleLines.takeFirst()});
        static const QRegularExpression byRe(QStringLiteral("^(written\\s+)?by:?$"), QRegularExpression::CaseInsensitiveOption);
        QStringList rest;
        for (int i = 0; i < titleLines.size(); ++i) {
            if (byRe.match(titleLines.at(i)).hasMatch() && i + 1 < titleLines.size()) {
                sp.titlePage.append({QStringLiteral("Credit"), titleLines.at(i)});
                sp.titlePage.append({QStringLiteral("Author"), titleLines.at(++i)});
            } else {
                rest << titleLines.at(i);
            }
        }
        if (!rest.isEmpty())
            sp.titlePage.append({QStringLiteral("Contact"), rest.join(QLatin1Char('\n'))});
    }
    *out = sp;
    return true;
}

QByteArray toFdx(const Screenplay &sp)
{
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n"
                               "<FinalDraft DocumentType=\"Script\" Template=\"No\" Version=\"1\">\n");
    auto paragraph = [](const Para &p, const QString &type, const QString &extra = QString()) {
        QString o = QStringLiteral("    <Paragraph");
        if (p.role == Role::Centered)
            o += QStringLiteral(" Alignment=\"Center\"");
        o += extra;
        if (p.pageBreak)
            o += QStringLiteral(" StartsNewPage=\"Yes\"");
        o += QStringLiteral(" Type=\"") + type + QStringLiteral("\">\n");
        // (character names keep the case they were written in: "McALLISTER")
        const bool upper = p.role == Role::SceneHeading || p.role == Role::Transition || p.role == Role::Shot;
        bool any = false;
        // runs of the same look are one <Text>; a line break alone would be lost
        // as white space, it joins the run before it
        QList<Span> runs;
        for (const Span &s : p.spans) {
            if (s.note || s.text.isEmpty())
                continue; // notes of the writer stay in the project
            const bool onlyBreaks = s.text.trimmed().isEmpty();
            if (!runs.isEmpty() && (onlyBreaks || (runs.last().bold == s.bold && runs.last().italic == s.italic
                                                   && runs.last().underline == s.underline)))
                runs.last().text += s.text;
            else
                runs.append(s);
        }
        for (const Span &s : runs) {
            QStringList st;
            if (s.bold)
                st << QStringLiteral("Bold");
            if (s.italic)
                st << QStringLiteral("Italic");
            if (s.underline)
                st << QStringLiteral("Underline");
            o += QStringLiteral("      <Text");
            if (!st.isEmpty())
                o += QStringLiteral(" Style=\"") + st.join(QLatin1Char('+')) + QLatin1Char('"');
            o += QLatin1Char('>') + xmlEscape(upper ? s.text.toUpper() : s.text) + QStringLiteral("</Text>\n");
            any = true;
        }
        if (!any)
            o += QStringLiteral("      <Text></Text>\n");
        o += QStringLiteral("    </Paragraph>\n");
        return o;
    };
    x += QStringLiteral("  <Content>\n");
    for (const Para &p : sp.paras) {
        if (p.isEmpty() || p.text().trimmed().isEmpty())
            continue;
        QString type = QStringLiteral("Action");
        for (const FdxType &ft : kFdxTypes)
            if (ft.role == p.role) {
                type = QLatin1String(ft.name);
                break;
            }
        x += paragraph(p, type);
    }
    x += QStringLiteral("  </Content>\n");
    const QString title = sp.titleValue(QStringLiteral("Title"));
    if (!title.isEmpty()) {
        x += QStringLiteral("  <TitlePage>\n  <Content>\n");
        auto line = [&](const QString &text) {
            Para p;
            p.role = Role::Centered;
            Span s;
            s.text = text;
            p.spans = {s};
            x += paragraph(p, QStringLiteral("General"));
        };
        for (int i = 0; i < 12; ++i)
            x += QStringLiteral("    <Paragraph Type=\"General\">\n      <Text></Text>\n    </Paragraph>\n");
        line(title);
        QString author = sp.titleValue(QStringLiteral("Author"));
        if (author.isEmpty())
            author = sp.titleValue(QStringLiteral("Authors"));
        if (!author.isEmpty()) {
            x += QStringLiteral("    <Paragraph Type=\"General\">\n      <Text></Text>\n    </Paragraph>\n");
            const QString credit = sp.titleValue(QStringLiteral("Credit"));
            line(credit.isEmpty() ? QStringLiteral("Written by") : credit);
            x += QStringLiteral("    <Paragraph Type=\"General\">\n      <Text></Text>\n    </Paragraph>\n");
            line(author);
        }
        x += QStringLiteral("  </Content>\n  </TitlePage>\n");
    }
    x += QStringLiteral("</FinalDraft>\n");
    return x.toUtf8();
}

// ------------------------------------------------------------------ documents

Screenplay fromDocument(const QTextDocument *doc, const Formatting &f)
{
    Screenplay sp;
    if (!doc)
        return sp;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        const QString styleName = bf.stringProperty(props::Style);
        const Element *el = element(f, styleName);
        const Style *style = styles::find(f, styleName);
        Para p;
        if (el) {
            p.role = el->role;
        } else if (style && style->headingLevel > 0) {
            p.role = Role::Section;
            p.depth = style->headingLevel;
        } else if ((bf.alignment() & Qt::AlignHCenter) || styleName == QLatin1String("Centered")
                   || styleName == QLatin1String("Title") || styleName == QLatin1String("Subtitle")) {
            p.role = Role::Centered;
        } else {
            p.role = Role::Action;
        }
        p.pageBreak = bf.property(props::PageBreak).toBool() || (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore);
        const bool caps = el && el->allCaps;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.isValid())
                continue;
            const QTextCharFormat cf = fr.charFormat();
            if (cf.isImageFormat())
                continue;
            Span s;
            s.text = fr.text();
            s.text.replace(QChar(QChar::LineSeparator), QLatin1Char('\n'));
            s.text.remove(QChar(QChar::ObjectReplacementCharacter));
            if (s.text.isEmpty())
                continue;
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            s.note = isInlineNote(kind);
            if (!s.note) {
                // what the element itself makes bold or italic is not emphasis
                s.bold = cf.fontWeight() >= QFont::DemiBold && !(style && style->bold);
                s.italic = cf.fontItalic() && !(style && style->italic);
                s.underline = cf.fontUnderline() && !cf.isAnchor() && !(style && style->underline);
                if (caps || cf.fontCapitalization() == QFont::AllUppercase)
                    s.text = s.text.toUpper();
            }
            if (!p.spans.isEmpty() && p.spans.last().note == s.note && p.spans.last().bold == s.bold
                && p.spans.last().italic == s.italic && p.spans.last().underline == s.underline)
                p.spans.last().text += s.text;
            else
                p.spans.append(s);
        }
        if (p.isEmpty()) {
            // an empty paragraph is spacing; a page break moves to the next text
            if (p.pageBreak && b.next().isValid()) {
                Para marker;
                marker.pageBreak = true;
                sp.paras.append(marker);
            }
            continue;
        }
        if (!sp.paras.isEmpty() && sp.paras.last().spans.isEmpty()) {
            p.pageBreak = true;
            sp.paras.removeLast();
        }
        sp.paras.append(p);
    }
    while (!sp.paras.isEmpty() && sp.paras.last().spans.isEmpty())
        sp.paras.removeLast();
    return sp;
}

void toDocument(const Screenplay &sp, QTextDocument *doc, const Formatting &f)
{
    if (!doc)
        return;
    doc->clear();
    const Format &fmt = format(f);
    QTextCursor c(doc);
    c.beginEditBlock();
    bool first = true;
    for (const Para &p : sp.paras) {
        if (p.spans.isEmpty())
            continue;
        QString style;
        QTextBlockFormat base;
        const Element *el = nullptr;
        switch (p.role) {
        case Role::General:
            break;
        case Role::Centered:
            base.setAlignment(Qt::AlignHCenter);
            break;
        case Role::Section:
            el = fmt.byRole(Role::Section);
            if (!el)
                style = styles::forHeadingLevel(qBound(1, p.depth, 3));
            break;
        case Role::Lyrics:
            el = fmt.byRole(Role::Lyrics);
            if (!el)
                el = fmt.byRole(Role::Dialogue);
            break;
        case Role::Shot:
            el = fmt.byRole(Role::Shot);
            if (!el)
                el = fmt.byRole(Role::Action);
            break;
        default:
            el = fmt.byRole(p.role);
            if (!el)
                el = fmt.byRole(Role::Action);
            break;
        }
        if (el)
            style = styleName(el->name);
        QTextBlockFormat bf = display::blockFormatFor(f, style, base);
        if (p.pageBreak && !first) {
            bf.setProperty(props::PageBreak, true);
            bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        }
        const QTextCharFormat bcf = display::charFormatFor(f, style);
        if (first) {
            c.setBlockFormat(bf);
            c.setBlockCharFormat(bcf);
            first = false;
        } else {
            c.insertBlock(bf, bcf);
        }
        const Style *st = styles::find(f, style);
        for (const Span &s : p.spans) {
            if (s.text.isEmpty())
                continue;
            QTextCharFormat cf = bcf;
            if (s.note) {
                cf.merge(text::noteFormat(NoteKind::InlineAnnotation, newId()));
            } else {
                if (s.bold)
                    cf.setFontWeight(QFont::Bold);
                if (s.italic)
                    cf.setFontItalic(true);
                if (s.underline)
                    cf.setFontUnderline(true);
            }
            Q_UNUSED(st);
            QString t = s.text;
            t.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
            c.insertText(t, cf);
        }
    }
    c.endEditBlock();
    doc->setModified(false);
}

// ------------------------------------------------------------------ compile

Screenplay fromManuscript(const compile::Manuscript &m)
{
    using compile::Paragraph;
    using compile::Run;
    Screenplay sp;
    if (!m.title.isEmpty())
        sp.titlePage.append({QStringLiteral("Title"), m.title});
    if (!m.author.isEmpty()) {
        sp.titlePage.append({QStringLiteral("Credit"), QStringLiteral("Written by")});
        sp.titlePage.append({QStringLiteral("Author"), m.author});
    }
    bool pendingBreak = false;
    for (const Paragraph &mp : m.paragraphs) {
        Para p;
        if (!mp.script.isEmpty()) {
            p.role = roleFromKey(mp.script);
            if (p.role == Role::General)
                p.role = Role::Action;
        } else {
            switch (mp.style) {
            case Paragraph::Heading1: p.role = Role::Section; p.depth = 1; break;
            case Paragraph::Heading2: p.role = Role::Section; p.depth = 2; break;
            case Paragraph::Heading3: p.role = Role::Section; p.depth = 3; break;
            case Paragraph::Title:
            case Paragraph::Subtitle:
            case Paragraph::Centered:
            case Paragraph::Separator:
                p.role = Role::Centered;
                break;
            default:
                p.role = (mp.align & Qt::AlignHCenter) ? Role::Centered : Role::Action;
                break;
            }
        }
        p.pageBreak = mp.pageBreakBefore || pendingBreak;
        for (const Run &r : mp.runs) {
            Span s;
            switch (r.kind) {
            case Run::Text: s.text = r.capitals == 2 ? r.text.toUpper() : r.text; break;
            case Run::Tab: s.text = QStringLiteral("    "); break;
            case Run::LineBreak: s.text = QStringLiteral("\n"); break;
            default: continue;
            }
            s.bold = r.bold;
            s.italic = r.italic;
            s.underline = r.underline;
            s.note = mp.style == Paragraph::Synopsis;
            if (!p.spans.isEmpty() && p.spans.last().bold == s.bold && p.spans.last().italic == s.italic
                && p.spans.last().underline == s.underline && p.spans.last().note == s.note)
                p.spans.last().text += s.text;
            else
                p.spans.append(s);
        }
        if (p.isEmpty()) {
            pendingBreak = p.pageBreak;
            continue;
        }
        pendingBreak = false;
        sp.paras.append(p);
    }
    return sp;
}

} // namespace script
} // namespace qb
