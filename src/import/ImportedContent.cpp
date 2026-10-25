#include "import/ImportedContent.h"

#include "compile/PluginManager.h"
#include "import/FileImporter.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>
#include <QUuid>

namespace qb {
namespace importer {

static QString styleFor(const importers::Paragraph &p, const Formatting &f)
{
    if (!p.style.isEmpty()) {
        if (const Style *s = styles::find(f, p.style))
            return s->name;
        // names of other programs for the same thing
        const QString l = p.style.toLower();
        if (l.startsWith(QLatin1String("heading ")))
            return styles::forHeadingLevel(qBound(1, l.mid(8).toInt(), 6));
        if (l == QLatin1String("title"))
            return QStringLiteral("Title");
        if (l == QLatin1String("subtitle"))
            return QStringLiteral("Subtitle");
        if (l.contains(QLatin1String("quote")) || l.contains(QLatin1String("quotation")) || l == QLatin1String("block text"))
            return QStringLiteral("Block Quote");
        if (l == QLatin1String("caption"))
            return QStringLiteral("Caption");
    }
    if (p.headingLevel > 0)
        return styles::forHeadingLevel(qBound(1, p.headingLevel, 6));
    return QString();
}

static QString oneLine(QString s)
{
    s.replace(QLatin1Char('\r'), QString());
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return s.trimmed();
}

void fromContent(const importers::Content &content, QTextDocument *doc, const Formatting &f, bool keepFormatting)
{
    doc->clear();
    QTextCursor c(doc);
    // one note id per comment (a comment may span several runs and paragraphs)
    QHash<int, QString> commentIds;
    QHash<int, QImage> images;
    QMap<QPair<int, int>, QTextList *> lists;
    bool prevList = false;
    const Style *codeSpan = styles::findChar(f, QStringLiteral("Code Span"));
    bool first = true;
    for (const importers::Paragraph &p : content.paragraphs) {
        const QString style = styleFor(p, f);
        QTextBlockFormat bf = display::blockFormatFor(f, style);
        direct::ParaSpec spec;
        if (p.align == 1 || p.align == 2 || p.align == 3) {
            spec.bits |= direct::Align;
            spec.align = p.align == 1 ? Qt::AlignHCenter : p.align == 2 ? Qt::AlignRight : Qt::AlignJustify;
        }
        if (keepFormatting && p.listLevel < 0) {
            if (p.leftIndent > 0) {
                spec.bits |= direct::Left;
                spec.left = p.leftIndent * direct::kPxPerPt;
            }
            if (p.rightIndent > 0) {
                spec.bits |= direct::Right;
                spec.right = p.rightIndent * direct::kPxPerPt;
            }
            if (p.firstIndent != 0) {
                spec.bits |= direct::First;
                spec.first = p.firstIndent * direct::kPxPerPt;
            }
        }
        if (spec.bits) {
            if (spec.bits & direct::Align)
                bf.setTextIndent(0);
            bf = direct::withParaSpec(bf, spec, f, p.listLevel >= 0);
        }
        if (p.pageBreakBefore && !first) {
            bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            bf.setProperty(props::PageBreak, true);
        }
        const QTextCharFormat scf = display::charFormatFor(f, style);
        if (first) {
            c.setBlockFormat(bf);
            c.setBlockCharFormat(scf);
            first = false;
        } else {
            c.insertBlock(bf, scf);
        }
        if (p.listLevel >= 0) {
            if (!prevList)
                lists.clear();
            // deeper lists end when a shallower item comes
            for (auto it = lists.begin(); it != lists.end();) {
                if (it.key().second > p.listLevel)
                    it = lists.erase(it);
                else
                    ++it;
            }
            const QPair<int, int> key(p.listId, p.listLevel);
            QTextList *l = lists.value(key);
            const QTextListFormat::Style ls = p.numbered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;
            if (l && l->format().style() == ls) {
                l->add(c.block());
            } else {
                QTextListFormat lf;
                lf.setStyle(ls);
                lf.setIndent(p.listLevel + 1);
                lists[key] = c.createList(lf);
            }
            prevList = true;
        } else {
            prevList = false;
        }
        for (const importers::Run &r : p.runs) {
            if (r.image >= 0 && r.image < content.images.size()) {
                const importers::Image &im = content.images.at(r.image);
                QImage img = images.value(r.image);
                if (img.isNull()) {
                    img.loadFromData(im.data);
                    images.insert(r.image, img);
                }
                if (img.isNull())
                    continue;
                const QString name = QStringLiteral("importimage://%1.png").arg(QUuid::createUuid().toString(QUuid::Id128));
                doc->addResource(QTextDocument::ImageResource, QUrl(name), img);
                QTextImageFormat imf;
                imf.setName(name);
                double w = im.width > 0 ? im.width * direct::kPxPerPt : img.width();
                double h = im.height > 0 ? im.height * direct::kPxPerPt : img.height() * w / qMax(1, img.width());
                if (w > 500) {
                    h = h * 500.0 / w;
                    w = 500;
                }
                if (w != img.width() || h != img.height()) {
                    imf.setWidth(w);
                    imf.setHeight(h);
                }
                if (!im.alt.isEmpty())
                    imf.setToolTip(im.alt);
                c.insertImage(imf);
                continue;
            }
            if (r.note >= 0 && r.note < content.notes.size()) {
                const QString t = oneLine(content.notes.at(r.note).text);
                if (!t.isEmpty()) {
                    QTextCharFormat nf = scf;
                    nf.merge(text::noteFormat(NoteKind::InlineFootnote, QUuid::createUuid().toString(QUuid::WithoutBraces)));
                    c.insertText(t, nf);
                }
                continue;
            }
            if (r.text.isEmpty())
                continue;
            QTextCharFormat in;
            if (r.bold)
                in.setFontWeight(QFont::Bold);
            if (r.italic)
                in.setFontItalic(true);
            if (r.underline)
                in.setFontUnderline(true);
            if (r.strike)
                in.setFontStrikeOut(true);
            if (r.vertical > 0)
                in.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
            else if (r.vertical < 0)
                in.setVerticalAlignment(QTextCharFormat::AlignSubScript);
            QTextCharFormat cf = scf;
            cf.merge(display::semanticCharFormat(in));
            if (!r.link.isEmpty())
                cf.merge(text::linkFormat(r.link));
            if (r.highlight.isValid() && r.highlight != QColor(Qt::white)) {
                cf.setProperty(props::Highlight, true);
                cf.setBackground(r.highlight);
            }
            if (r.code && codeSpan) {
                styles::applyChars(cf, *codeSpan);
                cf.setProperty(props::CharStyle, codeSpan->name);
            }
            if (keepFormatting || r.smallCaps || r.allCaps) {
                direct::CharSpec cs;
                if (keepFormatting) {
                    cs.family = r.font;
                    cs.size = r.size;
                    if (r.color.isValid() && r.color != QColor(Qt::black))
                        cs.color = r.color;
                }
                cs.caps = r.smallCaps ? 1 : r.allCaps ? 2 : 0;
                if (!cs.isEmpty())
                    direct::mergeCharSpec(cf, cs);
            }
            if (r.comment >= 0 && r.comment < content.notes.size()) {
                const importers::Note &n = content.notes.at(r.comment);
                QString id = commentIds.value(r.comment);
                if (id.isEmpty()) {
                    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    commentIds.insert(r.comment, id);
                }
                QString t = n.text.trimmed();
                if (!n.author.trimmed().isEmpty())
                    t = n.author.trimmed() + QStringLiteral(": ") + t;
                cf.merge(text::noteFormat(NoteKind::Comment, id, t));
            }
            QString t = r.text;
            t.replace(QLatin1Char('\r'), QString());
            t.replace(QLatin1Char('\n'), QChar::LineSeparator);
            c.insertText(t, cf);
        }
    }
}

QString sniffFormat(const QByteArray &head)
{
    QByteArray h = head;
    if (h.startsWith("\xEF\xBB\xBF"))
        h = h.mid(3);
    if (h.startsWith("{\\rtf"))
        return QStringLiteral("rtf");
    if (h.startsWith("PK\x03\x04")) {
        if (head.contains("mimetypeapplication/vnd.oasis.opendocument"))
            return QStringLiteral("odt");
        return QStringLiteral("docx");
    }
    const QByteArray l = h.left(600).trimmed().toLower();
    if (l.startsWith("<!doctype html") || l.startsWith("<html") || l.contains("<html") || l.startsWith("mime-version:"))
        return l.startsWith("mime-version:") ? QString() : QStringLiteral("html");
    return QString();
}

bool readWithImporter(const QString &path, QTextDocument *doc, const Formatting &f, bool keepFormatting,
                      QString *error, QStringList *warnings)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    importers::ImporterInterface *imp = compile::PluginManager::instance().importerFor(ext);
    if (!imp) {
        if (error)
            *error = QObject::tr("%1: no add-on that reads .%2 files is installed.").arg(QFileInfo(path).fileName(), ext);
        return false;
    }
    QTemporaryDir work;
    if (!work.isValid()) {
        if (error)
            *error = QObject::tr("Cannot create a temporary folder.");
        return false;
    }
    importers::Content content;
    QString err;
    if (!imp->readFile(path, work.path(), &content, &err)) {
        if (error)
            *error = QObject::tr("%1: %2").arg(QFileInfo(path).fileName(),
                                               err.isEmpty() ? QObject::tr("the file could not be read.") : err);
        return false;
    }
    if (warnings)
        for (const QString &w : content.warnings)
            warnings->append(QObject::tr("%1: %2").arg(QFileInfo(path).fileName(), w));
    if (!content.file.isEmpty()) {
        // the importer wrote a file the program reads itself (never one of its own kind again)
        if (QFileInfo(content.file).suffix().toLower() == ext || !QFileInfo::exists(content.file)) {
            if (error)
                *error = QObject::tr("%1: the importer gave no readable result.").arg(QFileInfo(path).fileName());
            return false;
        }
        return readTextFile(content.file, doc, f, keepFormatting, error);
    }
    fromContent(content, doc, f, keepFormatting);
    return true;
}

} // namespace importer
} // namespace qb
