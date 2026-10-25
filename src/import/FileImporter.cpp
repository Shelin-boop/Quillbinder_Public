#include "import/FileImporter.h"

#include "compile/PluginManager.h"
#include "import/ImportedContent.h"

#include "text/Script.h"
#include "text/ScriptIO.h"
#include "text/TextProps.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Zip.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QProcess>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>

namespace qb {
namespace importer {

static ImportOptions &importOptions();
static QString sofficeProgram();

// Suffixes the program reads itself.
static bool classifyBuiltIn(const QString &ext)
{
    static const QStringList known = {QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
                                      QStringLiteral("rtf"), QStringLiteral("docx"), QStringLiteral("odt"),
                                      QStringLiteral("fountain"), QStringLiteral("text"), QStringLiteral("mdown"),
                                      QStringLiteral("fdx"), QStringLiteral("spmd"), QStringLiteral("html"),
                                      QStringLiteral("htm")};
    return known.contains(ext);
}

FileKind classify(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QStringList text = {QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
                                     QStringLiteral("rtf"), QStringLiteral("docx"), QStringLiteral("odt"),
                                     QStringLiteral("fountain"), QStringLiteral("text"), QStringLiteral("mdown"),
                                     QStringLiteral("fdx"), QStringLiteral("spmd")};
    static const QStringList images = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                       QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
                                       QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("svg")};
    const QStringList &media = mediaSuffixes();
    if (text.contains(ext))
        return FileKind::Text;
    // read by an add-on (.doc …)
    if (compile::PluginManager::instance().importerFor(ext))
        return FileKind::Text;
    // old Word files that are really RTF, Word XML or a web page
    if (isLegacyWordSuffix(ext)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && !sniffFormat(f.peek(4096)).isEmpty())
            return FileKind::Text;
        // ... or read by LibreOffice (File > Options > Sharing > Conversion, 0.21)
        if (importOptions().docConverter == QLatin1String("libreoffice") && !sofficeProgram().isEmpty())
            return FileKind::Text;
    }
    if (images.contains(ext))
        return FileKind::Image;
    if (ext == QLatin1String("pdf"))
        return FileKind::Pdf;
    if (media.contains(ext))
        return FileKind::Media;
    if (ext == QLatin1String("html") || ext == QLatin1String("htm"))
        return importOptions().htmlAsText ? FileKind::Text : FileKind::WebPage;
    return FileKind::Other;
}

bool isLegacyWordSuffix(const QString &suffix)
{
    const QString ext = suffix.toLower();
    return ext == QLatin1String("doc") || ext == QLatin1String("dot");
}

QStringList textFileFilters()
{
    QString patterns = QStringLiteral("*.txt *.md *.markdown *.rtf *.docx *.odt *.fountain *.fdx");
    QStringList out;
    QStringList extra = compile::PluginManager::instance().importerSuffixes();
    if (importOptions().docConverter == QLatin1String("libreoffice") && !sofficeProgram().isEmpty())
        for (const char *e : {"doc", "dot"})   // read by LibreOffice (0.21)
            if (!extra.contains(QLatin1String(e)))
                extra << QLatin1String(e);
    for (const QString &e : extra)
        patterns += QStringLiteral(" *.") + e;
    out << QObject::tr("Text documents (%1)").arg(patterns);
    for (importers::ImporterInterface *i : compile::PluginManager::instance().importers()) {
        if (i->suffixes().isEmpty())
            continue;
        QStringList p;
        for (const QString &e : i->suffixes())
            p << QStringLiteral("*.") + e;
        out << QStringLiteral("%1 (%2)").arg(i->fileTypeName(), p.join(QLatin1Char(' ')));
    }
    out << QObject::tr("All files (*)");
    return out;
}

bool isScriptFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("fountain") || ext == QLatin1String("spmd") || ext == QLatin1String("fdx");
}

// ------------------------------------------------------------------ helpers

static ImportOptions &importOptions()
{
    static ImportOptions o;
    return o;
}

void setOptions(const ImportOptions &o) { importOptions() = o; }

static int s_rawReading = 0;
RawReading::RawReading() { ++s_rawReading; }
RawReading::~RawReading() { --s_rawReading; }
const ImportOptions &options() { return importOptions(); }

// The characters 0x80 ... 0x9F of Windows Western (cp1252); 0 = not used.
static const char16_t kCp1252[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                     0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                     0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

static QString decodeSingleByte(const QByteArray &data, const QString &encoding)
{
    QString out;
    out.reserve(data.size());
    const bool cp1252 = encoding == QLatin1String("windows-1252");
    const bool latin9 = encoding == QLatin1String("ISO-8859-15");
    for (const char ch : data) {
        const uchar b = uchar(ch);
        char16_t u = b;
        if (cp1252 && b >= 0x80 && b <= 0x9F && kCp1252[b - 0x80])
            u = kCp1252[b - 0x80];
        if (latin9) {   // the eight places where Latin-9 differs from Latin-1
            switch (b) {
            case 0xA4: u = 0x20AC; break;
            case 0xA6: u = 0x0160; break;
            case 0xA8: u = 0x0161; break;
            case 0xB4: u = 0x017D; break;
            case 0xB8: u = 0x017E; break;
            case 0xBC: u = 0x0152; break;
            case 0xBD: u = 0x0153; break;
            case 0xBE: u = 0x0178; break;
            default: break;
            }
        }
        out.append(QChar(u));
    }
    return out;
}

QString decodeText(const QByteArray &data, const QString &encoding)
{
    if (encoding == QLatin1String("UTF-8"))
        return QString::fromUtf8(data.startsWith("\xEF\xBB\xBF") ? data.mid(3) : data);
    if (encoding == QLatin1String("UTF-16")) {
        QStringDecoder dec(QStringConverter::Utf16);
        return dec(data);
    }
    if (encoding == QLatin1String("windows-1252") || encoding == QLatin1String("ISO-8859-1") || encoding == QLatin1String("ISO-8859-15"))
        return decodeSingleByte(data, encoding);
    // automatic
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.mid(3));
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) {
        QStringDecoder dec(QStringConverter::Utf16);
        return dec(data);
    }
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString s = utf8(data);
    if (!utf8.hasError())
        return s;
    return decodeSingleByte(data, QStringLiteral("windows-1252"));
}

