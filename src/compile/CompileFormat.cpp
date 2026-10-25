#include "compile/CompileFormat.h"
#include "core/Addons.h"
#include "core/Paths.h"
#include "text/TextUtil.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace qb {
namespace compile {

const StyleOverride *Format::styleOverride(const QString &n) const
{
    if (n.isEmpty())
        return nullptr;
    for (const StyleOverride &o : styleOverrides)
        if (o.style.compare(n, Qt::CaseInsensitive) == 0)
            return &o;
    return nullptr;
}

const SectionLayout *Format::layout(const QString &n) const
{
    for (const SectionLayout &l : layouts)
        if (l.name.compare(n, Qt::CaseInsensitive) == 0)
            return &l;
    return nullptr;
}

QList<PaperSize> paperSizes()
{
    return {{QStringLiteral("A4"), 210, 297},
            {QStringLiteral("A5"), 148, 210},
            {QStringLiteral("US Letter"), 215.9, 279.4},
            {QStringLiteral("US Trade 6 x 9 in"), 152.4, 228.6},
            {QStringLiteral("5.5 x 8.5 in"), 139.7, 215.9},
            {QStringLiteral("5.06 x 7.81 in"), 128.5, 198.4},
            {QStringLiteral("5 x 8 in"), 127, 203.2}};
}

static SectionLayout lay(const QString &name)
{
    SectionLayout l;
    l.name = name;
    return l;
}

static QList<SectionLayout> commonLayouts(const QString &separator, bool upperChapter, bool firstNoIndent)
{
    QList<SectionLayout> l;
    SectionLayout s;

    s = lay(QStringLiteral("Part Title"));
    s.includeTitle = true;
    s.showItemTitle = true;
    s.titlePrefix = QObject::tr("Part <$W:part>") + QLatin1Char('\n');
    s.titleStyle = Paragraph::Title;
    s.pageBreakBefore = true;
    s.blankLinesBeforeTitle = 6;
    s.titleUppercase = upperChapter;
    l << s;

    s = lay(QStringLiteral("Chapter Heading"));
    s.includeTitle = true;
    s.showItemTitle = false;
    s.titlePrefix = QObject::tr("Chapter <$W:chapter>");
    s.titleSuffix = QString();
    s.pageBreakBefore = true;
    s.blankLinesBeforeTitle = 4;
    s.titleUppercase = upperChapter;
    s.noIndentFirstParagraph = firstNoIndent;
    s.titleStyle = Paragraph::Heading1;
    l << s;

    s = lay(QStringLiteral("Chapter with Title"));
    s.includeTitle = true;
    s.titlePrefix = QObject::tr("Chapter <$W:chapter>") + QLatin1Char('\n');
    s.pageBreakBefore = true;
    s.blankLinesBeforeTitle = 4;
    s.titleUppercase = upperChapter;
    s.noIndentFirstParagraph = firstNoIndent;
    l << s;

    s = lay(QStringLiteral("Chapter Title Only"));
    s.includeTitle = true;
    s.pageBreakBefore = true;
    s.blankLinesBeforeTitle = 4;
    s.titleUppercase = upperChapter;
    s.noIndentFirstParagraph = firstNoIndent;
    l << s;

    s = lay(QStringLiteral("Section Text"));
    s.separator = separator;
    s.noIndentFirstParagraph = firstNoIndent;
    l << s;

    s = lay(QStringLiteral("Section with Title"));
    s.includeTitle = true;
    s.titleStyle = Paragraph::Heading2;
    s.titleAlign = Qt::AlignLeft;
    s.noIndentFirstParagraph = true;
    l << s;

    s = lay(QStringLiteral("Subsection with Title"));
    s.includeTitle = true;
    s.titleStyle = Paragraph::Heading3;
    s.titleAlign = Qt::AlignLeft;
    s.noIndentFirstParagraph = true;
    l << s;

    s = lay(QStringLiteral("Heading"));
    s.includeTitle = true;
    s.titleStyle = Paragraph::Heading2;
    s.titleAlign = Qt::AlignLeft;
    s.noIndentFirstParagraph = true;
    l << s;

    s = lay(QStringLiteral("Title Page"));
    s.noIndentFirstParagraph = true;
    l << s;

    s = lay(QStringLiteral("Text"));
    l << s;

    s = lay(QStringLiteral("Synopsis Outline"));
    s.includeTitle = true;
    s.titleStyle = Paragraph::Heading2;
    s.titleAlign = Qt::AlignLeft;
    s.includeText = false;
    s.includeSynopsis = true;
    l << s;
    return l;
}

static QMap<QString, QString> fictionAssignments()
{
    return {{QStringLiteral("part"), QStringLiteral("Part Title")},
            {QStringLiteral("chapter"), QStringLiteral("Chapter Heading")},
            {QStringLiteral("scene"), QStringLiteral("Section Text")},
            {QStringLiteral("section"), QStringLiteral("Section Text")},
            {QStringLiteral("text"), QStringLiteral("Section Text")},
            {QStringLiteral("heading"), QStringLiteral("Heading")},
            {QStringLiteral("subsection"), QStringLiteral("Subsection with Title")},
            {QStringLiteral("title page"), QStringLiteral("Title Page")}};
}

QList<Format> builtinFormats()
{
    QList<Format> out;
    Format f;

    f = Format();
    f.name = QStringLiteral("Manuscript (Times)");
    f.builtin = true;
    f.fontFamily = QStringLiteral("Times New Roman");
    f.fontSize = 12;
    f.lineSpacing = 2.0;
    f.firstLineIndentMm = 12.7;
    f.page.headerRight = QStringLiteral("<$surname> / <$shorttitle> / <$p>");
    f.layouts = commonLayouts(QStringLiteral("#"), false, false);
    f.defaultAssignments = fictionAssignments();
    out << f;

    f.name = QStringLiteral("Manuscript (Courier)");
    f.fontFamily = QStringLiteral("Courier New");
    f.layouts = commonLayouts(QStringLiteral("#"), true, false);
    out << f;

    f = Format();
    f.name = QStringLiteral("Modern");
    f.builtin = true;
    f.headingScale = 1.5;
    f.headingBold = true;
    f.fontFamily = QStringLiteral("Georgia");
    f.headingFontFamily = QStringLiteral("Arial");
    f.fontSize = 11;
    f.lineSpacing = 1.15;
    f.firstLineIndentMm = 0;
    f.paragraphSpacingPt = 8;
    f.page.footerCenter = QStringLiteral("<$p>");
    f.layouts = commonLayouts(QStringLiteral("blank"), false, true);
    {
        SectionLayout s = lay(QStringLiteral("Numbered Chapter"));
        s.includeTitle = true;
        s.titlePrefix = QStringLiteral("<$n:chapter>. ");
        s.titleAlign = Qt::AlignLeft;
        s.pageBreakBefore = true;
        s.noIndentFirstParagraph = true;
        f.layouts << s;
    }
    f.defaultAssignments = fictionAssignments();
    f.defaultAssignments[QStringLiteral("chapter")] = QStringLiteral("Numbered Chapter");
    f.defaultAssignments[QStringLiteral("section")] = QStringLiteral("Section with Title");
    f.defaultAssignments[QStringLiteral("heading")] = QStringLiteral("Heading");
    out << f;

    f = Format();
    f.name = QStringLiteral("Paperback (6 x 9 in)");
    f.builtin = true;
    f.headingScale = 1.4;
    f.fontFamily = QStringLiteral("Georgia");
    f.fontSize = 11;
    f.lineSpacing = 1.25;
    f.firstLineIndentMm = 6;
    f.justify = true;
    f.page.widthMm = 152.4;
    f.page.heightMm = 228.6;
    f.page.marginTopMm = f.page.marginBottomMm = 19;
    f.page.marginLeftMm = 22;
    f.page.marginRightMm = 16;
    f.page.mirrorMargins = true;
    f.page.footerCenter = QStringLiteral("<$p>");
    f.page.headerLeft = QStringLiteral("<$author>");
    f.page.headerRight = QStringLiteral("<$projecttitle>");
    f.page.mirrorHeaders = true;
    f.page.headerOnChapterPages = false;
    f.page.chaptersOnRightPage = true;
    f.layouts = commonLayouts(QStringLiteral("* * *"), false, true);
    f.defaultAssignments = fictionAssignments();
    out << f;

    // e-books: no page settings to speak of, a table of contents, files per chapter
    f = Format();
    f.name = QStringLiteral("E-book");
    f.builtin = true;
    f.headingScale = 1.5;
    f.headingBold = true;
    f.fontFamily = QStringLiteral("Georgia");
    f.fontSize = 12;
    f.lineSpacing = 1.3;
    f.firstLineIndentMm = 5;
    f.justify = true;
    f.toc = true;
    f.tocLevels = 2;
    f.layouts = commonLayouts(QStringLiteral("* * *"), false, true);
    f.defaultAssignments = fictionAssignments();
    f.defaultAssignments[QStringLiteral("chapter")] = QStringLiteral("Chapter with Title");
    out << f;

    f = Format();
    f.name = QStringLiteral("Default (Editor Formatting)");
    f.builtin = true;
    f.headingScale = 1.5;
    f.headingBold = true;
    f.useEditorFormatting = true;
    f.layouts = commonLayouts(QStringLiteral("blank"), false, false);
    f.defaultAssignments = fictionAssignments();
    f.defaultAssignments[QStringLiteral("chapter")] = QStringLiteral("Chapter Title Only");
    f.lineSpacing = 1.2;
    out << f;

    f = Format();
    f.name = QStringLiteral("Outline (Synopses)");
    f.builtin = true;
    f.headingScale = 1.2;
    f.headingBold = true;
    f.fontFamily = QStringLiteral("Arial");
    f.fontSize = 11;
    f.lineSpacing = 1.15;
    f.firstLineIndentMm = 0;
    f.paragraphSpacingPt = 6;
    f.layouts = commonLayouts(QStringLiteral("none"), false, true);
    f.defaultAssignments.clear();
    f.defaultAssignments[QStringLiteral("*")] = QStringLiteral("Synopsis Outline");
    out << f;
    return out;
}

QString userFormatsDir()
{
    return dataDir() + QLatin1String("/CompileFormats");
}

QList<Format> userFormats()
{
    QList<Format> out;
    const QDir dir(userFormatsDir());
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        Format fm = Format::fromJson(QJsonDocument::fromJson(f.readAll()).object());
        if (!fm.name.isEmpty())
            out << fm;
    }
    return out;
}

