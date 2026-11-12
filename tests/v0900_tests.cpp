// Checks of 0.90.0 beta: what a Scrivener project keeps when this program writes it.
//
// The checks of 0.81.3 compared words, styles and notes, and their step "every text written anew" wrote almost
// none (a letter added and taken away again is no change). Here every text is really written anew, and a small
// RTF reader of the test's own (not the program's) adds up what the texts carry: bold and italic (by switch or by
// the font's face name), colors, fonts, sizes, indents, tab stops, lists, pictures, links. What Scrivener wrote and
// what this program wrote must add up the same.
//
// The project is made up, in the forms of a project of Scrivener 3.1.6.0 for Windows: fonts named by face
// ("Cambria-BoldItalic") with the switches off, a right indent below zero, a list with a list table, links to
// outside addresses underlined, a picture linked to a file, linked notes with what Scrivener keeps with them,
// bookmarks of documents and of the project to outside addresses, a default label and status, a deadline, a
// document's target, cards placed freely on the corkboard.
// QB_SCRIV_REAL=<folder.scriv> does the same with a copy of a real project (never named or kept here);
// QB_V0900_KEEP=<dir> keeps the copy as written.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0900_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/TextProps.h"
#include "core/IconCatalog.h"
#include "ui/AppSettings.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/Language.h"

#include "AppInfo.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <cstdio>
#include <memory>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("        %s\n", qPrintable(detail.left(1500)));
    }
    std::fflush(stdout);
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void writeAll(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

static void copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString t = to + QLatin1Char('/') + QDir(from).relativeFilePath(f);
        QDir().mkpath(QFileInfo(t).absolutePath());
        QFile::copy(f, t);
    }
}

static QMap<QString, QByteArray> filesOf(const QString &dir)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        out.insert(QDir(dir).relativeFilePath(f), readAll(f));
    }
    return out;
}

static QString differences(const QMap<QString, QByteArray> &a, const QMap<QString, QByteArray> &b)
{
    QStringList out;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        if (!b.contains(it.key()))
            out << QStringLiteral("gone: ") + it.key();
        else if (b.value(it.key()) != it.value())
            out << QStringLiteral("changed: ") + it.key();
    for (auto it = b.cbegin(); it != b.cend(); ++it)
        if (!a.contains(it.key()))
            out << QStringLiteral("new: ") + it.key();
    return out.join(QLatin1String(", "));
}

// A project file's element as text that says the same for the same content (attributes sorted, no white space)
static QString canonical(const QDomElement &e)
{
    QStringList attrs;
    const QDomNamedNodeMap map = e.attributes();
    for (int i = 0; i < map.count(); ++i)
        attrs << map.item(i).nodeName() + QLatin1Char('=') + map.item(i).nodeValue();
    attrs.sort();
    QString out = QLatin1Char('<') + e.tagName() + QLatin1Char(' ') + attrs.join(QLatin1Char(' ')) + QLatin1Char('>');
    bool kids = false;
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        out += canonical(c);
        kids = true;
    }
    if (!kids)
        out += e.text().trimmed();
    return out + QLatin1String("</>");
}