static QString decodeText(const QByteArray &data)
{
    return decodeText(data, importOptions().encoding);
}

// File > Options > Sharing > Import: the files' styles left out, and comments and
// footnotes made inline or linked as chosen (0.21).
static void applyImportOptions(QTextDocument *doc, const Formatting &f, bool annotationsAreComments)
{
    if (s_rawReading > 0)
        return;
    const ImportOptions &o = importOptions();
    text::convertNotes(doc, o.commentsInline, o.footnotesInline, annotationsAreComments);
    if (!o.ignoreStyles)
        return;
    const QTextBlockFormat normal = display::normalBlockFormat(f);
    const QTextCharFormat normalChar = display::charFormatFor(f, QString());
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const bool styled = !b.blockFormat().stringProperty(props::Style).isEmpty();
        if (styled) {
            QTextCursor bc(b);
            QTextBlockFormat bf = normal;
            bf.setAlignment(b.blockFormat().alignment());
            bc.setBlockFormat(bf);
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            if (!styled && cf.stringProperty(props::CharStyle).isEmpty())
                continue;
            // the plain text formatting of the project, with bold, italic, notes, links ... kept
            QTextCharFormat out = normalChar;
            out.merge(display::semanticCharFormat(cf));
            out.clearProperty(props::CharStyle);
            QTextCursor fc(doc);
            fc.setPosition(fr.position());
            fc.setPosition(fr.position() + fr.length(), QTextCursor::KeepAnchor);
            fc.setCharFormat(out);
        }
    }
    c.endEditBlock();
}

static void insertPlain(QTextDocument *doc, const QString &text, const Formatting &f)
{
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QTextCursor c(doc);
    c.setBlockFormat(display::normalBlockFormat(f));
    const QStringList lines = t.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            c.insertBlock(display::normalBlockFormat(f));
        c.insertText(lines[i]);
    }
}

// ------------------------------------------------------------------ DOCX

namespace {

struct DRun {
    QString text;
    QTextCharFormat fmt;
    QImage image;
    QSizeF size;
};

struct DPara {
    QString style;
    bool center = false;
    bool pageBreak = false;
    int listLevel = -1;
    int listNum = 0;
    QList<DRun> runs;
};

QHash<QString, QString> readRels(const ZipReader &zr, const QString &path)
{
    QHash<QString, QString> map;
    QXmlStreamReader x(zr.fileData(path));
    while (!x.atEnd()) {
        if (x.readNext() == QXmlStreamReader::StartElement && x.name() == QLatin1String("Relationship"))
            map.insert(x.attributes().value(QLatin1String("Id")).toString(),
                       x.attributes().value(QLatin1String("Target")).toString());
    }
    return map;
}

QString docxPlainOfElement(QXmlStreamReader &x, const QString &endName)
{
    // collects w:t text until the end element (paragraph breaks -> \n)
    QString out;
    int paras = 0;
    while (!x.atEnd()) {
        const auto t = x.readNext();
        if (t == QXmlStreamReader::StartElement) {
            if (x.name() == QLatin1String("t"))
                out += x.readElementText();
            else if (x.name() == QLatin1String("tab"))
                out += QLatin1Char('\t');
            else if (x.name() == QLatin1String("p")) {
                if (paras++ > 0)
                    out += QLatin1Char('\n');
            }
        } else if (t == QXmlStreamReader::EndElement && x.name() == endName) {
            break;
        }
    }
    return out.trimmed();
}

bool attrOn(const QXmlStreamAttributes &a)
{
    const QString v = a.value(QLatin1String("w:val")).toString();
    return v.isEmpty() || !(v == QLatin1String("0") || v == QLatin1String("false") || v == QLatin1String("none"));
}

} // namespace