// Formats installed by add-ons; read-only like the built-in ones.
QList<Format> addonFormats()
{
    QList<Format> out;
    for (const QString &d : addons::contentDirs(QStringLiteral("compile-formats"))) {
        const QDir dir(d);
        for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            Format fm = Format::fromJson(QJsonDocument::fromJson(f.readAll()).object());
            if (fm.name.isEmpty())
                continue;
            fm.builtin = true;
            out << fm;
        }
    }
    return out;
}

QList<Format> allFormats()
{
    QList<Format> all = builtinFormats();
    QStringList names;
    for (const Format &f : all)
        names << f.name;
    for (const Format &f : addonFormats() + userFormats()) {
        if (names.contains(f.name))
            continue; // first one wins: built-in, add-on, then the user's own
        names << f.name;
        all << f;
    }
    return all;
}

Format formatByName(const QString &name)
{
    for (const Format &f : allFormats())
        if (f.name == name)
            return f;
    return builtinFormats().first();
}

bool saveUserFormat(const Format &f, QString *error)
{
    QDir().mkpath(userFormatsDir());
    QSaveFile file(QDir(userFormatsDir()).filePath(text::safeFileName(f.name) + QLatin1String(".json")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    Format copy = f;
    copy.builtin = false;
    file.write(QJsonDocument(copy.toJson()).toJson());
    return file.commit();
}

bool exportFormat(const Format &f, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonObject o = f.toJson();
    o[QStringLiteral("kind")] = QStringLiteral("quillbinder-compile-format");
    file.write(QJsonDocument(o).toJson());
    return file.commit();
}

bool importFormat(const QString &path, Format *f, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(file.readAll(), &pe);
    if (!d.isObject() || pe.error != QJsonParseError::NoError) {
        if (error)
            *error = QObject::tr("This is not a compile format file.");
        return false;
    }
    *f = Format::fromJson(d.object());
    if (f->name.isEmpty()) {
        if (error)
            *error = QObject::tr("The format has no name.");
        return false;
    }
    return true;
}

bool deleteUserFormat(const QString &name)
{
    return QFile::remove(QDir(userFormatsDir()).filePath(text::safeFileName(name) + QLatin1String(".json")));
}

static QString alignToString(Qt::Alignment a)
{
    if (a & Qt::AlignHCenter)
        return QStringLiteral("center");
    if (a & Qt::AlignRight)
        return QStringLiteral("right");
    return QStringLiteral("left");
}

static Qt::Alignment alignFromString(const QString &s)
{
    if (s == QLatin1String("center"))
        return Qt::AlignHCenter;
    if (s == QLatin1String("right"))
        return Qt::AlignRight;
    return Qt::AlignLeft;
}

QJsonObject Format::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("useEditorFormatting")] = useEditorFormatting;
    o[QStringLiteral("font")] = fontFamily;
    o[QStringLiteral("headingFont")] = headingFontFamily;
    o[QStringLiteral("fontSize")] = fontSize;
    o[QStringLiteral("lineSpacing")] = lineSpacing;
    o[QStringLiteral("indentMm")] = firstLineIndentMm;
    o[QStringLiteral("spacingPt")] = paragraphSpacingPt;
    o[QStringLiteral("justify")] = justify;
    o[QStringLiteral("headingScale")] = headingScale;
    o[QStringLiteral("headingBold")] = headingBold;
    QJsonObject pg;
    pg[QStringLiteral("w")] = page.widthMm;
    pg[QStringLiteral("h")] = page.heightMm;
    pg[QStringLiteral("top")] = page.marginTopMm;
    pg[QStringLiteral("bottom")] = page.marginBottomMm;
    pg[QStringLiteral("left")] = page.marginLeftMm;
    pg[QStringLiteral("right")] = page.marginRightMm;
    pg[QStringLiteral("mirror")] = page.mirrorMargins;
    pg[QStringLiteral("headerLeft")] = page.headerLeft;
    pg[QStringLiteral("headerCenter")] = page.headerCenter;
    pg[QStringLiteral("headerRight")] = page.headerRight;
    pg[QStringLiteral("headerFirst")] = page.headerOnFirstPage;
    pg[QStringLiteral("footerCenter")] = page.footerCenter;
    pg[QStringLiteral("footerLeft")] = page.footerLeft;
    pg[QStringLiteral("footerRight")] = page.footerRight;
    pg[QStringLiteral("mirrorHeaders")] = page.mirrorHeaders;
    pg[QStringLiteral("headerOnChapterPages")] = page.headerOnChapterPages;
    pg[QStringLiteral("chaptersRight")] = page.chaptersOnRightPage;
    pg[QStringLiteral("firstNumber")] = page.firstPageNumber;
    pg[QStringLiteral("numberStyle")] = page.pageNumberStyle;
    pg[QStringLiteral("headerSize")] = page.headerFontSize;
    o[QStringLiteral("page")] = pg;
    QJsonArray so;
    for (const StyleOverride &x : styleOverrides) {
        QJsonObject e;
        e[QStringLiteral("style")] = x.style;
        e[QStringLiteral("mode")] = int(x.mode);
        if (!x.fontFamily.isEmpty())
            e[QStringLiteral("font")] = x.fontFamily;
        if (x.fontSize > 0)
            e[QStringLiteral("size")] = x.fontSize;
        e[QStringLiteral("bold")] = x.bold;
        e[QStringLiteral("italic")] = x.italic;
        e[QStringLiteral("underline")] = x.underline;
        e[QStringLiteral("caps")] = x.capitals;
        if (x.color.isValid())
            e[QStringLiteral("color")] = x.color.name();
        e[QStringLiteral("align")] = x.align;
        e[QStringLiteral("left")] = x.leftIndentMm;
        e[QStringLiteral("right")] = x.rightIndentMm;
        e[QStringLiteral("first")] = x.firstLineMm;
        e[QStringLiteral("before")] = x.spaceBeforePt;
        e[QStringLiteral("after")] = x.spaceAfterPt;
        e[QStringLiteral("line")] = x.lineSpacing;
        e[QStringLiteral("prefix")] = x.prefix;
        e[QStringLiteral("suffix")] = x.suffix;
        e[QStringLiteral("upper")] = x.uppercase;
        e[QStringLiteral("pageBreak")] = x.pageBreakBefore;
        so.append(e);
    }
    o[QStringLiteral("styleOverrides")] = so;
    QJsonArray rp;
    for (const Replacement &r : replacements) {
        QJsonObject e;
        e[QStringLiteral("find")] = r.find;
        e[QStringLiteral("replace")] = r.replace;
        e[QStringLiteral("regex")] = r.regex;
        e[QStringLiteral("case")] = r.matchCase;
        e[QStringLiteral("words")] = r.wholeWords;
        e[QStringLiteral("on")] = r.enabled;
        rp.append(e);
    }
    o[QStringLiteral("replacements")] = rp;
    QJsonObject tr;
    tr[QStringLiteral("straightQuotes")] = transform.straightQuotes;
    tr[QStringLiteral("smartQuotes")] = transform.smartQuotes;
    tr[QStringLiteral("dashes")] = transform.dashesToHyphens;
    tr[QStringLiteral("italicsToUnderline")] = transform.italicsToUnderline;
    tr[QStringLiteral("collapseSpaces")] = transform.collapseSpaces;
    tr[QStringLiteral("trailingSpaces")] = transform.removeTrailingSpaces;
    tr[QStringLiteral("plainText")] = transform.plainText;
    o[QStringLiteral("transform")] = tr;
    o[QStringLiteral("toc")] = toc;
    o[QStringLiteral("tocTitle")] = tocTitle;
    o[QStringLiteral("tocLevels")] = tocLevels;
    o[QStringLiteral("footnotes")] = footnotePlacement;
    o[QStringLiteral("endnotesTitle")] = endnotesTitle;
    o[QStringLiteral("footnoteSize")] = footnoteSize;
    o[QStringLiteral("pdfBookmarks")] = pdfBookmarks;
    o[QStringLiteral("pdfLinks")] = pdfLinks;
    o[QStringLiteral("splitAtPageBreaks")] = splitAtPageBreaks;
    o[QStringLiteral("tocPage")] = tocPage;
    QJsonArray ls;
    for (const SectionLayout &l : layouts) {
        QJsonObject lo;
        lo[QStringLiteral("name")] = l.name;
        lo[QStringLiteral("title")] = l.includeTitle;
        lo[QStringLiteral("showTitle")] = l.showItemTitle;
        lo[QStringLiteral("prefix")] = l.titlePrefix;
        lo[QStringLiteral("suffix")] = l.titleSuffix;
        lo[QStringLiteral("upper")] = l.titleUppercase;
        lo[QStringLiteral("titleStyle")] = int(l.titleStyle);
        lo[QStringLiteral("titleAlign")] = alignToString(l.titleAlign);
        lo[QStringLiteral("text")] = l.includeText;
        lo[QStringLiteral("synopsis")] = l.includeSynopsis;
        lo[QStringLiteral("notes")] = l.includeNotes;
        lo[QStringLiteral("pageBreak")] = l.pageBreakBefore;
        lo[QStringLiteral("separator")] = l.separator;
        lo[QStringLiteral("noIndentFirst")] = l.noIndentFirstParagraph;
        lo[QStringLiteral("blankLines")] = l.blankLinesBeforeTitle;
        ls.append(lo);
    }
    o[QStringLiteral("layouts")] = ls;
    QJsonObject as;
    for (auto i = defaultAssignments.cbegin(); i != defaultAssignments.cend(); ++i)
        as[i.key()] = i.value();
    o[QStringLiteral("assignments")] = as;
    o[QStringLiteral("annotationsAsComments")] = keepAnnotationsAsComments;
    o[QStringLiteral("comments")] = keepComments;
    o[QStringLiteral("removeHighlights")] = removeHighlights;
    return o;
}