// Which parts of two project files differ: top-level elements by name, binder items by title and part
static QString projectFileChanges(const QByteArray &a, const QByteArray &b)
{
    QDomDocument da, db;
    da.setContent(a);
    db.setContent(b);
    QStringList out;
    for (QDomElement e = da.documentElement().firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
        if (e.tagName() != QLatin1String("Binder") && canonical(e) != canonical(db.documentElement().firstChildElement(e.tagName())))
            out << e.tagName();
    for (QDomElement e = db.documentElement().firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
        if (da.documentElement().firstChildElement(e.tagName()).isNull())
            out << QStringLiteral("new ") + e.tagName() + QLatin1String(" ") + canonical(e).left(400);
    QHash<QString, QDomElement> items;
    const QDomNodeList la = da.elementsByTagName(QStringLiteral("BinderItem")), lb = db.elementsByTagName(QStringLiteral("BinderItem"));
    for (int i = 0; i < la.size(); ++i)
        items.insert(la.at(i).toElement().attribute(QStringLiteral("UUID")), la.at(i).toElement());
    for (int i = 0; i < lb.size(); ++i) {
        const QDomElement y = lb.at(i).toElement(), x = items.value(y.attribute(QStringLiteral("UUID")));
        QSet<QString> tags;
        for (QDomElement c = x.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            tags.insert(c.tagName());
        for (QDomElement c = y.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            tags.insert(c.tagName());
        tags.remove(QStringLiteral("Children"));
        for (const QString &t : std::as_const(tags))
            if (x.firstChildElement(t).isNull() != y.firstChildElement(t).isNull()
                || (!x.firstChildElement(t).isNull() && canonical(x.firstChildElement(t)) != canonical(y.firstChildElement(t))))
                out << y.firstChildElement(QStringLiteral("Title")).text() + QLatin1Char('/') + t;
    }
    return out.isEmpty() ? QStringLiteral("(only the order or the root's attributes)") : out.join(QLatin1String(", "));
}

static QByteArray pngBytes(const QColor &color, int w = 8, int h = 6)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

// ------------------------------------------------------------------------------------------------------------------
// The test's own reader: what an RTF text adds up to. It knows nothing of the program's reader or writer.

using Measure = QMap<QString, double>;

static Measure measure(const QByteArray &bytes)
{
    QString s = QString::fromLatin1(bytes);
    static const QRegularExpression marks(QStringLiteral("<!?\\$Scr_[A-Za-z]+::\\d+>"));
    s.remove(marks);                                                  // Scrivener's style marks are no text
    s.replace(QLatin1String("{\\*\\shppict"), QLatin1String("{"));  // the group Scrivener puts around a picture
    QHash<int, QString> fonts;
    QList<QColor> colors;
    {
        static const QRegularExpression tbl(QStringLiteral("\\{\\\\fonttbl(.*?)\\}\\}"), QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression ent(QStringLiteral("\\\\f(\\d+)[^;{}]*? ([^;{}\\\\]+);"));
        auto it = ent.globalMatch(tbl.match(s).captured(1));
        while (it.hasNext()) {
            const auto e = it.next();
            fonts.insert(e.captured(1).toInt(), e.captured(2).trimmed());
        }
        static const QRegularExpression ctbl(QStringLiteral("\\{\\\\colortbl(.*?)\\}"), QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression rgb(QStringLiteral("\\\\red(\\d+)\\\\green(\\d+)\\\\blue(\\d+)"));
        const QStringList parts = ctbl.match(s).captured(1).split(QLatin1Char(';'));
        for (int i = 0; i + 1 < parts.size(); ++i) {
            const auto m = rgb.match(parts[i]);
            colors << (m.hasMatch() ? QColor(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()) : QColor());
        }
    }
    struct Ch {
        bool b = false, i = false, ul = false, strike = false, sup = false, sub = false;
        int cf = 0, hl = 0, f = 0, fs = 24;
        QString href;
    };
    struct Pa {
        QChar al = QLatin1Char('l');
        int fi = 0, li = 0, ri = 0, sb = 0, sa = 0, tabs = 0;
        bool list = false;
    };
    struct Frame {
        Ch c;
        int skip, uc;
    };
    static const QRegularExpression deffRx(QStringLiteral("\\\\deff(\\d+)"));
    Ch c;
    c.f = deffRx.match(s).captured(1).toInt();
    const Ch c0 = c;
    Pa pa;
    Measure out;
    QList<Frame> stack;
    int skip = 0, uc = 1, ucskip = 0, inPar = 0;
    QString pendingHref;
    auto endPar = [&] {
        out[QStringLiteral("paragraphs")] += 1;
        out[QStringLiteral("paragraphs aligned ") + pa.al] += 1;
        out[QStringLiteral("first-line indents, twips")] += pa.fi;
        out[QStringLiteral("left indents, twips")] += pa.li;
        out[QStringLiteral("right indents, twips")] += pa.ri;
        out[QStringLiteral("space before, twips")] += pa.sb;
        out[QStringLiteral("space after, twips")] += pa.sa;
        out[QStringLiteral("tab stops")] += pa.tabs;
        if (pa.list)
            out[QStringLiteral("list paragraphs")] += 1;
        inPar = 0;
    };
    // blank: a space or a tab. It counts as a character; how it is set (bold, its font ...) does not count, as
    // nobody can see it (Scrivener keeps a character style on a space whose own look differs from the style's)
    auto text = [&](int n, bool blank = false) {
        if (skip || n <= 0)
            return;
        inPar += n;
        if (blank) {
            out[QStringLiteral("characters")] += n;
            return;
        }
        const QString face = fonts.value(c.f, QStringLiteral("?"));
        const QString style = face.section(QLatin1Char('-'), 1);
        static const QRegularExpression boldRx(QStringLiteral("Bold|Black|Heavy")), italicRx(QStringLiteral("Italic|Oblique"));
        const bool fb = style.contains(boldRx), fi = style.contains(italicRx);
        out[QStringLiteral("characters")] += n;
        if (c.b || fb)
            out[QStringLiteral("bold characters")] += n;
        if (c.i || fi)
            out[QStringLiteral("italic characters")] += n;
        if (c.b && fb)
            out[QStringLiteral("bold by switch and by face name")] += n;
        if (c.i && fi)
            out[QStringLiteral("italic by switch and by face name")] += n;
        if (c.ul)
            out[c.href.isEmpty() ? QStringLiteral("underlined characters") : QStringLiteral("underlined characters of links")] += n;
        if (c.strike)
            out[QStringLiteral("struck-through characters")] += n;
        if (c.sup)
            out[QStringLiteral("superscript characters")] += n;
        if (c.sub)
            out[QStringLiteral("subscript characters")] += n;
        if (c.hl)
            out[QStringLiteral("highlighted characters")] += n;
        const QColor col = c.cf > 0 && c.cf < colors.size() ? colors.at(c.cf) : QColor();
        if (col.isValid() && col != QColor(0, 0, 0))
            out[QStringLiteral("characters in color ") + col.name()] += n;
        out[QStringLiteral("characters in font ") + face.section(QLatin1Char('-'), 0, 0)] += n;
        out[QStringLiteral("characters of %1 pt").arg(c.fs / 2.0)] += n;
    };
    static const QSet<QString> skipped = {QStringLiteral("fonttbl"), QStringLiteral("colortbl"), QStringLiteral("stylesheet"),
                                          QStringLiteral("info"), QStringLiteral("listtable"), QStringLiteral("listoverridetable"),
                                          QStringLiteral("generator"), QStringLiteral("listtext"), QStringLiteral("pntext"),
                                          QStringLiteral("fldinst"), QStringLiteral("pict"), QStringLiteral("header"),
                                          QStringLiteral("footer")};
    const int n = s.size();
    for (int i = 0; i < n;) {
        const QChar ch = s.at(i);
        if (ch == QLatin1Char('{')) {
            stack.append({c, skip, uc});
            ++i;
        } else if (ch == QLatin1Char('}')) {
            if (!stack.isEmpty()) {
                const Frame f = stack.takeLast();
                c = f.c;
                skip = f.skip;
                uc = f.uc;
            }
            ++i;
        } else if (ch == QLatin1Char('\\') && i + 1 < n) {
            const QChar nx = s.at(i + 1);
            if (nx.isLetter()) {
                int j = i + 1;
                while (j < n && s.at(j).isLetter())
                    ++j;
                const QString word = s.mid(i + 1, j - i - 1);
                int k = j;
                if (k < n && s.at(k) == QLatin1Char('-'))
                    ++k;
                while (k < n && s.at(k).isDigit())
                    ++k;
                const bool hasNum = k > j && s.at(k - 1).isDigit();
                const int num = hasNum ? s.mid(j, k - j).toInt() : 0;
                if (!hasNum)
                    k = j;
                if (k < n && s.at(k) == QLatin1Char(' '))
                    ++k;
                const int after = k;
                i = k;
                const bool on = !hasNum || num != 0;
                if (skipped.contains(word)) {
                    if (word == QLatin1String("fldinst")) {
                        static const QRegularExpression link(QStringLiteral("^[^}]*?HYPERLINK\\s+\\\\?\"([a-zA-Z]+):"));
                        const auto m = link.match(s.mid(after, 400));
                        pendingHref = m.hasMatch() ? m.captured(1) : QStringLiteral("other");
                        out[QStringLiteral("links: ") + pendingHref] += 1;
                    } else if (word == QLatin1String("pict") && !skip) {
                        out[QStringLiteral("pictures")] += 1;
                        if (!c.href.isEmpty())
                            out[QStringLiteral("pictures inside a link")] += 1;
                        const QString grp = s.mid(after, 600);
                        static const QRegularExpression gw(QStringLiteral("\\\\picwgoal(\\d+)")), gh(QStringLiteral("\\\\pichgoal(\\d+)"));
                        out[QStringLiteral("picture widths, twips")] += gw.match(grp).captured(1).toInt();
                        out[QStringLiteral("picture heights, twips")] += gh.match(grp).captured(1).toInt();
                    } else if ((word == QLatin1String("listtext") || word == QLatin1String("pntext")) && !skip) {
                        pa.list = true;
                    }
                    ++skip;
                } else if (skip) {
                    // inside something that is no text
                } else if (word == QLatin1String("par")) {
                    endPar();
                } else if (word == QLatin1String("pard")) {
                    pa = Pa();
                } else if (word == QLatin1String("plain")) {
                    const QString href = c.href;
                    c = c0;
                    c.href = href;
                } else if (word == QLatin1String("fldrslt")) {
                    c.href = pendingHref;
                } else if (word == QLatin1String("u")) {
                    text(1);
                    ucskip = uc;
                } else if (word == QLatin1String("uc")) {
                    uc = num;
                } else if (word == QLatin1String("b")) {
                    c.b = on;
                } else if (word == QLatin1String("i")) {
                    c.i = on;
                } else if (word == QLatin1String("strike")) {
                    c.strike = on;
                } else if (word == QLatin1String("ul") || word == QLatin1String("uldb") || word == QLatin1String("ulw")) {
                    c.ul = on;
                } else if (word == QLatin1String("ulnone")) {
                    c.ul = false;
                } else if (word == QLatin1String("super")) {
                    c.sup = on;
                } else if (word == QLatin1String("sub")) {
                    c.sub = on;
                } else if (word == QLatin1String("nosupersub")) {
                    c.sup = c.sub = false;
                } else if (word == QLatin1String("cf")) {
                    c.cf = num;
                } else if (word == QLatin1String("highlight") || word == QLatin1String("cb")) {
                    c.hl = num;
                } else if (word == QLatin1String("f")) {
                    c.f = num;
                } else if (word == QLatin1String("fs")) {
                    c.fs = num > 0 ? num : 24;
                } else if (word == QLatin1String("ql") || word == QLatin1String("qc") || word == QLatin1String("qr")
                           || word == QLatin1String("qj")) {
                    pa.al = word.at(1);
                } else if (word == QLatin1String("fi")) {
                    pa.fi = num;
                } else if (word == QLatin1String("li")) {
                    pa.li = num;
                } else if (word == QLatin1String("ri")) {
                    pa.ri = num;
                } else if (word == QLatin1String("sb")) {
                    pa.sb = num;
                } else if (word == QLatin1String("sa")) {
                    pa.sa = num;
                } else if (word == QLatin1String("tx")) {
                    if (num > 0)   // Scrivener also writes a stop at 0, which is no stop
                        ++pa.tabs;
                } else if (word == QLatin1String("ls")) {
                    pa.list = true;
                } else if (word == QLatin1String("tab")) {
                    text(1, true);
                } else if (word == QLatin1String("line") || word == QLatin1String("emdash")
                           || word == QLatin1String("endash") || word == QLatin1String("bullet") || word == QLatin1String("lquote")
                           || word == QLatin1String("rquote") || word == QLatin1String("ldblquote") || word == QLatin1String("rdblquote")) {
                    text(1);
                }
            } else if (nx == QLatin1Char('\'')) {
                if (ucskip > 0)
                    --ucskip;
                else
                    text(1);
                i += 4;
            } else {
                if (nx == QLatin1Char('*'))
                    ++skip;
                else if (QStringLiteral("\\{}~_").contains(nx))
                    text(1);
                i += 2;
            }
        } else if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            ++i;
        } else {
            if (ucskip > 0)
                --ucskip;
            else
                text(1, ch == QLatin1Char(' ') || ch == QLatin1Char('\t'));
            ++i;
        }
    }
    if (inPar > 0)
        endPar();
    return out;
}

// what differs between two texts' measures ("" = nothing)
static QString unlike(const Measure &a, const Measure &b)
{
    QStringList out;
    QSet<QString> keys;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        keys.insert(it.key());
    for (auto it = b.cbegin(); it != b.cend(); ++it)
        keys.insert(it.key());
    QStringList sorted = keys.values();
    sorted.sort();
    for (const QString &k : std::as_const(sorted))
        if (qAbs(a.value(k) - b.value(k)) > 0.5)
            out << QStringLiteral("%1: %2 -> %3").arg(k).arg(a.value(k)).arg(b.value(k));
    return out.join(QLatin1String("; "));
}

// Every text and note of the working copy counts as changed here: the next save writes all of them into the .scriv
static void writeAllAnew(Project *q)
{
    scrivener::SyncState st;
    const QString file = scrivener::stateFile(q);
    st.load(file);
    for (auto it = st.ours.begin(); it != st.ours.end(); ++it)
        if (it.key().endsWith(QLatin1String("/text")) || it.key().endsWith(QLatin1String("/notes")))
            it.value() = QStringLiteral("to be written anew");
    st.save(file);
}

static QDomDocument scrivxDom(const QString &scriv)
{
    QDomDocument d;
    d.setContent(readAll(scrivener::scrivxIn(scriv)));
    return d;
}

static QDomElement itemEl(const QDomDocument &d, const QString &uuid)
{
    const QDomNodeList list = d.elementsByTagName(QStringLiteral("BinderItem"));
    for (int i = 0; i < list.size(); ++i) {
        const QDomElement e = list.at(i).toElement();
        if (e.attribute(QStringLiteral("UUID")).compare(uuid, Qt::CaseInsensitive) == 0)
            return e;
    }
    return QDomElement();
}

// ------------------------------------------------------------------------------------------------------------------
// The made-up project

static const char *kDraft = "A9000000-0000-4000-8000-000000000001";
static const char *kLedger = "A9000000-0000-4000-8000-000000000002";   // fonts by face name, right indent, links, a list
static const char *kChart = "A9000000-0000-4000-8000-000000000003";    // a picture linked to a file, a plain picture
static const char *kTide = "A9000000-0000-4000-8000-000000000004";     // a linked comment and a linked footnote
static const char *kSteps = "A9000000-0000-4000-8000-000000000005";    // a numbered list
static const char *kLog = "A9000000-0000-4000-8000-000000000006";      // the project's most used font is an italic face; a picture's place without its data
static const char *kCmt = "C9000000-0000-4000-8000-000000000001";
static const char *kFn = "C9000000-0000-4000-8000-000000000002";

static QString makeProject(const QString &dir)
{
    const QString scriv = dir + QLatin1String("/Quay.scriv");
    const QByteArray x = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<ScrivenerProject Template="No" Version="2.0" Identifier="9E3C7A1B-2D4F-4C9B-8A32-4B5C6D7E8F91" Creator="SCRWIN-3.1.6.0" Device="Desk" Author="A. Writer" Modified="2026-11-12 09:00:00 +0100" ModID="31111111-2222-3333-4444-555555555555">
    <Binder>
        <BinderItem UUID="A9000000-0000-4000-8000-000000000001" Type="DraftFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Manuscript</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
            <TextSettings>
                <TextSelection>0,0</TextSelection>
            </TextSettings>
            <CorkboardAndOutliner>
                <SelectedSubdocumentUUIDs>A9000000-0000-4000-8000-000000000002</SelectedSubdocumentUUIDs>
                <CorkboardSettings>
                    <Arrangement>Freeform</Arrangement>
                    <FreeformCorkboardDocuments>
                        <IndexCardBinderUUID Position="1.139394,0.363636" TimeStamp="2026-11-12 08:20:00 +0100">A9000000-0000-4000-8000-000000000002</IndexCardBinderUUID>
                        <IndexCardBinderUUID Position="0.545455,0.057576" TimeStamp="2026-11-12 08:21:00 +0100">A9000000-0000-4000-8000-000000000003</IndexCardBinderUUID>
                    </FreeformCorkboardDocuments>
                </CorkboardSettings>
            </CorkboardAndOutliner>
            <Children>
                <BinderItem UUID="A9000000-0000-4000-8000-000000000002" Type="Text" Created="2026-11-01 10:01:00 +0100" Modified="2026-11-02 10:01:00 +0100">
                    <Title>Ledger</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                        <Target Type="Words" Notify="Yes" ShowOverrun="Yes" ShowBuffer="Yes">500</Target>
                    </TextSettings>
                </BinderItem>
                <BinderItem UUID="A9000000-0000-4000-8000-000000000003" Type="Text" Created="2026-11-01 10:02:00 +0100" Modified="2026-11-02 10:02:00 +0100">
                    <Title>Chart</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                    <Bookmarks>
                        <Bookmark BinderUUID="A9000000-0000-4000-8000-000000000002">Ledger</Bookmark>
                        <Bookmark Destination="https://example.org/tides">Tide tables</Bookmark>
                    </Bookmarks>
                </BinderItem>
                <BinderItem UUID="A9000000-0000-4000-8000-000000000004" Type="Text" Created="2026-11-01 10:03:00 +0100" Modified="2026-11-02 10:03:00 +0100">
                    <Title>Tide</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <ShowSynopsisImage>Yes</ShowSynopsisImage>
                        <IndexCardImageFileExtension>png</IndexCardImageFileExtension>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                </BinderItem>
                <BinderItem UUID="A9000000-0000-4000-8000-000000000005" Type="Text" Created="2026-11-01 10:04:00 +0100" Modified="2026-11-02 10:04:00 +0100">
                    <Title>Steps</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                </BinderItem>
                <BinderItem UUID="A9000000-0000-4000-8000-000000000006" Type="Text" Created="2026-11-01 10:05:00 +0100" Modified="2026-11-02 10:05:00 +0100">
                    <Title>Log</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                </BinderItem>
            </Children>
        </BinderItem>
        <BinderItem UUID="A9000000-0000-4000-8000-000000000010" Type="ResearchFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Research</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
        </BinderItem>
        <BinderItem UUID="A9000000-0000-4000-8000-000000000020" Type="TrashFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Trash</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
        </BinderItem>
    </Binder>
    <Collections>
        <Collection Type="Binder" ID="B9000000-0000-4000-8000-000000000001" Color="0.941176 0.937255 0.956863">
            <Title>Binder</Title>
        </Collection>
        <Collection Type="RecentSearch" ID="B9000000-0000-4000-8000-000000000002" Color="0.901961 0.901961 0.980392">
            <Title>Search Results</Title>
            <SearchSettings Operator="Any" Type="Text" Scope="All" CompileSetting="All" CaseSensitive="No" IgnoreDiacritics="No">tide</SearchSettings>
        </Collection>
        <Collection Type="SavedSearch" ID="B9000000-0000-4000-8000-000000000003" Color="0.990005 0.878630 0.841505">
            <Title>Harbor</Title>
            <SearchSettings Operator="Any" Type="Text" Scope="All" CompileSetting="All" CaseSensitive="No" IgnoreDiacritics="No">harbor office</SearchSettings>
        </Collection>
    </Collections>
    <LabelSettings>
        <Title>Label</Title>
        <DefaultLabelID>2</DefaultLabelID>
        <Labels>
            <Label ID="-1">No Label</Label>
            <Label ID="1" Color="1.000000 0.000000 0.000000">Red</Label>
            <Label ID="2" Color="0.000000 0.000000 1.000000">Blue</Label>
        </Labels>
    </LabelSettings>
    <StatusSettings>
        <Title>Status</Title>
        <DefaultStatusID>1</DefaultStatusID>
        <StatusItems>
            <Status ID="-1">No Status</Status>
            <Status ID="1">To Do</Status>
        </StatusItems>
    </StatusSettings>
    <ProjectBookmarks>
        <Bookmark BinderUUID="A9000000-0000-4000-8000-000000000002">Ledger</Bookmark>
        <Bookmark Destination="https://example.org/harbor">Harbor office</Bookmark>
    </ProjectBookmarks>
    <ProjectTargets Notify="No">
        <DraftTarget Type="Words" CountIncludedOnly="Yes" CurrentCompileGroupOnly="No" Deadline="2027-03-01 23:59:00 +0100" IgnoreDeadline="No">50000</DraftTarget>
        <SessionTarget Type="Words" CountDraftOnly="Yes" AllowNegatives="No" ResetType="Time" ResetTime="00:00" DeterminedFromDeadline="No" CanWriteOnDeadlineDate="Yes" WritingDays="">800</SessionTarget>
    </ProjectTargets>
</ScrivenerProject>
)XML";
    writeAll(scriv + QLatin1String("/Quay.scrivx"), x);
    const QString d = scriv + QLatin1String("/Files/Data/");
    const QByteArray page = "\\paperw12240\\paperh15840\\margl1800\\margr1800\\margt1440\\margb1440\\f0\\fs24\\cf0\n";
    // fonts named by their face with the switches off, as Scrivener for Windows does it (one run has the switch
    // as well); a right indent below zero with a tab stop out there; links; a highlight; a list with its table
    writeAll(d + QLatin1String(kLedger) + QLatin1String("/content.rtf"),
             QByteArray("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n"
                        "{\\fonttbl{\\f0\\fmodern\\fcharset0\\fprq2 Cambria;}{\\f1\\fmodern\\fcharset0\\fprq2 Cambria-BoldItalic;}"
                        "{\\f2\\fmodern\\fcharset0\\fprq2 SegoeUI-Bold;}{\\f3\\fmodern\\fcharset0\\fprq2 Cambria-Italic;}}\n"
                        "{\\colortbl;\\red0\\green0\\blue0;\\red255\\green255\\blue255;\\red0\\green149\\blue223;\\red255\\green230\\blue120;}\n"
                        "{\\*\\listtable{\\list\\listtemplateid1\\listhybrid{\\listlevel\\levelnfc23\\levelnfcn23\\leveljc0\\leveljcn0"
                        "\\levelfollow0\\levelstartat1\\levelspace360\\levelindent0{\\*\\levelmarker \\{disc\\}}{\\leveltext\\leveltemplateid1"
                        "\\'01\\uc1\\u8226\\'95;}{\\levelnumbers;}\\f0\\fs24\\b0\\i0\\fi-500\\li720\\lin720}{\\listname ;}\\listid1}}\n"
                        "{\\*\\listoverridetable{\\listoverride\\listid1\\listoverridecount0\\ls1}}\n")
                 + page
                 + "\\pard\\plain \\tx0\\tx360\\tx720\\sa360\\sl264\\slmult1\\ltrch\\loch {\\f0\\fs24\\b0\\i0 The ledger lay open. }"
                   "{\\f1\\fs24\\b0\\i0 Every entry mattered.}{\\f0\\fs24\\b0\\i0  So she read on, }{\\f2\\fs22\\b1\\i0\\cf3 line by line}"
                   "{\\f3\\fs24\\b0\\i0  and slowly}{\\f0\\fs24\\b0\\i0 .}\n"
                   "\\par\\pard\\plain \\tqr\\tx10498\\li44\\fi-43\\ri-1829\\sa240\\ltrch\\loch {\\f0\\fs24\\b0\\i0 Flour\\tab 12 sacks}\n"
                   "\\par\\pard\\plain \\tx0\\tx360\\sa240\\ltrch\\loch {\\f0\\fs24\\b0\\i0 See }"
                   "{\\field{\\*\\fldinst HYPERLINK \"https://example.org/harbor\"}{\\fldrslt\\f0\\fs24\\b0\\i0\\ul\\ulc0 the harbor office}}"
                   "{\\f0\\fs24\\b0\\i0  and }"
                   "{\\field{\\*\\fldinst HYPERLINK \"scrivlnk://A9000000-0000-4000-8000-000000000003\"}{\\fldrslt\\f0\\fs24\\b0\\i0 the chart}}"
                   "{\\f0\\fs24\\b0\\i0 . }{\\f0\\fs24\\b0\\i0\\cb4\\highlight4 Marked words}{\\f0\\fs24\\b0\\i0  and }"
                   "{\\f0\\fs24\\b0\\i0\\strike\\strikec0 struck words}{\\f0\\fs24\\b0\\i0 .}\n"
                   "\\par\\pard\\plain \\tx0\\tx360\\li763\\fi-475\\sa240\\ql\\ltrch\\loch \\ls1\\ilvl0{\\listtext\t\\f0\\fs24\\b0\\i0 \\uc0\\u8226 \t}"
                   "{\\f0\\fs24\\b0\\i0 Rope}\n"
                   "\\par\\plain \\ilvl0{\\listtext\t\\f0\\fs24\\b0\\i0 \\uc0\\u8226 \t}{\\f0\\fs24\\b0\\i0 Tar}\n"
                   "\\par\\pard\\plain \\tx0\\sa240\\ltrch\\loch {\\f0\\fs24\\b0\\i0 The end of the page.}}\n");
    // a picture linked to a file (the link around the picture) and a plain one, in Scrivener's \shppict form
    writeAll(d + QLatin1String(kChart) + QLatin1String("/content.rtf"),
             QByteArray("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl{\\f0\\fmodern\\fcharset0\\fprq2 Cambria;}}\n"
                        "{\\colortbl;\\red0\\green0\\blue0;}\n")
                 + page
                 + "\\pard\\plain \\tx0\\ltrch\\loch {\\f0\\fs24\\b0\\i0 The chart: }"
                   "{\\field{\\*\\fldinst HYPERLINK \"file:///C:/Users/writer/Pictures/chart.png\"}{\\fldrslt{\\*\\shppict{\\pict"
                   "{\\*\\nisusfilename chart.png}\\picw40\\pich30\\picwgoal1200\\pichgoal900\\pngblip "
                 + pngBytes(QColor(20, 90, 160), 40, 30).toHex()
                 + "}}}}{\\f0\\fs24\\b0\\i0  and a plain one: }{\\*\\shppict{\\pict{\\*\\nisusfilename buoy.png}\\picw20\\pich20"
                   "\\picwgoal600\\pichgoal600\\pngblip "
                 + pngBytes(QColor(200, 40, 40), 20, 20).toHex() + "}}{\\f0\\fs24\\b0\\i0 .}}\n");
    // a linked comment (its box folded, a bold word in it) and a linked footnote
    writeAll(d + QLatin1String(kTide) + QLatin1String("/content.rtf"),
             QByteArray("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl{\\f0\\fmodern\\fcharset0\\fprq2 Cambria;}}\n"
                        "{\\colortbl;\\red0\\green0\\blue0;}\n")
                 + page
                 + "\\pard\\plain \\tx0\\ltrch\\loch {\\f0\\fs24\\b0\\i0 The tide }"
                   "{\\field{\\*\\fldinst HYPERLINK \"scrivcmt://C9000000-0000-4000-8000-000000000001\"}{\\fldrslt\\f0\\fs24\\b0\\i0 turned}}"
                   "{\\f0\\fs24\\b0\\i0  at }"
                   "{\\field{\\*\\fldinst HYPERLINK \"scrivcmt://C9000000-0000-4000-8000-000000000002\"}{\\fldrslt\\f0\\fs24\\b0\\i0 noon}}"
                   "{\\f0\\fs24\\b0\\i0 .}}\n");
    writeAll(d + QLatin1String(kTide) + QLatin1String("/content.comments"),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Comments Version=\"1.0\">\n"
             "    <Comment ID=\"C9000000-0000-4000-8000-000000000001\" Collapsed=\"Yes\" Color=\"0.996078 0.964706 0.737255\"><![CDATA["
             "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl{\\f0\\fnil\\fcharset0\\fprq2 SegoeUI;}}\n{\\colortbl;\\red0\\green0\\blue0;}\n"
             "\\pard\\plain \\ltrch\\loch {\\f0\\fs20\\b0\\i0 Check the }{\\f0\\fs20\\b1\\i0 almanac}{\\f0\\fs20\\b0\\i0 .}}]]></Comment>\n"
             "    <Comment ID=\"C9000000-0000-4000-8000-000000000002\" Footnote=\"Yes\" Collapsed=\"No\"><![CDATA["
             "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl{\\f0\\fnil\\fcharset0\\fprq2 SegoeUI;}}\n{\\colortbl;\\red0\\green0\\blue0;}\n"
             "\\pard\\plain \\ltrch\\loch {\\f0\\fs20\\b0\\i0 Local time.}}]]></Comment>\n</Comments>\n");
    // a numbered list (the table's level says "decimal"; this form follows RTF, no file of Scrivener was at hand)
    writeAll(d + QLatin1String(kSteps) + QLatin1String("/content.rtf"),
             QByteArray("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl{\\f0\\fmodern\\fcharset0\\fprq2 Cambria;}}\n"
                        "{\\colortbl;\\red0\\green0\\blue0;}\n"
                        "{\\*\\listtable{\\list\\listtemplateid1\\listhybrid{\\listlevel\\levelnfc0\\levelnfcn0\\leveljc0\\leveljcn0"
                        "\\levelfollow0\\levelstartat1\\levelspace360\\levelindent0{\\*\\levelmarker \\{decimal\\}.}{\\leveltext\\leveltemplateid1"
                        "\\'02\\'00.;}{\\levelnumbers\\'01;}\\f0\\fs24\\b0\\i0\\fi-500\\li720\\lin720}{\\listname ;}\\listid1}}\n"
                        "{\\*\\listoverridetable{\\listoverride\\listid1\\listoverridecount0\\ls1}}\n")
                 + page
                 + "\\pard\\plain \\tx0\\tx360\\li763\\fi-475\\sa240\\ql\\ltrch\\loch \\ls1\\ilvl0{\\listtext\t\\f0\\fs24\\b0\\i0 1.\t}"
                   "{\\f0\\fs24\\b0\\i0 Cast off}\n"
                   "\\par\\plain \\ilvl0{\\listtext\t\\f0\\fs24\\b0\\i0 2.\t}{\\f0\\fs24\\b0\\i0 Row out}}\n");
    // most of the project's text is in a font named by its italic face (so that face is the project's own font
    // here), and a paragraph holds only the place of a picture whose data is missing, as Scrivener for Windows keeps it
    {
        QByteArray watch;
        for (int i = 1; i <= 12; ++i)
            watch += "Watch " + QByteArray::number(i) + ": wind steady from the west, sea calm, nothing to report. ";
        writeAll(d + QLatin1String(kLog) + QLatin1String("/content.rtf"),
                 QByteArray("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl{\\f0\\fmodern\\fcharset0\\fprq2 Cambria;}"
                            "{\\f1\\fmodern\\fcharset0\\fprq2 Cambria-Italic;}}\n{\\colortbl;\\red0\\green0\\blue0;}\n")
                     + page + "\\pard\\plain \\tx0\\sa240\\ltrch\\loch {\\f1\\fs24\\b0\\i0 " + watch.trimmed() + "}\n"
                     + "\\par\\pard\\plain \\tx0\\sa240\\ltrch\\loch {\\f1\\fs24\\b0\\i0 \\loch\\af1\\hich\\af1\\dbch\\af1\\uc1\\u65532\\'3F}\n"
                       "\\par\\plain {\\f1\\fs24\\b0\\i0 More of the log, still in the italic face.}\n"
                       "\\par\\pard\\plain \\tx0\\sa240\\ltrch\\loch {\\f0\\fs24\\b0\\i0 Upright again.}}\n");
    }
    writeAll(d + QLatin1String(kTide) + QLatin1String("/card-image.png"), pngBytes(QColor(10, 140, 90), 30, 20));
    writeAll(scriv + QLatin1String("/Settings/compile.xml"),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<CompileSettings>\n    <ProjectSettings>\n        <MetaData>\n"
             "            <ProjectTitle>The Quay at Dusk</ProjectTitle>\n            <ProjectAbbreviatedTitle>Quay</ProjectAbbreviatedTitle>\n"
             "            <Surname>Writer</Surname>\n            <Forename>Ann</Forename>\n            <EbookLanguage>en</EbookLanguage>\n"
             "        </MetaData>\n    </ProjectSettings>\n    <CurrentFileType>print</CurrentFileType>\n</CompileSettings>\n");
    writeAll(scriv + QLatin1String("/Settings/ui-common.xml"),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<UIStates>\n    <Binder Show=\"Yes\">\n        <ShowCollections>No</ShowCollections>\n"
             "        <ExpandedItems>\n            <ItemID>A9000000-0000-4000-8000-000000000001</ItemID>\n        </ExpandedItems>\n"
             "        <Selection>\n            <ItemID>A9000000-0000-4000-8000-000000000004</ItemID>\n        </Selection>\n"
             "    </Binder>\n</UIStates>\n");
    writeAll(scriv + QLatin1String("/Files/writing.history"),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<WritingHistory>\n"
             "    <Day dwc=\"-3\" dcc=\"-20\" dtwc=\"1200\" owc=\"0\" occ=\"-5\" dtcc=\"6800\">2026-11-10</Day>\n"
             "    <Day dwc=\"310\" dcc=\"1720\" owc=\"12\" occ=\"60\">2026-11-11</Day>\n</WritingHistory>\n");
    writeAll(scriv + QLatin1String("/Files/version.txt"), "23");
    return scriv;
}

static QString textFile(const QString &scriv, const char *id, const char *name = "content.rtf")
{
    return scriv + QLatin1String("/Files/Data/") + QLatin1String(id) + QLatin1Char('/') + QLatin1String(name);
}

// ------------------------------------------------------------------------------------------------------------------
// Texts: written anew, they add up to what Scrivener wrote

// The system test's menu monkey (2026-11-12): a list item with a paragraph style lost the style at the next
// opening in Markdown storage (the list's mark took the place of "::Style::").
static void listStyles()
{
    const QString L = QStringLiteral("list items with a style: ");
    Formatting f;
    QTextDocument doc;
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Place Name"));
    c.insertBlock();
    c.insertText(QStringLiteral("Where it is"));
    c.insertBlock();
    c.insertText(QStringLiteral("Plain item"));
    c.insertBlock();
    c.insertText(QStringLiteral("After the list"));
    // as the monkey did it: the styles first, then Numbered List over the paragraphs
    display::applyParagraphStyle(QTextCursor(doc.findBlockByNumber(0)), QStringLiteral("Heading 1"), f);
    display::applyParagraphStyle(QTextCursor(doc.findBlockByNumber(1)), QStringLiteral("Caption"), f);
    QTextCursor all(&doc);
    all.setPosition(doc.findBlockByNumber(2).position() + 1, QTextCursor::KeepAnchor);
    QTextListFormat lf;
    lf.setStyle(QTextListFormat::ListDecimal);
    all.createList(lf);
    auto styleOf = [](const QTextDocument &d, int n) { return d.findBlockByNumber(n).blockFormat().property(props::Style).toString(); };
    auto listed = [](const QTextDocument &d, int n) { return d.findBlockByNumber(n).textList() != nullptr; };
    check(styleOf(doc, 0) == QLatin1String("Heading 1") && styleOf(doc, 1) == QLatin1String("Caption") && listed(doc, 0) && listed(doc, 2),
          L + QStringLiteral("made (a numbered list of three, two with a style)"));
    const QString mdText = md::write(&doc, f);
    QTextDocument back;
    md::read(mdText, &back, f);
    check(styleOf(back, 0) == QLatin1String("Heading 1") && styleOf(back, 1) == QLatin1String("Caption") && styleOf(back, 2).isEmpty()
              && listed(back, 0) && listed(back, 1) && listed(back, 2) && !listed(back, 3) && back.toPlainText() == doc.toPlainText(),
          L + QStringLiteral("Markdown: the styles and the list come back"), mdText.left(200));
    check(back.findBlockByNumber(1).begin().fragment().charFormat().fontItalic() == doc.findBlockByNumber(1).begin().fragment().charFormat().fontItalic(),
          L + QStringLiteral("Markdown: the style's own look comes back (Caption)"));
    QTextDocument older;
    md::read(QStringLiteral("1. Place Name\n2. Where it is"), &older, f);
    check(listed(older, 0) && styleOf(older, 0).isEmpty() && older.toPlainText() == QStringLiteral("Place Name\nWhere it is"),
          L + QStringLiteral("Markdown: list items as earlier versions wrote them read as before"));
    QTextDocument rtfBack;
    rtf::read(rtf::write(&doc, f), &rtfBack, f);
    check(styleOf(rtfBack, 0) == QLatin1String("Heading 1") && styleOf(rtfBack, 1) == QLatin1String("Caption") && listed(rtfBack, 0) && listed(rtfBack, 2),
          L + QStringLiteral("RTF: the styles and the list come back"));
}

static void texts(const QString &W)
{
    const QString L = QStringLiteral("texts: ");
    const QString scriv = makeProject(W + QLatin1String("/texts"));
    const QMap<QString, QByteArray> before = filesOf(scriv);
    QString err;
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("the project opens"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    check(q->save(&err), L + QStringLiteral("saved without a change"), err);
    {
        QMap<QString, QByteArray> after = filesOf(scriv);
        for (const QString &k : after.keys())
            if (k.endsWith(QLatin1String("quillbinder.lock")) || (k == QLatin1String("Files/search.indexes") && !before.contains(k)))
                after.remove(k);
        check(differences(before, after).isEmpty(), L + QStringLiteral("a save without a change changes no file"),
              differences(before, after) + QLatin1String(": ")
                  + projectFileChanges(before.value(QStringLiteral("Quay.scrivx")), after.value(QStringLiteral("Quay.scrivx"))));
    }
    // what the program's reader makes of the files themselves
    {
        auto kinds = [&](const char *id) {
            QTextDocument d;
            rtf::read(readAll(textFile(scriv, id)), &d, Formatting());
            QStringList out;
            for (QTextBlock b = d.begin(); b.isValid(); b = b.next())
                out << (b.textList() ? (b.textList()->format().style() == QTextListFormat::ListDecimal ? QStringLiteral("number") : QStringLiteral("bullet"))
                                     : QStringLiteral("-"));
            return out.join(QLatin1Char(' '));
        };
        check(kinds(kSteps) == QLatin1String("number number"), L + QStringLiteral("the reader: a numbered list from its first paragraph on"), kinds(kSteps));
        check(kinds(kLedger) == QLatin1String("- - - bullet bullet -"), L + QStringLiteral("the reader: a bullet list in the middle of a text"), kinds(kLedger));
        // through the program's own form of a text (the working copy) and through Scrivener's form
        for (const bool scrivenerForm : {false, true}) {
            QTextDocument d, back;
            rtf::read(readAll(textFile(scriv, kSteps)), &d, Formatting());
            rtf::WriteOptions how;
            how.scrivener = scrivenerForm;
            const QByteArray written = rtf::write(&d, Formatting(), how);
            rtf::read(written, &back, Formatting());
            QStringList out;
            for (QTextBlock b = back.begin(); b.isValid(); b = b.next())
                out << (b.textList() ? (b.textList()->format().style() == QTextListFormat::ListDecimal ? QStringLiteral("number") : QStringLiteral("bullet"))
                                     : QStringLiteral("-"));
            check(out.join(QLatin1Char(' ')) == QLatin1String("number number"),
                  L + (scrivenerForm ? QStringLiteral("a list from the first paragraph on, written in Scrivener's form and read")
                                     : QStringLiteral("a list from the first paragraph on, written in the program's own form and read")),
                  out.join(QLatin1Char(' ')) + QLatin1String(" | ") + QString::fromLatin1(written.mid(written.indexOf("\\pard"), 260)));
        }
    }
    // what the program read
    {
        QTextDocument *chart = q->docs()->text(QLatin1String(kChart));
        int linked = 0, pictures = 0;
        for (QTextBlock b = chart->begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat cf = it.fragment().charFormat();
                if (!cf.isImageFormat())
                    continue;
                ++pictures;
                linked += cf.isAnchor() && cf.anchorHref().startsWith(QLatin1String("file:"));
            }
        check(pictures == 2 && linked == 1, L + QStringLiteral("a picture linked to a file is read with its link"),
              QStringLiteral("%1 pictures, %2 with a link").arg(pictures).arg(linked));
        QTextDocument *steps = q->docs()->text(QLatin1String(kSteps));
        QTextList *numbered = steps->begin().textList();
        check(numbered && numbered->format().style() == QTextListFormat::ListDecimal,
              L + QStringLiteral("a numbered list of a list table is read as a numbered list"),
              numbered ? QStringLiteral("list style %1").arg(int(numbered->format().style())) : QStringLiteral("no list"));
        QTextDocument *ledger = q->docs()->text(QLatin1String(kLedger));
        QTextList *bullets = ledger->findBlockByNumber(3).textList();
        check(bullets && bullets->format().style() == QTextListFormat::ListDisc, L + QStringLiteral("a bullet list stays a bullet list"));
    }
    writeAllAnew(q);
    check(q->save(&err), L + QStringLiteral("saved with every text written anew"), err);
    const QMap<QString, QByteArray> after = filesOf(scriv);
    int written = 0;
    QStringList unlikeTexts;
    for (const char *id : {kLedger, kChart, kTide, kSteps, kLog}) {
        const QString key = QStringLiteral("Files/Data/") + QLatin1String(id) + QLatin1String("/content.rtf");
        written += before.value(key) != after.value(key);
        const QString u = unlike(measure(before.value(key)), measure(after.value(key)));
        if (!u.isEmpty())
            unlikeTexts << QDir(scriv).relativeFilePath(textFile(scriv, id)).section(QLatin1Char('/'), 2, 2).right(4) + QLatin1String(": ") + u;
    }
    check(written == 5, L + QStringLiteral("all five texts were written anew"), QStringLiteral("%1 written").arg(written));
    check(unlikeTexts.isEmpty(), L + QStringLiteral("written anew, every text adds up to what Scrivener wrote"),
          unlikeTexts.join(QLatin1String("\n        ")));
    const QByteArray ledger = after.value(QStringLiteral("Files/Data/") + QLatin1String(kLedger) + QLatin1String("/content.rtf"));
    check(ledger.contains("\\ri-1829"), L + QStringLiteral("the right indent below zero is written back"));
    // the control words right before a piece of text
    auto codesOf = [](const QByteArray &rtf, const char *words) {
        const qsizetype at = rtf.indexOf(words);
        return at < 0 ? QByteArray("(not found)") : rtf.mid(rtf.lastIndexOf('{', at), at - rtf.lastIndexOf('{', at));
    };
    static const QRegularExpression sw(QStringLiteral("\\\\[bi](?![a-z0])"));   // \b or \i switched on
    check(codesOf(ledger, "the harbor office").contains("\\ul") && !codesOf(ledger, "the chart").contains("\\ul"),
          L + QStringLiteral("a link underlined in the file stays so, one that was not stays plain"),
          QString::fromLatin1(codesOf(ledger, "the harbor office") + " | " + codesOf(ledger, "the chart")));
    check(ledger.contains("{\\*\\listtable{\\list") && ledger.contains("\\levelnfc23") && ledger.contains("{\\*\\listoverridetable{\\listoverride")
              && ledger.contains("\\ls1\\ilvl0{\\listtext") && !ledger.contains("\\pntext"),
          L + QStringLiteral("a list is written with a list table, as Scrivener writes it"));
    check(ledger.contains("\\fi-475\\li763"), L + QStringLiteral("a list item keeps the indents it had"));
    {
        const QByteArray face = codesOf(ledger, "Every entry mattered"), both = codesOf(ledger, "line by line");
        check(ledger.count("Cambria-BoldItalic;") == 1 && !sw.match(QString::fromLatin1(face)).hasMatch(),
              L + QStringLiteral("a font named by its bold italic face gets no bold or italic switch"), QString::fromLatin1(face));
        check(both.contains("\\b"), L + QStringLiteral("a bold face that had the switch as well keeps it"), QString::fromLatin1(both));
        const QByteArray log = after.value(QStringLiteral("Files/Data/") + QLatin1String(kLog) + QLatin1String("/content.rtf"));
        check(!sw.match(QString::fromLatin1(codesOf(log, "More of the log"))).hasMatch() && !sw.match(QString::fromLatin1(codesOf(log, "Watch 1:"))).hasMatch(),
              L + QStringLiteral("text in the project's own font, an italic face, gets no italic switch either"),
              QString::fromLatin1(codesOf(log, "More of the log")));
        check(log.contains("\\u65532\\'3F"), L + QStringLiteral("the place of a picture without its data stays in the text"));
    }
    const QByteArray chart = after.value(QStringLiteral("Files/Data/") + QLatin1String(kChart) + QLatin1String("/content.rtf"));
    check(chart.contains("HYPERLINK \"file:///C:/Users/writer/Pictures/chart.png\"") && measure(chart).value(QStringLiteral("pictures inside a link")) == 1
              && measure(chart).value(QStringLiteral("pictures")) == 2,
          L + QStringLiteral("the picture linked to a file is written inside its link"));
    const QByteArray steps = after.value(QStringLiteral("Files/Data/") + QLatin1String(kSteps) + QLatin1String("/content.rtf"));
    check(steps.contains("\\levelnfc0") && steps.contains("{\\listtext") && steps.contains(" 1.\t}") && steps.contains(" 2.\t}"),
          L + QStringLiteral("a numbered list is written as a numbered list"));
    // the linked notes
    {
        QDomDocument dom;
        dom.setContent(after.value(QStringLiteral("Files/Data/") + QLatin1String(kTide) + QLatin1String("/content.comments")));
        QDomElement c1 = dom.documentElement().firstChildElement(QStringLiteral("Comment"));
        QDomElement c2 = c1.nextSiblingElement(QStringLiteral("Comment"));
        check(c1.attribute(QStringLiteral("ID")) == QLatin1String(kCmt) && c2.attribute(QStringLiteral("ID")) == QLatin1String(kFn)
                  && c2.nextSiblingElement(QStringLiteral("Comment")).isNull(),
              L + QStringLiteral("the linked notes keep their IDs"), c1.attribute(QStringLiteral("ID")) + QLatin1Char(' ') + c2.attribute(QStringLiteral("ID")));
        check(c1.attribute(QStringLiteral("Collapsed")) == QLatin1String("Yes") && c2.attribute(QStringLiteral("Collapsed")) == QLatin1String("No"),
              L + QStringLiteral("a folded note stays folded"));
        check(!c1.hasAttribute(QStringLiteral("Footnote")) && c1.attribute(QStringLiteral("Color")) == QLatin1String("0.996078 0.964706 0.737255")
                  && c2.attribute(QStringLiteral("Footnote")) == QLatin1String("Yes") && !c2.hasAttribute(QStringLiteral("Color")),
              L + QStringLiteral("a comment has its color and a footnote its mark, as Scrivener writes them"));
        check(c1.text().contains(QLatin1String("\\b1\\i0 almanac")), L + QStringLiteral("the formatting inside an unchanged note stays"), c1.text().right(120));
        const QByteArray tide = after.value(QStringLiteral("Files/Data/") + QLatin1String(kTide) + QLatin1String("/content.rtf"));
        check(tide.contains(QByteArray("scrivcmt://") + kCmt) && tide.contains(QByteArray("scrivcmt://") + kFn),
              L + QStringLiteral("the text names its notes by the same IDs"));
    }
    // a second writing of everything changes nothing more (what was written is read and written the same way)
    {
        writeAllAnew(q);
        check(q->save(&err), L + QStringLiteral("saved again with every text written anew"), err);
        const QMap<QString, QByteArray> again = filesOf(scriv);
        QStringList changed;
        for (const char *id : {kLedger, kChart, kTide, kSteps, kLog}) {
            const QString key = QStringLiteral("Files/Data/") + QLatin1String(id) + QLatin1String("/content.rtf");
            const QString u = unlike(measure(after.value(key)), measure(again.value(key)));
            if (!u.isEmpty())
                changed << u;
        }
        check(changed.isEmpty(), L + QStringLiteral("written a second time, the texts add up the same"), changed.join(QLatin1String("\n        ")));
    }
    // read by the program again (as after a change in Scrivener): the same model
    {
        ScrivenerImportReport rep;
        auto back = ScrivenerImporter::importProject(scriv, W + QLatin1String("/texts-back"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
        check(bool(back), L + QStringLiteral("the written project reads back"), err);
        if (back) {
            QTextDocument *steps2 = back->docs()->text(QLatin1String(kSteps));
            QTextList *l = steps2 ? steps2->begin().textList() : nullptr;
            check(l && l->format().style() == QTextListFormat::ListDecimal && l->count() == 2,
                  L + QStringLiteral("read back: the numbered list with its two items"));
            QTextDocument *ledger2 = back->docs()->text(QLatin1String(kLedger));
            check(ledger2 && ledger2->findBlockByNumber(1).blockFormat().intProperty(props::RightOutdent) == -1829,
                  L + QStringLiteral("read back: the right indent below zero"));
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// The project file: what Scrivener keeps there stays when this program writes it

static void projectFile(const QString &W)
{
    const QString L = QStringLiteral("project file: ");
    for (const char *lang : {"en", "de"}) {
        const QString tag = QLatin1String(lang) == QLatin1String("de") ? QStringLiteral(" (German interface)") : QString();
        qputenv("QB_LANGUAGE", lang);
        language::install();
        const QString scriv = makeProject(W + QLatin1String("/file-") + QLatin1String(lang));
        QString err;
        scrivener::OpenResult r = scrivener::open(scriv, &err);
        check(bool(r.project), L + QStringLiteral("the project opens") + tag, err);
        if (!r.project)
            continue;
        Project *q = r.project.get();
        if (tag.isEmpty())
            check(q->targets.deadline == QDate(2027, 3, 1), L + QStringLiteral("the deadline of the draft target is read"),
                  q->targets.deadline.toString(Qt::ISODate));
        // a change of the binder: the project file is written
        BinderItem *tide = q->item(QLatin1String(kTide));
        tide->title = QStringLiteral("Tide at noon");
        q->notifyItemChanged(tide, false);
        q->markIndexDirty();
        check(q->save(&err), L + QStringLiteral("saved after a title changed") + tag, err);
        const QDomDocument dom = scrivxDom(scriv);
        const QDomElement root = dom.documentElement();
        check(itemEl(dom, QLatin1String(kTide)).firstChildElement(QStringLiteral("Title")).text() == QLatin1String("Tide at noon"),
              L + QStringLiteral("the project file was written") + tag);
        const QDomElement labels = root.firstChildElement(QStringLiteral("LabelSettings"));
        const QDomElement statuses = root.firstChildElement(QStringLiteral("StatusSettings"));
        check(labels.firstChildElement(QStringLiteral("Labels")).firstChildElement(QStringLiteral("Label")).text() == QLatin1String("No Label")
                  && statuses.firstChildElement(QStringLiteral("StatusItems")).firstChildElement(QStringLiteral("Status")).text()
                         == QLatin1String("No Status"),
              L + QStringLiteral("the names of \"No Label\" and \"No Status\" stay as the file has them") + tag,
              labels.firstChildElement(QStringLiteral("Labels")).firstChildElement(QStringLiteral("Label")).text());
        check(labels.firstChildElement(QStringLiteral("DefaultLabelID")).text() == QLatin1String("2")
                  && statuses.firstChildElement(QStringLiteral("DefaultStatusID")).text() == QLatin1String("1"),
              L + QStringLiteral("the default label and status stay") + tag,
              labels.firstChildElement(QStringLiteral("DefaultLabelID")).text() + QLatin1Char(' ')
                  + statuses.firstChildElement(QStringLiteral("DefaultStatusID")).text());
        {
            QStringList marks;
            const QDomElement pb = root.firstChildElement(QStringLiteral("ProjectBookmarks"));
            for (QDomElement b = pb.firstChildElement(QStringLiteral("Bookmark")); !b.isNull(); b = b.nextSiblingElement(QStringLiteral("Bookmark")))
                marks << (b.hasAttribute(QStringLiteral("Destination")) ? b.attribute(QStringLiteral("Destination"))
                                                                         : b.attribute(QStringLiteral("BinderUUID")))
                             + QLatin1Char('=') + b.text();
            check(marks == QStringList({QLatin1String(kLedger) + QStringLiteral("=Ledger"), QStringLiteral("https://example.org/harbor=Harbor office")}),
                  L + QStringLiteral("a project bookmark to an outside address stays, in its place") + tag, marks.join(QLatin1String(" | ")));
        }
        const QDomElement dt = root.firstChildElement(QStringLiteral("ProjectTargets")).firstChildElement(QStringLiteral("DraftTarget"));
        check(dt.attribute(QStringLiteral("IgnoreDeadline")) == QLatin1String("No")
                  && dt.attribute(QStringLiteral("Deadline")) == QLatin1String("2027-03-01 23:59:00 +0100") && dt.text() == QLatin1String("50000"),
              L + QStringLiteral("the deadline stays switched on, with its time") + tag,
              dt.attribute(QStringLiteral("Deadline")) + QLatin1String(" ignore=") + dt.attribute(QStringLiteral("IgnoreDeadline")));
        const QDomElement st = root.firstChildElement(QStringLiteral("ProjectTargets")).firstChildElement(QStringLiteral("SessionTarget"));
        check(st.attribute(QStringLiteral("CountDraftOnly")) == QLatin1String("Yes") && st.attribute(QStringLiteral("ResetTime")) == QLatin1String("00:00")
                  && st.text() == QLatin1String("800"),
              L + QStringLiteral("the session target keeps what Scrivener set") + tag);
        const QDomElement ledger = itemEl(dom, QLatin1String(kLedger)), chart = itemEl(dom, QLatin1String(kChart));
        check(ledger.firstChildElement(QStringLiteral("TextSettings")).firstChildElement(QStringLiteral("Target")).text() == QLatin1String("500"),
              L + QStringLiteral("a document's target stays") + tag);
        check(chart.firstChildElement(QStringLiteral("Bookmarks")).elementsByTagName(QStringLiteral("Bookmark")).size() == 2
                  && chart.firstChildElement(QStringLiteral("Bookmarks")).lastChildElement(QStringLiteral("Bookmark")).attribute(QStringLiteral("Destination"))
                         == QLatin1String("https://example.org/tides"),
              L + QStringLiteral("a document's bookmarks stay") + tag);
        const QDomElement free = itemEl(dom, QLatin1String(kDraft)).firstChildElement(QStringLiteral("CorkboardAndOutliner"))
                                     .firstChildElement(QStringLiteral("CorkboardSettings"));
        check(free.firstChildElement(QStringLiteral("Arrangement")).text() == QLatin1String("Freeform")
                  && free.firstChildElement(QStringLiteral("FreeformCorkboardDocuments")).elementsByTagName(QStringLiteral("IndexCardBinderUUID")).size() == 2,
              L + QStringLiteral("cards placed freely on the corkboard stay") + tag);
    }
    qputenv("QB_LANGUAGE", "en");
    language::install();
}

// ------------------------------------------------------------------------------------------------------------------
// What Scrivener keeps and this program now reads, shows and writes back: the corkboard, bookmarks, targets

static QString attr(const QDomElement &e, const char *name)
{
    return e.attribute(QLatin1String(name));
}

static void exchange(const QString &W)
{
    const QString L = QStringLiteral("exchange: ");
    const QString scriv = makeProject(W + QLatin1String("/exchange"));
    QString err;
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("the project opens"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    BinderItem *draft = q->item(QLatin1String(kDraft)), *ledger = q->item(QLatin1String(kLedger)), *chart = q->item(QLatin1String(kChart));
    // read
    check(draft->corkArrangement == QLatin1String("freeform"), L + QStringLiteral("a corkboard arranged freely in Scrivener is read so"),
          draft->corkArrangement);
    const int unit = ScrivenerImporter::cardWidth();
    check(ledger->corkboardPos == QStringLiteral("%1,%2").arg(qRound(1.139394 * unit)).arg(qRound(0.363636 * unit))
              && chart->corkboardPos == QStringLiteral("%1,%2").arg(qRound(0.545455 * unit)).arg(qRound(0.057576 * unit)),
          L + QStringLiteral("the places of its cards are read, in this program's card width"),
          ledger->corkboardPos + QLatin1String(" ") + chart->corkboardPos + QStringLiteral(" (card width %1)").arg(unit));
    check(ledger->target == 500 && !ledger->targetChars, L + QStringLiteral("a document's target is read"), QString::number(ledger->target));
    check(chart->bookmarks.size() == 2 && chart->bookmarks.value(0).isInternal() && chart->bookmarks.value(0).internalId() == QLatin1String(kLedger)
              && chart->bookmarks.value(1).target == QLatin1String("https://example.org/tides")
              && chart->bookmarks.value(1).title == QLatin1String("Tide tables"),
          L + QStringLiteral("a document's bookmarks are read, to a document and to an outside address"));
    check(q->projectBookmarks.size() == 2 && q->projectBookmarks.value(1).target == QLatin1String("https://example.org/harbor"),
          L + QStringLiteral("the project's bookmark to an outside address is read"));
    check(q->history.size() == 2 && q->history.value(0).date == QDate(2026, 11, 10) && q->history.value(0).draftWords == -3
              && q->history.value(1).draftWords == 310 && q->history.value(1).otherChars == 60,
          L + QStringLiteral("the writing history is read"), QString::number(q->history.size()));
    check(!q->targets.countAnywhere && !q->targets.autoFromDeadline,
          L + QStringLiteral("the session target's options are read (only the draft counts, not from the deadline)"));
    {
        const Collection *search = nullptr;
        for (const Collection &c : q->collections)
            if (c.isSearch)
                search = &c;
        check(search && search->name == QLatin1String("Harbor") && search->query.text == QLatin1String("harbor office") && search->query.anyWord
                  && !search->query.caseSensitive,
              L + QStringLiteral("a saved search of Scrivener is a search collection here (Scrivener's own search results are not)"));
        BinderItem *tide = q->item(QLatin1String(kTide));
        check(tide && tide->synopsisImage == QLatin1String("card.png") && QFile::exists(q->contentDir(tide->id) + QLatin1String("/card.png")),
              L + QStringLiteral("the picture of an index card is read"), tide ? tide->synopsisImage : QString());
    }
    check(q->compile.titleOverride == QLatin1String("The Quay at Dusk") && q->compile.authorOverride == QLatin1String("Ann Writer")
              && q->compile.language == QLatin1String("en"),
          L + QStringLiteral("the compile metadata is read (title, author, language)"), q->compile.titleOverride + QLatin1String(" / ") + q->compile.authorOverride);
    check(q->uiState.value(QStringLiteral("expanded")).toStringList() == QStringList{QLatin1String(kDraft)}
              && q->uiState.value(QStringLiteral("binderSelection")).toStringList() == QStringList{QLatin1String(kTide)},
          L + QStringLiteral("the binder opens as Scrivener left it (open folders, selection)"));
    check(icons::fromScrivener(QStringLiteral("Book (Yellow Book)")) == QLatin1String("qb-book-yellow"),
          L + QStringLiteral("Scrivener's icon \"Book (Yellow Book)\" is known"), icons::fromScrivener(QStringLiteral("Book (Yellow Book)")));

    // the pane: the container is shown as Scrivener arranged it; a choice made here is kept with it
    {
        EditorPane pane;
        pane.resize(900, 600);
        pane.setProject(q);
        pane.setCorkMode(int(Corkboard::Mode::Grid));
        pane.showItems({draft}, true, int(ViewMode::Corkboard));
        check(pane.corkMode() == int(Corkboard::Mode::Freeform), L + QStringLiteral("the pane shows that container freely arranged"));
        pane.show();
        QApplication::processEvents();
        pane.grab().save(outDir + QLatin1String("/v0900-freeform.png"));
        pane.chooseCorkMode(int(Corkboard::Mode::Grid));
        check(draft->corkArrangement == QLatin1String("grid"), L + QStringLiteral("choosing the grid is kept with the container"));
        BinderItem *research = q->research();
        pane.showItems({research}, true, int(ViewMode::Corkboard));
        check(pane.corkMode() == int(Corkboard::Mode::Grid), L + QStringLiteral("a container without a choice keeps the pane's arrangement"));
        pane.setCorkMode(int(Corkboard::Mode::Freeform));
        pane.showItems({draft}, true, int(ViewMode::Corkboard));
        check(pane.corkMode() == int(Corkboard::Mode::Grid), L + QStringLiteral("the container's choice wins over the pane's"));
        pane.chooseCorkMode(int(Corkboard::Mode::Freeform));
    }

    // changes made here go into the .scriv
    const QByteArray scrivxBefore = readAll(scrivener::scrivxIn(scriv));
    chart->corkboardPos = QStringLiteral("%1,%2").arg(unit * 2).arg(unit);         // moved to (2.0, 1.0) card widths
    ledger->target = 750;
    chart->bookmarks.append({QStringLiteral("Almanac"), QStringLiteral("https://example.org/almanac")});
    BinderItem *tide = q->item(QLatin1String(kTide));
    tide->bookmarks.append({QString(), QStringLiteral("qb://") + QLatin1String(kSteps)});
    q->projectBookmarks.removeAt(0);
    q->targets.deadline = QDate(2027, 4, 15);
    q->targets.countAnywhere = true;
    q->compile.authorOverride = QStringLiteral("Ann Marie Writer");
    for (Collection &c : q->collections)
        if (c.isSearch)
            c.query.text = QStringLiteral("harbor master");
    {
        Collection mine;
        mine.id = QStringLiteral("search-made-here");
        mine.name = QStringLiteral("Ropes");
        mine.isSearch = true;
        mine.query.text = QStringLiteral("rope");
        mine.query.anyWord = true;
        mine.query.allWords = false;
        mine.query.caseSensitive = true;
        q->collections << mine;
        q->item(QLatin1String(kTide))->synopsisImage.clear();   // the picture taken off this card
        BinderItem *steps = q->item(QLatin1String(kSteps));
        QDir().mkpath(q->contentDir(steps->id));
        writeAll(q->contentDir(steps->id) + QLatin1String("/card.png"), pngBytes(QColor(240, 200, 20), 24, 16));
        steps->synopsisImage = QStringLiteral("card.png");      // and one put on another
    }
    {
        WritingDay day;
        day.date = QDate(2026, 11, 12);
        day.draftWords = 95;
        day.draftChars = 530;
        q->history << day;
    }
    // a keyword on the container that has a corkboard element: Scrivener's order of the parts
    Keyword kw;
    kw.id = QStringLiteral("k-made-here");
    kw.name = QStringLiteral("Harbor");
    kw.color = QColor(40, 120, 200);
    q->keywords << kw;
    draft->keywords << kw.id;
    q->markIndexDirty();
    check(q->save(&err), L + QStringLiteral("saved after the changes"), err);
    const QDomDocument dom = scrivxDom(scriv);
    const QDomElement root = dom.documentElement();
    const QDomElement draftEl = itemEl(dom, QLatin1String(kDraft));
    const QDomElement cs = draftEl.firstChildElement(QStringLiteral("CorkboardAndOutliner")).firstChildElement(QStringLiteral("CorkboardSettings"));
    check(cs.firstChildElement(QStringLiteral("Arrangement")).text() == QLatin1String("Freeform"), L + QStringLiteral("the arrangement is written"));
    QDomElement ledgerCard, chartCard;
    for (QDomElement c = cs.firstChildElement(QStringLiteral("FreeformCorkboardDocuments")).firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        (c.text() == QLatin1String(kLedger) ? ledgerCard : chartCard) = c;
    check(attr(chartCard, "Position") == QLatin1String("2.000000,1.000000") && attr(chartCard, "TimeStamp") != QLatin1String("2026-11-12 08:21:00 +0100"),
          L + QStringLiteral("a card moved here: its place in card widths, with a new moment"), attr(chartCard, "Position") + QLatin1Char(' ') + attr(chartCard, "TimeStamp"));
    check(attr(ledgerCard, "Position") == QLatin1String("1.139394,0.363636") && attr(ledgerCard, "TimeStamp") == QLatin1String("2026-11-12 08:20:00 +0100"),
          L + QStringLiteral("a card not moved keeps the file's text of its place"), attr(ledgerCard, "Position"));
    {
        QStringList order;
        for (QDomElement c = draftEl.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            order << c.tagName();
        check(order.join(QLatin1String(" > ")) == QLatin1String("Title > MetaData > TextSettings > CorkboardAndOutliner > Keywords > Children"),
              L + QStringLiteral("a keyword added: the parts in Scrivener's order"), order.join(QLatin1String(" > ")));
        const QDomElement tideEl = itemEl(dom, QLatin1String(kTide));
        order.clear();
        for (QDomElement c = tideEl.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            order << c.tagName();
        check(order.join(QLatin1String(" > ")) == QLatin1String("Title > MetaData > TextSettings > Bookmarks")
                  && attr(tideEl.firstChildElement(QStringLiteral("Bookmarks")).firstChildElement(), "BinderUUID") == QLatin1String(kSteps)
                  && tideEl.firstChildElement(QStringLiteral("Bookmarks")).firstChildElement().text() == QLatin1String("Steps"),
              L + QStringLiteral("a bookmark added to a document without bookmarks"), order.join(QLatin1String(" > ")));
    }
    const QDomElement ledgerEl = itemEl(dom, QLatin1String(kLedger)), chartEl = itemEl(dom, QLatin1String(kChart));
    const QDomElement target = ledgerEl.firstChildElement(QStringLiteral("TextSettings")).firstChildElement(QStringLiteral("Target"));
    check(target.text() == QLatin1String("750") && attr(target, "Notify") == QLatin1String("Yes") && attr(target, "Type") == QLatin1String("Words"),
          L + QStringLiteral("a document's target changed here: the number written, the rest as Scrivener had it"));
    {
        QStringList marks;
        for (QDomElement b = chartEl.firstChildElement(QStringLiteral("Bookmarks")).firstChildElement(); !b.isNull(); b = b.nextSiblingElement())
            marks << (b.hasAttribute(QStringLiteral("Destination")) ? attr(b, "Destination") : attr(b, "BinderUUID")) + QLatin1Char('=') + b.text();
        check(marks == QStringList({QLatin1String(kLedger) + QStringLiteral("=Ledger"), QStringLiteral("https://example.org/tides=Tide tables"),
                                    QStringLiteral("https://example.org/almanac=Almanac")}),
              L + QStringLiteral("a bookmark to an outside address added here"), marks.join(QLatin1String(" | ")));
    }
    {
        const QDomElement pb = root.firstChildElement(QStringLiteral("ProjectBookmarks"));
        check(pb.elementsByTagName(QStringLiteral("Bookmark")).size() == 1 && attr(pb.firstChildElement(), "Destination") == QLatin1String("https://example.org/harbor"),
              L + QStringLiteral("a project bookmark removed here is gone, the other stays"));
    }
    const QDomElement dt = root.firstChildElement(QStringLiteral("ProjectTargets")).firstChildElement(QStringLiteral("DraftTarget"));
    check(attr(dt, "Deadline").startsWith(QLatin1String("2027-04-15")) && attr(dt, "IgnoreDeadline") == QLatin1String("No"),
          L + QStringLiteral("a deadline changed here"), attr(dt, "Deadline"));
    {
        const QDomElement st = root.firstChildElement(QStringLiteral("ProjectTargets")).firstChildElement(QStringLiteral("SessionTarget"));
        check(attr(st, "CountDraftOnly") == QLatin1String("No") && attr(st, "DeterminedFromDeadline") == QLatin1String("No")
                  && attr(st, "ResetTime") == QLatin1String("00:00"),
              L + QStringLiteral("a session target option changed here, the others as Scrivener had them"));
        // the saved searches
        QStringList searches;
        for (QDomElement c = root.firstChildElement(QStringLiteral("Collections")).firstChildElement(QStringLiteral("Collection")); !c.isNull();
             c = c.nextSiblingElement(QStringLiteral("Collection")))
            if (attr(c, "Type") == QLatin1String("SavedSearch")) {
                const QDomElement ss = c.firstChildElement(QStringLiteral("SearchSettings"));
                searches << c.firstChildElement(QStringLiteral("Title")).text() + QLatin1Char('=') + ss.text() + QLatin1Char(' ')
                                + attr(ss, "Operator") + QLatin1Char(' ') + attr(ss, "CaseSensitive") + QLatin1Char(' ') + attr(ss, "Scope");
            }
        check(searches == QStringList({QStringLiteral("Harbor=harbor master Any No All"), QStringLiteral("Ropes=rope Any Yes All")}),
              L + QStringLiteral("saved searches: one changed here, one made here, in Scrivener's form"), searches.join(QLatin1String(" | ")));
        // the card pictures
        const QDomElement tideMeta = itemEl(dom, QLatin1String(kTide)).firstChildElement(QStringLiteral("MetaData"));
        const QDomElement stepsMeta = itemEl(dom, QLatin1String(kSteps)).firstChildElement(QStringLiteral("MetaData"));
        check(tideMeta.firstChildElement(QStringLiteral("ShowSynopsisImage")).text() == QLatin1String("No")
                  && QFile::exists(textFile(scriv, kTide, "card-image.png")),
              L + QStringLiteral("a card picture taken off here: the card shows its synopsis in Scrivener (the file stays)"));
        QStringList stepsParts;
        for (QDomElement e = stepsMeta.firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
            stepsParts << e.tagName() + (e.tagName().contains(QLatin1String("Image")) ? QLatin1Char('=') + e.text() : QString());
        check(stepsParts.join(QLatin1Char(' ')) == QLatin1String("IncludeInCompile ShowSynopsisImage=Yes IndexCardImageFileExtension=png FileExtension")
                  && readAll(textFile(scriv, kSteps, "card-image.png")) == pngBytes(QColor(240, 200, 20), 24, 16),
              L + QStringLiteral("a card picture put on here: the file and its switch in Scrivener's form"), stepsParts.join(QLatin1Char(' ')));
        QDomDocument cd;
        cd.setContent(readAll(scriv + QLatin1String("/Settings/compile.xml")));
        const QDomElement md = cd.documentElement().firstChildElement(QStringLiteral("ProjectSettings")).firstChildElement(QStringLiteral("MetaData"));
        check(md.firstChildElement(QStringLiteral("Forename")).text() == QLatin1String("Ann Marie")
                  && md.firstChildElement(QStringLiteral("Surname")).text() == QLatin1String("Writer")
                  && md.firstChildElement(QStringLiteral("ProjectAbbreviatedTitle")).text() == QLatin1String("Quay"),
              L + QStringLiteral("the author changed here goes into Scrivener's compile settings, the rest stays"));
        const QByteArray hist = readAll(scriv + QLatin1String("/Files/writing.history"));
        check(hist == "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<WritingHistory>\n"
                      "    <Day dwc=\"-3\" dcc=\"-20\" dtwc=\"1200\" owc=\"0\" occ=\"-5\" dtcc=\"6800\">2026-11-10</Day>\n"
                      "    <Day dwc=\"310\" dcc=\"1720\" owc=\"12\" occ=\"60\">2026-11-11</Day>\n"
                      "    <Day dwc=\"95\" dcc=\"530\" owc=\"0\" occ=\"0\">2026-11-12</Day>\n</WritingHistory>\n",
              L + QStringLiteral("a day of writing added here goes into the history, in Scrivener's form"), QString::fromUtf8(hist));
    }
    {
        const QDomElement kws = root.firstChildElement(QStringLiteral("Keywords"));
        QString id;
        for (QDomElement k = kws.firstChildElement(QStringLiteral("Keyword")); !k.isNull(); k = k.nextSiblingElement(QStringLiteral("Keyword")))
            if (k.firstChildElement(QStringLiteral("Title")).text() == QLatin1String("Harbor"))
                id = attr(k, "ID");
        check(!id.isEmpty() && draftEl.firstChildElement(QStringLiteral("Keywords")).firstChildElement().text() == id,
              L + QStringLiteral("the keyword made here is the container's"));
    }
    // read again (as after Scrivener changed the project): the same
    {
        ScrivenerImportReport rep;
        auto back = ScrivenerImporter::importProject(scriv, W + QLatin1String("/exchange-back"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
        check(back && back->item(QLatin1String(kChart))->corkboardPos == QStringLiteral("%1,%2").arg(unit * 2).arg(unit)
                  && back->item(QLatin1String(kChart))->bookmarks.size() == 3 && back->item(QLatin1String(kLedger))->target == 750
                  && back->targets.deadline == QDate(2027, 4, 15) && back->item(QLatin1String(kDraft))->corkArrangement == QLatin1String("freeform")
                  && back->history.size() == 3 && back->targets.countAnywhere
                  && back->item(QLatin1String(kSteps))->synopsisImage == QLatin1String("card.png") && back->item(QLatin1String(kTide))->synopsisImage.isEmpty()
                  && std::count_if(back->collections.cbegin(), back->collections.cend(), [](const Collection &c) { return c.isSearch; }) == 2,
              L + QStringLiteral("read back: the places, bookmarks, target, deadline, arrangement, history and options"), err);
    }
    // saved again without a change: nothing written
    {
        const QMap<QString, QByteArray> before = filesOf(scriv);
        check(q->save(&err), L + QStringLiteral("saved again"), err);
        QMap<QString, QByteArray> after = filesOf(scriv);
        check(differences(before, after).isEmpty(), L + QStringLiteral("a save after it without a change writes nothing"),
              differences(before, after) + QLatin1String(": ")
                  + projectFileChanges(before.value(QStringLiteral("Quay.scrivx")), after.value(QStringLiteral("Quay.scrivx"))));
    }
    Q_UNUSED(scrivxBefore);
}

// ------------------------------------------------------------------------------------------------------------------
// A copy of a real project (QB_SCRIV_REAL): the same, with every text it has

static void realProject(const QString &source, const QString &W)
{
    const QString L = QStringLiteral("real project: ");
    const QString scriv = W + QLatin1String("/real/") + QFileInfo(source).fileName();
    copyTree(source, scriv);
    const QMap<QString, QByteArray> before = filesOf(scriv);
    QString err;
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("opened"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    {
        // what the file holds of the kinds 0.90.0 reads, counted in the .scrivx, and what arrived here (counts only:
        // the project is never named, nor its titles)
        QFile f(scriv + QLatin1Char('/') + QDir(scriv).entryList({QStringLiteral("*.scrivx")}, QDir::Files).value(0));
        f.open(QIODevice::ReadOnly);
        QDomDocument d;
        d.setContent(f.readAll());
        int searches = 0, pictures = 0, freeform = 0, itemMarks = 0;
        const QDomNodeList cols = d.elementsByTagName(QStringLiteral("Collection"));
        for (int i = 0; i < cols.size(); ++i)
            searches += cols.at(i).toElement().attribute(QStringLiteral("Type")) == QLatin1String("SavedSearch");
        const QDomNodeList shows = d.elementsByTagName(QStringLiteral("ShowSynopsisImage"));
        for (int i = 0; i < shows.size(); ++i) {
            // Scrivener also sets the switch on pictures and media documents (their card shows the document): a
            // card picture is the switch with a card-image file in the document's folder
            const QString uuid = shows.at(i).parentNode().parentNode().toElement().attribute(QStringLiteral("UUID"));
            pictures += shows.at(i).toElement().text() == QLatin1String("Yes")
                        && !QDir(scriv + QLatin1String("/Files/Data/") + uuid).entryList({QStringLiteral("card-image.*")}, QDir::Files).isEmpty();
        }
        const QDomNodeList arr = d.elementsByTagName(QStringLiteral("Arrangement"));
        for (int i = 0; i < arr.size(); ++i)
            freeform += arr.at(i).toElement().text() == QLatin1String("Freeform");
        const QDomNodeList bms = d.elementsByTagName(QStringLiteral("Bookmark"));
        for (int i = 0; i < bms.size(); ++i)
            itemMarks += bms.at(i).parentNode().parentNode().nodeName() == QLatin1String("BinderItem");
        int searchesHere = 0, picturesHere = 0, freeformHere = 0, marksHere = 0;
        for (const Collection &c : q->collections)
            searchesHere += c.isSearch;
        for (BinderItem *it : q->allItems()) {
            picturesHere += !it->synopsisImage.isEmpty();
            freeformHere += it->corkArrangement == QLatin1String("freeform");
            marksHere += int(it->bookmarks.size());
        }
        std::printf("        (the file: %d saved searches, %d card pictures, %d freeform corkboards, %d document bookmarks)\n",
                    searches, pictures, freeform, itemMarks);
        check(searchesHere >= searches && picturesHere == pictures && freeformHere == freeform && marksHere == itemMarks,
              L + QStringLiteral("saved searches, card pictures, freeform corkboards and document bookmarks arrived"),
              QStringLiteral("here: %1 searches, %2 pictures, %3 freeform, %4 bookmarks").arg(searchesHere).arg(picturesHere).arg(freeformHere).arg(marksHere));
    }
    check(q->save(&err), L + QStringLiteral("saved without a change"), err);
    {
        QMap<QString, QByteArray> after = filesOf(scriv);
        for (const QString &k : after.keys())
            if (k.endsWith(QLatin1String("quillbinder.lock")) || (k == QLatin1String("Files/search.indexes") && !before.contains(k)))
                after.remove(k);
        check(differences(before, after).isEmpty(), L + QStringLiteral("nothing changed"), differences(before, after));
    }
    writeAllAnew(q);
    check(q->save(&err), L + QStringLiteral("saved with every text written anew"), err);
    const QMap<QString, QByteArray> after = filesOf(scriv);
    int texts = 0, written = 0, n = 0;
    QStringList unlikeTexts;
    Measure total, totalAfter;
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!it.key().endsWith(QLatin1String("/content.rtf")) || !it.key().startsWith(QLatin1String("Files/Data/")))
            continue;
        ++texts;
        written += after.value(it.key()) != it.value();
        const Measure a = measure(it.value()), b = measure(after.value(it.key()));
        for (auto m = a.cbegin(); m != a.cend(); ++m)
            total[m.key()] += m.value();
        for (auto m = b.cbegin(); m != b.cend(); ++m)
            totalAfter[m.key()] += m.value();
        const QString u = unlike(a, b);
        if (!u.isEmpty())
            unlikeTexts << QStringLiteral("text %1: ").arg(++n) + u;   // numbered, never named
    }
    std::printf("        (%d texts, %d written anew, %d characters, %d pictures, %d paragraphs)\n", texts, written,
                int(total.value(QStringLiteral("characters"))), int(total.value(QStringLiteral("pictures"))),
                int(total.value(QStringLiteral("paragraphs"))));
    check(written == texts, L + QStringLiteral("every text was written anew"), QStringLiteral("%1 of %2").arg(written).arg(texts));
    check(unlikeTexts.isEmpty(), L + QStringLiteral("written anew, every text adds up to what Scrivener wrote"),
          QStringLiteral("%1 texts differ; in all: ").arg(unlikeTexts.size()) + unlike(total, totalAfter) + QLatin1String("\n        ")
              + unlikeTexts.mid(0, 5).join(QLatin1String("\n        ")));
    if (const QString keep = qEnvironmentVariable("QB_V0900_KEEP"); !keep.isEmpty())
        copyTree(scriv, keep + QLatin1String("/written.scriv"));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + QLatin1String("/cache")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.90.0")) >= 0,
          QStringLiteral("version 0.90.0 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    AppSettings::instance().load();
    AppSettings::instance().backupOnOpen = false;
    AppSettings::instance().backupOnClose = false;
    AppSettings::instance().save();
    QTemporaryDir work;
    const QString W = work.path();

    // the test's own reader on a text it knows
    {
        const Measure m = measure(readAll(textFile(makeProject(W + QLatin1String("/own")), kLedger)));
        check(m.value(QStringLiteral("paragraphs")) == 6 && m.value(QStringLiteral("list paragraphs")) == 2
                  && m.value(QStringLiteral("bold characters")) == 29 && m.value(QStringLiteral("italic characters")) == 28
                  && m.value(QStringLiteral("bold by switch and by face name")) == 10 && m.value(QStringLiteral("underlined characters of links")) == 15
                  && m.value(QStringLiteral("right indents, twips")) == -1829 && m.value(QStringLiteral("highlighted characters")) == 11
                  && m.value(QStringLiteral("struck-through characters")) == 11 && m.value(QStringLiteral("links: https")) == 1
                  && m.value(QStringLiteral("links: scrivlnk")) == 1 && m.value(QStringLiteral("characters in color #0095df")) == 10
                  && m.value(QStringLiteral("tab stops")) == 6,
              QStringLiteral("the test's own reader adds a known text up right"), unlike(m, Measure()));
        const Measure c = measure(readAll(textFile(W + QLatin1String("/own/Quay.scriv"), kChart)));
        check(c.value(QStringLiteral("pictures")) == 2 && c.value(QStringLiteral("pictures inside a link")) == 1
                  && c.value(QStringLiteral("picture widths, twips")) == 1800 && c.value(QStringLiteral("links: file")) == 1,
              QStringLiteral("the test's own reader counts pictures and the link around one"), unlike(c, Measure()));
    }
    listStyles();
    texts(W);
    exchange(W);
    projectFile(W);   // last: it switches the interface's language
    const QString real = qEnvironmentVariable("QB_SCRIV_REAL");
    if (!real.isEmpty())
        realProject(real, W);

    std::printf("%d failure(s)\n", failures);
    return failures;
}