bool readDocx(const QString &path, QTextDocument *doc, const Formatting &f, QString *error)
{
    ZipReader zr(path);
    if (!zr.isValid() || !zr.fileNames().contains(QStringLiteral("word/document.xml"))) {
        if (error)
            *error = QObject::tr("%1 is not a valid Word document.").arg(QFileInfo(path).fileName());
        return false;
    }
    // styles: id -> name
    QHash<QString, QString> styleNames;
    {
        QXmlStreamReader x(zr.fileData(QStringLiteral("word/styles.xml")));
        QString curId;
        while (!x.atEnd()) {
            if (x.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (x.name() == QLatin1String("style"))
                curId = x.attributes().value(QLatin1String("w:styleId")).toString();
            else if (x.name() == QLatin1String("name") && !curId.isEmpty())
                styleNames.insert(curId, x.attributes().value(QLatin1String("w:val")).toString().toLower());
        }
    }
    // numbering: numId -> numbered?
    QHash<QString, bool> numbered;
    {
        QHash<QString, bool> abstractDecimal;
        QXmlStreamReader x(zr.fileData(QStringLiteral("word/numbering.xml")));
        QString curAbstract, curNum;
        bool firstLvl = false;
        while (!x.atEnd()) {
            const auto t = x.readNext();
            if (t != QXmlStreamReader::StartElement)
                continue;
            if (x.name() == QLatin1String("abstractNum")) {
                curAbstract = x.attributes().value(QLatin1String("w:abstractNumId")).toString();
                firstLvl = true;
            } else if (x.name() == QLatin1String("numFmt") && firstLvl && !curAbstract.isEmpty()) {
                abstractDecimal.insert(curAbstract, x.attributes().value(QLatin1String("w:val")) != QLatin1String("bullet"));
                firstLvl = false;
            } else if (x.name() == QLatin1String("num")) {
                curNum = x.attributes().value(QLatin1String("w:numId")).toString();
                curAbstract.clear();
            } else if (x.name() == QLatin1String("abstractNumId") && !curNum.isEmpty()) {
                numbered.insert(curNum, abstractDecimal.value(x.attributes().value(QLatin1String("w:val")).toString()));
            }
        }
    }
    auto notesMap = [&](const QString &file, const QString &elem) {
        QHash<QString, QString> map;
        QXmlStreamReader x(zr.fileData(file));
        while (!x.atEnd()) {
            if (x.readNext() == QXmlStreamReader::StartElement && x.name() == elem) {
                const QString id = x.attributes().value(QLatin1String("w:id")).toString();
                map.insert(id, docxPlainOfElement(x, elem));
            }
        }
        return map;
    };
    const QHash<QString, QString> footnotes = notesMap(QStringLiteral("word/footnotes.xml"), QStringLiteral("footnote"));
    const QHash<QString, QString> endnotes = notesMap(QStringLiteral("word/endnotes.xml"), QStringLiteral("endnote"));
    const QHash<QString, QString> comments = notesMap(QStringLiteral("word/comments.xml"), QStringLiteral("comment"));
    const QHash<QString, QString> rels = readRels(zr, QStringLiteral("word/_rels/document.xml.rels"));

    QList<DPara> paras;
    DPara cur;
    bool inPara = false;
    bool inRun = false, inRPr = false, inPPr = false, inDel = false;
    QTextCharFormat runFmt;
    QString href;
    QSet<QString> openComments;
    QHash<QString, QString> commentNoteIds;

    QXmlStreamReader x(zr.fileData(QStringLiteral("word/document.xml")));
    while (!x.atEnd()) {
        const auto t = x.readNext();
        const auto name = x.name();
        if (t == QXmlStreamReader::StartElement) {
            const QXmlStreamAttributes a = x.attributes();
            if (name == QLatin1String("p")) {
                cur = DPara();
                inPara = true;
            } else if (name == QLatin1String("pPr")) {
                inPPr = true;
            } else if (inPPr && name == QLatin1String("pStyle")) {
                cur.style = styleNames.value(a.value(QLatin1String("w:val")).toString(), a.value(QLatin1String("w:val")).toString().toLower());
            } else if (inPPr && name == QLatin1String("jc")) {
                cur.center = a.value(QLatin1String("w:val")) == QLatin1String("center");
            } else if (inPPr && name == QLatin1String("pageBreakBefore")) {
                cur.pageBreak = attrOn(a);
            } else if (inPPr && name == QLatin1String("ilvl")) {
                cur.listLevel = a.value(QLatin1String("w:val")).toInt();
            } else if (inPPr && name == QLatin1String("numId")) {
                const QString id = a.value(QLatin1String("w:val")).toString();
                if (id != QLatin1String("0")) {
                    if (cur.listLevel < 0)
                        cur.listLevel = 0;
                    cur.listNum = numbered.value(id) ? 2 : 1;
                } else {
                    cur.listLevel = -1;
                }
            } else if (name == QLatin1String("r")) {
                inRun = true;
                runFmt = QTextCharFormat();
                if (!href.isEmpty())
                    runFmt.merge(text::linkFormat(href));
                for (const QString &cid : openComments)
                    runFmt.merge(text::noteFormat(NoteKind::Comment, commentNoteIds.value(cid), comments.value(cid)));
            } else if (name == QLatin1String("rPr") && inRun) {
                inRPr = true;
            } else if (inRPr) {
                if (name == QLatin1String("b"))
                    runFmt.setFontWeight(attrOn(a) ? QFont::Bold : QFont::Normal);
                else if (name == QLatin1String("i"))
                    runFmt.setFontItalic(attrOn(a));
                else if (name == QLatin1String("u"))
                    runFmt.setFontUnderline(attrOn(a));
                else if (name == QLatin1String("strike") || name == QLatin1String("dstrike"))
                    runFmt.setFontStrikeOut(attrOn(a));
                else if (name == QLatin1String("vertAlign")) {
                    const auto v = a.value(QLatin1String("w:val"));
                    if (v == QLatin1String("superscript"))
                        runFmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
                    else if (v == QLatin1String("subscript"))
                        runFmt.setVerticalAlignment(QTextCharFormat::AlignSubScript);
                } else if (name == QLatin1String("highlight")) {
                    if (a.value(QLatin1String("w:val")) != QLatin1String("none"))
                        runFmt.merge(text::highlightFormat());
                }
            } else if (name == QLatin1String("del")) {
                inDel = true;
            } else if (name == QLatin1String("t") && inPara && !inDel) {
                cur.runs.append({x.readElementText(), runFmt, QImage(), QSizeF()});
            } else if (name == QLatin1String("tab") && inRun && !inPPr) {
                cur.runs.append({QStringLiteral("\t"), runFmt, QImage(), QSizeF()});
            } else if ((name == QLatin1String("br") || name == QLatin1String("cr")) && inRun) {
                if (a.value(QLatin1String("w:type")) == QLatin1String("page")) {
                    paras << cur;
                    cur = DPara();
                    cur.pageBreak = true;
                } else {
                    cur.runs.append({QString(QChar::LineSeparator), runFmt, QImage(), QSizeF()});
                }
            } else if (name == QLatin1String("footnoteReference") || name == QLatin1String("endnoteReference")) {
                const QString id = a.value(QLatin1String("w:id")).toString();
                const QString txt = name == QLatin1String("footnoteReference") ? footnotes.value(id) : endnotes.value(id);
                if (!txt.isEmpty())
                    cur.runs.append({txt, text::noteFormat(NoteKind::InlineFootnote,
                                                           QUuid::createUuid().toString(QUuid::WithoutBraces)),
                                     QImage(), QSizeF()});
            } else if (name == QLatin1String("commentRangeStart")) {
                const QString id = a.value(QLatin1String("w:id")).toString();
                openComments.insert(id);
                commentNoteIds.insert(id, QUuid::createUuid().toString(QUuid::WithoutBraces));
            } else if (name == QLatin1String("commentRangeEnd")) {
                openComments.remove(a.value(QLatin1String("w:id")).toString());
            } else if (name == QLatin1String("hyperlink")) {
                href = rels.value(a.value(QLatin1String("r:id")).toString());
            } else if (name == QLatin1String("blip")) {
                const QString target = rels.value(a.value(QLatin1String("r:embed")).toString());
                if (!target.isEmpty()) {
                    QImage img;
                    img.loadFromData(zr.fileData(QStringLiteral("word/") + target));
                    if (!img.isNull())
                        cur.runs.append({QString(), runFmt, img, QSizeF()});
                }
            }
        } else if (t == QXmlStreamReader::EndElement) {
            if (name == QLatin1String("p")) {
                paras << cur;
                inPara = false;
            } else if (name == QLatin1String("pPr")) {
                inPPr = false;
            } else if (name == QLatin1String("r")) {
                inRun = false;
            } else if (name == QLatin1String("rPr")) {
                inRPr = false;
            } else if (name == QLatin1String("del")) {
                inDel = false;
            } else if (name == QLatin1String("hyperlink")) {
                href.clear();
            }
        }
    }

    // build the document
    doc->clear();
    QTextCursor c(doc);
    bool first = true;
    QMap<int, QTextList *> lists;
    bool prevList = false;
    for (const DPara &p : paras) {
        QString style;
        if (p.style.startsWith(QLatin1String("heading "))) {
            style = styles::forHeadingLevel(p.style.mid(8).toInt());
        } else if (p.style == QLatin1String("title")) {
            style = QStringLiteral("Title");
        } else if (p.style == QLatin1String("subtitle")) {
            style = QStringLiteral("Subtitle");
        } else if (p.style.contains(QLatin1String("quote")) || p.style == QLatin1String("block text")) {
            style = QStringLiteral("Block Quote");
        }
        QTextBlockFormat bf = display::blockFormatFor(f, style);
        if (p.center && style.isEmpty()) {
            bf.setAlignment(Qt::AlignHCenter);
            bf.setTextIndent(0);
        }
        if (p.pageBreak)
            bf.setProperty(props::PageBreak, true);
        const QTextCharFormat scf = display::charFormatFor(f, style);
        if (first) {
            c.setBlockFormat(bf);
            c.setBlockCharFormat(scf);
            first = false;
        } else {
            c.insertBlock(bf, scf);
        }
        if (p.listLevel >= 0 && p.listNum) {
            if (!prevList)
                lists.clear();
            QTextList *l = lists.value(p.listLevel);
            if (l && (l->format().style() == QTextListFormat::ListDecimal) == (p.listNum == 2)) {
                l->add(c.block());
            } else {
                QTextListFormat lf;
                lf.setStyle(p.listNum == 2 ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
                lf.setIndent(p.listLevel + 1);
                lists[p.listLevel] = c.createList(lf);
            }
            prevList = true;
        } else {
            prevList = false;
        }
        for (const DRun &r : p.runs) {
            if (!r.image.isNull()) {
                const QString name = QStringLiteral("docximage://%1.png").arg(QUuid::createUuid().toString(QUuid::Id128));
                doc->addResource(QTextDocument::ImageResource, QUrl(name), r.image);
                QTextImageFormat imf;
                imf.setName(name);
                if (r.image.width() > 500) {
                    imf.setWidth(500);
                    imf.setHeight(r.image.height() * 500.0 / r.image.width());
                }
                c.insertImage(imf);
                continue;
            }
            QTextCharFormat cf = scf;
            cf.merge(r.fmt);
            c.insertText(r.text, cf);
        }
    }
    return true;
}

// ------------------------------------------------------------------ ODT

bool readOdt(const QString &path, QTextDocument *doc, const Formatting &f, QString *error)
{
    ZipReader zr(path);
    if (!zr.isValid() || !zr.fileNames().contains(QStringLiteral("content.xml"))) {
        if (error)
            *error = QObject::tr("%1 is not a valid OpenDocument text.").arg(QFileInfo(path).fileName());
        return false;
    }
    struct SProps {
        bool bold = false, italic = false, underline = false, strike = false, center = false;
        bool hasBold = false, hasItalic = false;
        QString parent;
    };
    QHash<QString, SProps> stylesMap;
    auto readStyles = [&](const QByteArray &xml) {
        QXmlStreamReader x(xml);
        QString cur;
        while (!x.atEnd()) {
            if (x.readNext() != QXmlStreamReader::StartElement)
                continue;
            const QXmlStreamAttributes a = x.attributes();
            if (x.name() == QLatin1String("style")) {
                cur = a.value(QLatin1String("style:name")).toString();
                stylesMap[cur].parent = a.value(QLatin1String("style:parent-style-name")).toString();
            } else if (x.name() == QLatin1String("text-properties") && !cur.isEmpty()) {
                SProps &s = stylesMap[cur];
                if (a.hasAttribute(QLatin1String("fo:font-weight"))) {
                    s.hasBold = true;
                    s.bold = a.value(QLatin1String("fo:font-weight")) == QLatin1String("bold");
                }
                if (a.hasAttribute(QLatin1String("fo:font-style"))) {
                    s.hasItalic = true;
                    s.italic = a.value(QLatin1String("fo:font-style")) == QLatin1String("italic");
                }
                if (a.hasAttribute(QLatin1String("style:text-underline-style")))
                    s.underline = a.value(QLatin1String("style:text-underline-style")) != QLatin1String("none");
                if (a.hasAttribute(QLatin1String("style:text-line-through-style")))
                    s.strike = a.value(QLatin1String("style:text-line-through-style")) != QLatin1String("none");
            } else if (x.name() == QLatin1String("paragraph-properties") && !cur.isEmpty()) {
                if (a.value(QLatin1String("fo:text-align")) == QLatin1String("center"))
                    stylesMap[cur].center = true;
            }
        }
    };
    readStyles(zr.fileData(QStringLiteral("styles.xml")));
    readStyles(zr.fileData(QStringLiteral("content.xml")));
    auto resolve = [&](const QString &name) {
        SProps out;
        QString n = name;
        int guard = 0;
        bool b = false, i = false;
        while (!n.isEmpty() && guard++ < 10) {
            const SProps s = stylesMap.value(n);
            if (s.hasBold && !b) {
                out.bold = s.bold;
                b = true;
            }
            if (s.hasItalic && !i) {
                out.italic = s.italic;
                i = true;
            }
            out.underline = out.underline || s.underline;
            out.strike = out.strike || s.strike;
            out.center = out.center || s.center;
            n = s.parent;
        }
        return out;
    };

    doc->clear();
    QTextCursor c(doc);
    bool firstBlock = true;
    QList<QTextCharFormat> fmtStack;
    int listDepth = 0;
    QMap<int, QTextList *> lists;
    int noteDepth = 0, annDepth = 0;
    QString noteText, annText;
    QString href;

    auto newBlock = [&](const QString &style, bool center) {
        QTextBlockFormat bf = display::blockFormatFor(f, style);
        if (center && style.isEmpty()) {
            bf.setAlignment(Qt::AlignHCenter);
            bf.setTextIndent(0);
        }
        const QTextCharFormat scf = display::charFormatFor(f, style);
        if (firstBlock) {
            c.setBlockFormat(bf);
            c.setBlockCharFormat(scf);
            firstBlock = false;
        } else {
            c.insertBlock(bf, scf);
        }
        if (listDepth > 0) {
            QTextList *l = lists.value(listDepth);
            if (l) {
                l->add(c.block());
            } else {
                QTextListFormat lf;
                lf.setStyle(QTextListFormat::ListDisc);
                lf.setIndent(listDepth);
                lists[listDepth] = c.createList(lf);
            }
        }
        fmtStack = {scf};
    };
    auto emitText = [&](const QString &t) {
        if (noteDepth > 0) {
            noteText += t;
            return;
        }
        if (annDepth > 0) {
            annText += t;
            return;
        }
        QTextCharFormat cf = fmtStack.isEmpty() ? QTextCharFormat() : fmtStack.last();
        if (!href.isEmpty())
            cf.merge(text::linkFormat(href));
        c.insertText(t, cf);
    };

    QXmlStreamReader x(zr.fileData(QStringLiteral("content.xml")));
    bool inBody = false;
    while (!x.atEnd()) {
        const auto t = x.readNext();
        const auto name = x.name();
        const auto ns = x.prefix();
        if (t == QXmlStreamReader::StartElement) {
            const QXmlStreamAttributes a = x.attributes();
            if (name == QLatin1String("text") && ns == QLatin1String("office")) {
                inBody = true;
                continue;
            }
            if (!inBody)
                continue;
            if (name == QLatin1String("note")) {
                ++noteDepth;
                noteText.clear();
            } else if (name == QLatin1String("note-citation")) {
                x.readElementText();
            } else if (name == QLatin1String("annotation")) {
                ++annDepth;
                annText.clear();
            } else if ((name == QLatin1String("p") || name == QLatin1String("h")) && noteDepth == 0 && annDepth == 0) {
                const QString sn = a.value(QLatin1String("text:style-name")).toString();
                const SProps sp = resolve(sn);
                QString style;
                if (name == QLatin1String("h"))
                    style = styles::forHeadingLevel(qMax(1, a.value(QLatin1String("text:outline-level")).toInt()));
                else if (sn.contains(QLatin1String("Quot"), Qt::CaseInsensitive))
                    style = QStringLiteral("Block Quote");
                else if (sn == QLatin1String("Title"))
                    style = QStringLiteral("Title");
                newBlock(style, sp.center);
                QTextCharFormat base = fmtStack.last();
                if (sp.bold)
                    base.setFontWeight(QFont::Bold);
                if (sp.italic)
                    base.setFontItalic(true);
                fmtStack.last() = base;
            } else if ((name == QLatin1String("p")) && (noteDepth > 0 || annDepth > 0)) {
                if (noteDepth > 0 && !noteText.isEmpty())
                    noteText += QLatin1Char(' ');
                if (annDepth > 0 && !annText.isEmpty())
                    annText += QLatin1Char('\n');
            } else if (name == QLatin1String("span")) {
                QTextCharFormat cf = fmtStack.isEmpty() ? QTextCharFormat() : fmtStack.last();
                const SProps sp = resolve(a.value(QLatin1String("text:style-name")).toString());
                if (sp.hasBold || sp.bold)
                    cf.setFontWeight(sp.bold ? QFont::Bold : QFont::Normal);
                if (sp.hasItalic || sp.italic)
                    cf.setFontItalic(sp.italic);
                if (sp.underline)
                    cf.setFontUnderline(true);
                if (sp.strike)
                    cf.setFontStrikeOut(true);
                fmtStack.append(cf);
            } else if (name == QLatin1String("s")) {
                const int n = qMax(1, a.value(QLatin1String("text:c")).toInt());
                emitText(QString(n, QLatin1Char(' ')));
            } else if (name == QLatin1String("tab")) {
                emitText(QStringLiteral("\t"));
            } else if (name == QLatin1String("line-break")) {
                emitText(QString(QChar::LineSeparator));
            } else if (name == QLatin1String("a")) {
                href = a.value(QLatin1String("xlink:href")).toString();
            } else if (name == QLatin1String("list")) {
                ++listDepth;
            }
        } else if (t == QXmlStreamReader::Characters && inBody) {
            if (!x.isWhitespace() || !x.text().isEmpty())
                emitText(x.text().toString());
        } else if (t == QXmlStreamReader::EndElement && inBody) {
            if (name == QLatin1String("span")) {
                if (fmtStack.size() > 1)
                    fmtStack.removeLast();
            } else if (name == QLatin1String("note")) {
                --noteDepth;
                if (noteDepth == 0 && !noteText.trimmed().isEmpty()) {
                    QTextCharFormat cf = fmtStack.isEmpty() ? QTextCharFormat() : fmtStack.last();
                    cf.merge(text::noteFormat(NoteKind::InlineFootnote, QUuid::createUuid().toString(QUuid::WithoutBraces)));
                    c.insertText(noteText.trimmed(), cf);
                }
            } else if (name == QLatin1String("annotation")) {
                --annDepth;
                if (annDepth == 0 && !annText.trimmed().isEmpty()) {
                    QTextCharFormat cf = fmtStack.isEmpty() ? QTextCharFormat() : fmtStack.last();
                    cf.merge(text::noteFormat(NoteKind::InlineAnnotation, QUuid::createUuid().toString(QUuid::WithoutBraces)));
                    c.insertText(annText.trimmed(), cf);
                }
            } else if (name == QLatin1String("a")) {
                href.clear();
            } else if (name == QLatin1String("list")) {
                lists.remove(listDepth);
                --listDepth;
            } else if (name == QLatin1String("text") && ns == QLatin1String("office")) {
                inBody = false;
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------ dispatch

static QString sofficeProgram()
{
    // QB_SOFFICE: the program to use, or "none" (as for the add-on; tests)
    const QString forced = qEnvironmentVariable("QB_SOFFICE");
    if (forced == QLatin1String("none"))
        return QString();
    if (!forced.isEmpty())
        return forced;
    QString p = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (p.isEmpty())
        p = QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
    return p;
}

// A .doc file through LibreOffice (when installed): converted into a .docx file in a
// temporary folder, which is then read as usual. LibreOffice runs with a profile of its
// own there, so that it works while the writer's LibreOffice is open.
static bool readWithLibreOffice(const QString &path, QTextDocument *doc, const Formatting &f, bool keepFormatting, QString *error)
{
    const QString soffice = sofficeProgram();
    if (soffice.isEmpty()) {
        if (error)
            *error = QObject::tr("%1: LibreOffice is not installed.").arg(QFileInfo(path).fileName());
        return false;
    }
    QTemporaryDir work;
    QProcess pr;
    pr.start(soffice, {QStringLiteral("-env:UserInstallation=") + QUrl::fromLocalFile(work.filePath(QStringLiteral("profile"))).toString(),
                       QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("docx"), QStringLiteral("--outdir"),
                       work.path(), path});
    if (!pr.waitForFinished(180000)) {
        pr.kill();
        pr.waitForFinished(5000);
    }
    const QString out = work.filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".docx"));
    if (!QFileInfo::exists(out)) {
        if (error)
            *error = QObject::tr("%1: LibreOffice could not convert the file.").arg(QFileInfo(path).fileName());
        return false;
    }
    return readTextFile(out, doc, f, keepFormatting, error);
}

bool readTextFile(const QString &path, QTextDocument *doc, const Formatting &f, bool keepFormatting, QString *error,
                  QString *titlePage, QStringList *warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open %1").arg(path);
        return false;
    }
    QString ext = QFileInfo(path).suffix().toLower();
    doc->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath() + QLatin1Char('/')));
    const bool builtIn = classifyBuiltIn(ext);
    if (!builtIn) {
        // a ".doc" may be RTF, Word XML or a web page saved by Word: read what it is
        const QString really = sniffFormat(file.peek(4096));
        if (!really.isEmpty()) {
            ext = really;
        } else {
            // .doc: the add-on "Word 97-2003 Import" or LibreOffice, as chosen in File > Options >
            // Sharing > Conversion (0.21); when the chosen way fails, the other one is tried
            const bool word = isLegacyWordSuffix(ext);
            const bool preferLo = word && importOptions().docConverter == QLatin1String("libreoffice");
            const bool addon = compile::PluginManager::instance().importerFor(ext) != nullptr;
            if (word || addon)
                file.close();
            QString firstError;
            if (preferLo && readWithLibreOffice(path, doc, f, keepFormatting, &firstError))
                return true;
            if (addon) // (the add-on itself turns to LibreOffice for files it cannot read)
                return readWithImporter(path, doc, f, keepFormatting, error, warnings);
            if (word && !preferLo && readWithLibreOffice(path, doc, f, keepFormatting, &firstError))
                return true;
            if (word) {
                if (error)
                    *error = firstError.isEmpty() ? QObject::tr("%1: no add-on that reads .%2 files is installed.").arg(QFileInfo(path).fileName(), ext)
                                                  : firstError;
                return false;
            }
        }
    }
    if (ext == QLatin1String("docx")) {
        file.close();
        const bool ok = readDocx(path, doc, f, error);
        if (ok)
            applyImportOptions(doc, f, false);
        return ok;
    }
    if (ext == QLatin1String("odt")) {
        file.close();
        const bool ok = readOdt(path, doc, f, error);
        if (ok)
            applyImportOptions(doc, f, true); // LibreOffice's comments are read as inline annotations
        return ok;
    }
    const QByteArray data = file.readAll();
    if (ext == QLatin1String("rtf") || rtf::looksLikeRtf(data)) {
        rtf::ReadOptions o;
        o.keepFormatting = keepFormatting;
        const auto res = rtf::read(data, doc, f, o);
        if (!keepFormatting)
            display::normalizeDocument(doc, f);
        if (res.ok)
            applyImportOptions(doc, f, false);
        return res.ok;
    }
    if (ext == QLatin1String("fdx")) {
        script::Screenplay sp;
        if (!script::fromFdx(data, &sp, error))
            return false;
        script::toDocument(sp, doc, f);
        if (titlePage)
            *titlePage = script::titlePageText(sp);
        return true;
    }
    const QString text = decodeText(data);
    if (ext == QLatin1String("fountain") || ext == QLatin1String("spmd")) {
        const script::Screenplay sp = script::fromFountain(text);
        script::toDocument(sp, doc, f);
        if (titlePage)
            *titlePage = script::titlePageText(sp);
        return true;
    }
    if (ext == QLatin1String("md") || ext == QLatin1String("markdown") || ext == QLatin1String("mdown")) {
        md::read(md::fromStandardMarkdown(text), doc, f);
        return true;
    }
    if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
        QTextDocument tmp;
        tmp.setHtml(text);
        // keep semantic formatting only
        QTextCursor c(doc);
        c.setBlockFormat(display::normalBlockFormat(f));
        bool first = true;
        for (QTextBlock b = tmp.begin(); b.isValid(); b = b.next()) {
            QString style;
            if (b.blockFormat().headingLevel() > 0)
                style = styles::forHeadingLevel(b.blockFormat().headingLevel());
            if (!first)
                c.insertBlock(display::blockFormatFor(f, style), display::charFormatFor(f, style));
            else
                c.setBlockFormat(display::blockFormatFor(f, style));
            first = false;
            for (auto it = b.begin(); !it.atEnd(); ++it)
                c.insertText(it.fragment().text(), display::semanticCharFormat(it.fragment().charFormat()));
        }
        return true;
    }
    insertPlain(doc, text, f);
    return true;
}