Format Format::fromJson(const QJsonObject &o)
{
    Format f;
    f.name = o.value(QStringLiteral("name")).toString();
    f.useEditorFormatting = o.value(QStringLiteral("useEditorFormatting")).toBool();
    f.fontFamily = o.value(QStringLiteral("font")).toString(f.fontFamily);
    f.headingFontFamily = o.value(QStringLiteral("headingFont")).toString();
    f.fontSize = o.value(QStringLiteral("fontSize")).toDouble(12);
    f.lineSpacing = o.value(QStringLiteral("lineSpacing")).toDouble(2.0);
    f.firstLineIndentMm = o.value(QStringLiteral("indentMm")).toDouble(12.7);
    f.paragraphSpacingPt = o.value(QStringLiteral("spacingPt")).toDouble(0);
    f.justify = o.value(QStringLiteral("justify")).toBool();
    f.headingScale = o.value(QStringLiteral("headingScale")).toDouble(1.0);
    f.headingBold = o.value(QStringLiteral("headingBold")).toBool();
    const QJsonObject pg = o.value(QStringLiteral("page")).toObject();
    f.page.widthMm = pg.value(QStringLiteral("w")).toDouble(210);
    f.page.heightMm = pg.value(QStringLiteral("h")).toDouble(297);
    f.page.marginTopMm = pg.value(QStringLiteral("top")).toDouble(25.4);
    f.page.marginBottomMm = pg.value(QStringLiteral("bottom")).toDouble(25.4);
    f.page.marginLeftMm = pg.value(QStringLiteral("left")).toDouble(25.4);
    f.page.marginRightMm = pg.value(QStringLiteral("right")).toDouble(25.4);
    f.page.mirrorMargins = pg.value(QStringLiteral("mirror")).toBool();
    f.page.headerLeft = pg.value(QStringLiteral("headerLeft")).toString();
    f.page.headerCenter = pg.value(QStringLiteral("headerCenter")).toString();
    f.page.headerRight = pg.value(QStringLiteral("headerRight")).toString();
    f.page.headerOnFirstPage = pg.value(QStringLiteral("headerFirst")).toBool();
    f.page.footerCenter = pg.value(QStringLiteral("footerCenter")).toString();
    f.page.footerLeft = pg.value(QStringLiteral("footerLeft")).toString();
    f.page.footerRight = pg.value(QStringLiteral("footerRight")).toString();
    f.page.mirrorHeaders = pg.value(QStringLiteral("mirrorHeaders")).toBool();
    f.page.headerOnChapterPages = pg.value(QStringLiteral("headerOnChapterPages")).toBool(true);
    f.page.chaptersOnRightPage = pg.value(QStringLiteral("chaptersRight")).toBool();
    f.page.firstPageNumber = pg.value(QStringLiteral("firstNumber")).toInt(1);
    f.page.pageNumberStyle = pg.value(QStringLiteral("numberStyle")).toInt(0);
    f.page.headerFontSize = pg.value(QStringLiteral("headerSize")).toDouble(0);
    for (const QJsonValue &v : o.value(QStringLiteral("styleOverrides")).toArray()) {
        const QJsonObject e = v.toObject();
        StyleOverride x;
        x.style = e.value(QStringLiteral("style")).toString();
        x.mode = StyleOverride::Mode(qBound(0, e.value(QStringLiteral("mode")).toInt(1), 3));
        x.fontFamily = e.value(QStringLiteral("font")).toString();
        x.fontSize = e.value(QStringLiteral("size")).toDouble(0);
        x.bold = e.value(QStringLiteral("bold")).toInt(-1);
        x.italic = e.value(QStringLiteral("italic")).toInt(-1);
        x.underline = e.value(QStringLiteral("underline")).toInt(-1);
        x.capitals = e.value(QStringLiteral("caps")).toInt(-1);
        if (e.contains(QStringLiteral("color")))
            x.color = QColor(e.value(QStringLiteral("color")).toString());
        x.align = e.value(QStringLiteral("align")).toInt(-1);
        x.leftIndentMm = e.value(QStringLiteral("left")).toDouble(-1);
        x.rightIndentMm = e.value(QStringLiteral("right")).toDouble(-1);
        x.firstLineMm = e.value(QStringLiteral("first")).toDouble(-1000);
        x.spaceBeforePt = e.value(QStringLiteral("before")).toDouble(-1);
        x.spaceAfterPt = e.value(QStringLiteral("after")).toDouble(-1);
        x.lineSpacing = e.value(QStringLiteral("line")).toDouble(0);
        x.prefix = e.value(QStringLiteral("prefix")).toString();
        x.suffix = e.value(QStringLiteral("suffix")).toString();
        x.uppercase = e.value(QStringLiteral("upper")).toBool();
        x.pageBreakBefore = e.value(QStringLiteral("pageBreak")).toBool();
        if (!x.style.isEmpty())
            f.styleOverrides << x;
    }
    for (const QJsonValue &v : o.value(QStringLiteral("replacements")).toArray()) {
        const QJsonObject e = v.toObject();
        Replacement r;
        r.find = e.value(QStringLiteral("find")).toString();
        r.replace = e.value(QStringLiteral("replace")).toString();
        r.regex = e.value(QStringLiteral("regex")).toBool();
        r.matchCase = e.value(QStringLiteral("case")).toBool(true);
        r.wholeWords = e.value(QStringLiteral("words")).toBool();
        r.enabled = e.value(QStringLiteral("on")).toBool(true);
        if (!r.find.isEmpty())
            f.replacements << r;
    }
    {
        const QJsonObject tr = o.value(QStringLiteral("transform")).toObject();
        f.transform.straightQuotes = tr.value(QStringLiteral("straightQuotes")).toBool();
        f.transform.smartQuotes = tr.value(QStringLiteral("smartQuotes")).toBool();
        f.transform.dashesToHyphens = tr.value(QStringLiteral("dashes")).toBool();
        f.transform.italicsToUnderline = tr.value(QStringLiteral("italicsToUnderline")).toBool();
        f.transform.collapseSpaces = tr.value(QStringLiteral("collapseSpaces")).toBool();
        f.transform.removeTrailingSpaces = tr.value(QStringLiteral("trailingSpaces")).toBool(true);
        f.transform.plainText = tr.value(QStringLiteral("plainText")).toBool();
    }
    f.toc = o.value(QStringLiteral("toc")).toBool();
    f.tocTitle = o.value(QStringLiteral("tocTitle")).toString(QStringLiteral("Contents"));
    f.tocLevels = qBound(1, o.value(QStringLiteral("tocLevels")).toInt(2), 6);
    f.footnotePlacement = qBound(0, o.value(QStringLiteral("footnotes")).toInt(0), 2);
    f.endnotesTitle = o.value(QStringLiteral("endnotesTitle")).toString(QStringLiteral("Notes"));
    f.footnoteSize = o.value(QStringLiteral("footnoteSize")).toDouble(0);
    f.pdfBookmarks = o.value(QStringLiteral("pdfBookmarks")).toBool(true);
    f.pdfLinks = o.value(QStringLiteral("pdfLinks")).toBool(true);
    f.splitAtPageBreaks = o.value(QStringLiteral("splitAtPageBreaks")).toBool(true);
    f.tocPage = o.value(QStringLiteral("tocPage")).toBool();
    for (const QJsonValue &v : o.value(QStringLiteral("layouts")).toArray()) {
        const QJsonObject lo = v.toObject();
        SectionLayout l;
        l.name = lo.value(QStringLiteral("name")).toString();
        l.includeTitle = lo.value(QStringLiteral("title")).toBool();
        l.showItemTitle = lo.value(QStringLiteral("showTitle")).toBool(true);
        l.titlePrefix = lo.value(QStringLiteral("prefix")).toString();
        l.titleSuffix = lo.value(QStringLiteral("suffix")).toString();
        l.titleUppercase = lo.value(QStringLiteral("upper")).toBool();
        l.titleStyle = Paragraph::Style(lo.value(QStringLiteral("titleStyle")).toInt(int(Paragraph::Heading1)));
        l.titleAlign = alignFromString(lo.value(QStringLiteral("titleAlign")).toString());
        l.includeText = lo.value(QStringLiteral("text")).toBool(true);
        l.includeSynopsis = lo.value(QStringLiteral("synopsis")).toBool();
        l.includeNotes = lo.value(QStringLiteral("notes")).toBool();
        l.pageBreakBefore = lo.value(QStringLiteral("pageBreak")).toBool();
        l.separator = lo.value(QStringLiteral("separator")).toString(QStringLiteral("none"));
        l.noIndentFirstParagraph = lo.value(QStringLiteral("noIndentFirst")).toBool();
        l.blankLinesBeforeTitle = lo.value(QStringLiteral("blankLines")).toInt();
        f.layouts << l;
    }
    const QJsonObject as = o.value(QStringLiteral("assignments")).toObject();
    for (auto i = as.begin(); i != as.end(); ++i)
        f.defaultAssignments.insert(i.key(), i.value().toString());
    f.keepAnnotationsAsComments = o.value(QStringLiteral("annotationsAsComments")).toBool();
    f.keepComments = o.value(QStringLiteral("comments")).toBool(true);
    f.removeHighlights = o.value(QStringLiteral("removeHighlights")).toBool(true);
    return f;
}

} // namespace compile
} // namespace qb