BinderItem *importMedia(Project *p, const QString &path, BinderItem *parent, int row)
{
    const QFileInfo fi(path);
    ItemType t = ItemType::File;
    switch (classify(path)) {
    case FileKind::Image: t = ItemType::Image; break;
    case FileKind::Pdf: t = ItemType::Pdf; break;
    case FileKind::Media: t = ItemType::Media; break;
    case FileKind::WebPage: t = ItemType::WebPage; break;
    default: t = ItemType::File; break;
    }
    BinderItem *it = p->model()->createItem(parent, row, t, fi.completeBaseName());
    it->fileExt = fi.suffix().toLower();
    it->originalFileName = fi.fileName();
    QDir().mkpath(p->contentDir(it->id));
    QFile::copy(path, p->mediaFilePath(it));
    p->notifyItemChanged(it, false);
    return it;
}

static bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    bool ok = true;
    for (const QFileInfo &fi : QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dest = to + QLatin1Char('/') + fi.fileName();
        if (fi.isDir())
            ok = copyTree(fi.absoluteFilePath(), dest) && ok;
        else
            ok = QFile::copy(fi.absoluteFilePath(), dest) && ok;
    }
    return ok;
}

BinderItem *addWebPage(Project *p, const importers::WebPage &page, BinderItem *parent, int row, QStringList *warnings)
{
    if (!parent || p->isInTrash(parent))
        parent = p->research();
    QString title = page.title.simplified();
    if (title.isEmpty())
        title = QUrl(page.url).host();
    if (title.isEmpty())
        title = QObject::tr("Web Page");
    title = title.left(200);
    BinderItem *it = nullptr;
    if (page.asWebPage) {
        if (p->isInDraft(parent)) {
            if (warnings)
                warnings->append(QObject::tr("“%1”: a web page document cannot be placed in the Draft; it was added to Research instead.")
                                     .arg(title));
            parent = p->research();
            row = -1;
        }
        if (!QFileInfo::exists(page.htmlFile)) {
            if (warnings)
                warnings->append(QObject::tr("“%1”: the page was not saved.").arg(title));
            return nullptr;
        }
        it = p->model()->createItem(parent, row, ItemType::WebPage, title);
        it->fileExt = QStringLiteral("html");
        it->originalFileName = QUrl(page.url).fileName();
        const QString dir = p->contentDir(it->id);
        QDir().mkpath(dir);
        QFile::copy(page.htmlFile, p->mediaFilePath(it));
        // the pictures: folders beside the page
        const QFileInfo hf(page.htmlFile);
        for (const QFileInfo &fi : hf.dir().entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            copyTree(fi.absoluteFilePath(), dir + QLatin1Char('/') + fi.fileName());
    } else {
        it = p->model()->createItem(parent, row, ItemType::Text, title);
        QTextDocument tmp;
        fromContent(page.content, &tmp, p->formatting, p->storage == StorageFormat::Rtf);
        p->docs()->setContent(it->id, &tmp);
    }
    if (!it)
        return nullptr;
    if (!page.url.isEmpty())
        it->bookmarks.append({page.title.simplified().isEmpty() ? page.url : page.title.simplified(), page.url});
    if (!page.excerpt.trimmed().isEmpty() && it->synopsis.isEmpty())
        it->synopsis = page.excerpt.simplified().left(800);
    QStringList lines;
    if (!page.url.isEmpty())
        lines << QObject::tr("Address: %1").arg(page.url);
    if (!page.siteName.trimmed().isEmpty())
        lines << QObject::tr("Site: %1").arg(page.siteName.simplified());
    if (!page.author.trimmed().isEmpty())
        lines << QObject::tr("Author: %1").arg(page.author.simplified());
    if (!page.published.trimmed().isEmpty())
        lines << QObject::tr("Published: %1").arg(page.published.simplified());
    lines << QObject::tr("Retrieved: %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    QTextDocument notes;
    insertPlain(&notes, lines.join(QLatin1Char('\n')), p->formatting);
    p->docs()->setNotesContent(it->id, &notes);
    p->notifyItemChanged(it, false);
    p->markIndexDirty();
    return it;
}

QList<BinderItem *> importFiles(Project *p, const QStringList &paths, BinderItem *parent, int row, QStringList *warnings)
{
    QList<BinderItem *> out;
    if (!parent)
        parent = p->research();
    for (const QString &path : paths) {
        const QFileInfo fi(path);
        if (fi.isDir()) {
            // import folders recursively
            BinderItem *folder = p->model()->createItem(parent, row, ItemType::Folder, fi.fileName());
            if (row >= 0)
                ++row;
            QStringList children;
            for (const QFileInfo &c : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                children << c.absoluteFilePath();
            out << folder;
            out << importFiles(p, children, folder, -1, warnings);
            continue;
        }
        const FileKind kind = classify(path);
        if (kind == FileKind::Text) {
            QTextDocument tmp;
            QString err, titlePage;
            if (!readTextFile(path, &tmp, p->formatting, p->storage == StorageFormat::Rtf, &err, &titlePage, warnings)) {
                if (warnings)
                    warnings->append(err);
                continue;
            }
            BinderItem *it = p->model()->createItem(parent, row, ItemType::Text, fi.completeBaseName());
            p->docs()->setContent(it->id, &tmp);
            if (isScriptFile(path)) {
                // a script: the document is in script mode, the title page goes into its notes
                it->scriptMode = true;
                if (!titlePage.isEmpty()) {
                    QTextDocument notes;
                    insertPlain(&notes, titlePage, p->formatting);
                    p->docs()->setNotesContent(it->id, &notes);
                }
                p->notifyItemChanged(it, false);
            }
            out << it;
        } else {
            if (isLegacyWordSuffix(fi.suffix()) && warnings)
                warnings->append(QObject::tr("%1: Word 97–2003 files become text with the add-on “Word 97–2003 Import” "
                                             "(Help > Add-on Manager); the file was added as it is.")
                                     .arg(fi.fileName()));
            if (p->isInDraft(parent)) {
                if (warnings)
                    warnings->append(QObject::tr("%1: only text can be placed in the Draft; it was added to Research instead.")
                                         .arg(fi.fileName()));
                out << importMedia(p, path, p->research(), -1);
                continue;
            }
            out << importMedia(p, path, parent, row);
        }
        if (row >= 0)
            ++row;
    }
    return out;
}

QList<BinderItem *> importAndSplit(Project *p, const QString &path, const QString &separator, bool firstLineIsTitle,
                                   BinderItem *parent, int row, QString *error, QStringList *warnings)
{
    QList<BinderItem *> out;
    QTextDocument whole;
    if (!readTextFile(path, &whole, p->formatting, p->storage == StorageFormat::Rtf, error, nullptr, warnings))
        return out;
    // split into block ranges
    QList<QPair<int, int>> ranges; // first block number, last block number
    int start = 0;
    int emptyRun = 0;
    const bool atScenes = separator == splitAtScenes();
    const bool splitOnEmpty = !atScenes && separator.trimmed().isEmpty();
    const int blocks = whole.blockCount();
    if (atScenes) {
        // scripts: a document per scene; act headings start a document as well
        firstLineIsTitle = false;
        for (int i = 1; i < blocks; ++i) {
            const QString style = whole.findBlockByNumber(i).blockFormat().stringProperty(props::Style);
            const script::Element *el = script::element(p->formatting, style);
            const script::Element *prev = script::element(
                p->formatting, whole.findBlockByNumber(i - 1).blockFormat().stringProperty(props::Style));
            const bool starts = el && (el->role == script::Role::SceneHeading || el->role == script::Role::Section);
            // a scene heading right after an act heading stays with it
            const bool afterAct = prev && prev->role == script::Role::Section && i - 1 == start;
            if (starts && !afterAct) {
                ranges.append({start, i - 1});
                start = i;
            }
        }
    }
    for (int i = 0; i < blocks && !atScenes; ++i) {
        const QString t = whole.findBlockByNumber(i).text().trimmed();
        bool isSep = false;
        if (splitOnEmpty) {
            if (t.isEmpty()) {
                ++emptyRun;
                if (emptyRun == 2)
                    isSep = true;
            } else {
                emptyRun = 0;
            }
        } else {
            isSep = (t == separator.trimmed());
        }
        if (isSep) {
            const int last = splitOnEmpty ? i - 2 : i - 1;
            if (last >= start)
                ranges.append({start, last});
            start = i + 1;
        }
    }
    if (start < blocks)
        ranges.append({start, blocks - 1});
    int n = 1;
    for (const auto &r : ranges) {
        QTextCursor c(&whole);
        const QTextBlock b1 = whole.findBlockByNumber(r.first);
        const QTextBlock b2 = whole.findBlockByNumber(r.second);
        c.setPosition(b1.position());
        c.setPosition(b2.position() + b2.length() - 1, QTextCursor::KeepAnchor);
        QString title = QFileInfo(path).completeBaseName() + QStringLiteral(" %1").arg(n++);
        QTextDocumentFragment frag(c);
        QTextDocument part;
        QTextCursor pc(&part);
        pc.insertFragment(frag);
        {
            // a fragment does not bring the paragraph format of its first paragraph along
            QTextCursor fc(part.begin());
            fc.setBlockFormat(b1.blockFormat());
            fc.setBlockCharFormat(b1.charFormat());
        }
        if (atScenes && part.blockCount() > 0) {
            // the heading names the document and stays in its text
            QString t = part.begin().text().trimmed();
            const script::Element *el = script::element(p->formatting, b1.blockFormat().stringProperty(props::Style));
            if (el && el->allCaps)
                t = t.toUpper();
            if (!t.isEmpty())
                title = t.left(120);
        }
        if (firstLineIsTitle && part.blockCount() > 0) {
            const QString t = part.begin().text().trimmed();
            if (!t.isEmpty() && t.size() < 120) {
                title = t;
                QTextCursor del(part.begin());
                del.select(QTextCursor::BlockUnderCursor);
                del.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
                del.removeSelectedText();
            }
        }
        BinderItem *it = p->model()->createItem(parent, row, ItemType::Text, title);
        if (row >= 0)
            ++row;
        p->docs()->setContent(it->id, &part);
        if (isScriptFile(path)) {
            it->scriptMode = true;
            p->notifyItemChanged(it, false);
        }
        out << it;
    }
    return out;
}

} // namespace importer
} // namespace qb
