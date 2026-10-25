#include "MarkdownExporter.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

using namespace qb::compile;

namespace {

QString escape(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '\\': case '*': case '_': case '`': case '[': case ']': case '<': case '>': case '#': case '~':
            out += QLatin1Char('\\');
            out += c;
            break;
        default: out += c;
        }
    }
    return out;
}

struct Writer {
    const Manuscript &m;
    QString imageDir;      // absolute folder for images
    QString imageDirName;  // as referenced from the Markdown file
    int imageCount = 0;
    QStringList notes;
    QString error;

    QString runs(const QList<Run> &list)
    {
        QString s;
        for (const Run &r : list) {
            switch (r.kind) {
            case Run::Tab: s += QLatin1Char('\t'); break;
            case Run::LineBreak: s += QLatin1String("\\\n"); break;
            case Run::FootnoteRef: {
                const int n = r.footnote + 1;
                s += QStringLiteral("[^%1]").arg(n);
                if (r.footnote >= 0 && r.footnote < m.footnotes.size()) {
                    QStringList paras;
                    for (const Paragraph &p : m.footnotes.at(r.footnote).paragraphs)
                        paras << runs(p.runs);
                    notes << QStringLiteral("[^%1]: %2").arg(n).arg(paras.join(QLatin1String("\n\n    ")));
                }
                break;
            }
            case Run::Image: {
                if (imageDir.isEmpty() || r.image.isNull())
                    break;
                QDir().mkpath(imageDir);
                const QString name = QStringLiteral("image%1.png").arg(++imageCount);
                if (!r.image.save(imageDir + QLatin1Char('/') + name, "PNG"))
                    error = QStringLiteral("Cannot save %1").arg(name);
                s += QStringLiteral("![](%1/%2)").arg(imageDirName, name);
                break;
            }
            case Run::Text: {
                QString t = escape(r.text);
                if (t.trimmed().isEmpty()) {
                    s += t;
                    break;
                }
                // keep spaces outside the markers: "** bold**" is not bold
                int lead = 0, trail = 0;
                while (lead < t.size() && t.at(lead).isSpace())
                    ++lead;
                while (trail < t.size() - lead && t.at(t.size() - 1 - trail).isSpace())
                    ++trail;
                QString core = t.mid(lead, t.size() - lead - trail);
                if (r.strike)
                    core = QLatin1String("~~") + core + QLatin1String("~~");
                if (r.italic)
                    core = QLatin1Char('*') + core + QLatin1Char('*');
                if (r.bold)
                    core = QLatin1String("**") + core + QLatin1String("**");
                if (r.superscript)
                    core = QLatin1String("<sup>") + core + QLatin1String("</sup>");
                if (r.subscript)
                    core = QLatin1String("<sub>") + core + QLatin1String("</sub>");
                if (!r.link.isEmpty())
                    core = QLatin1Char('[') + core + QLatin1String("](") + r.link + QLatin1Char(')');
                s += t.left(lead) + core + t.right(trail);
                break;
            }
            }
        }
        return s;
    }
};

} // namespace

bool MarkdownExporter::exportManuscript(const Manuscript &m, const QString &path, QString *error)
{
    const QFileInfo fi(path);
    Writer w{m, fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1String("_images"),
             fi.completeBaseName() + QLatin1String("_images")};
    QString out;
    if (!m.title.isEmpty() || !m.author.isEmpty())
        out += QStringLiteral("---\ntitle: \"%1\"\nauthor: \"%2\"\nlang: %3\n---\n\n")
                   .arg(QString(m.title).replace(QLatin1Char('"'), QLatin1String("\\\"")),
                        QString(m.author).replace(QLatin1Char('"'), QLatin1String("\\\"")), m.language);
    int prevList = -1;
    for (const Paragraph &p : m.paragraphs) {
        const QString body = w.runs(p.runs);
        const bool isList = p.style == Paragraph::ListBullet || p.style == Paragraph::ListNumber;
        QString line;
        switch (p.style) {
        case Paragraph::Title:
        case Paragraph::Heading1: line = QLatin1String("# ") + body; break;
        case Paragraph::Heading2: line = QLatin1String("## ") + body; break;
        case Paragraph::Heading3: line = QLatin1String("### ") + body; break;
        case Paragraph::Subtitle: line = QLatin1Char('*') + body.trimmed() + QLatin1Char('*'); break;
        case Paragraph::BlockQuote: line = QLatin1String("> ") + body; break;
        case Paragraph::Separator: line = body.trimmed().isEmpty() ? QStringLiteral("* * *") : body; break;
        case Paragraph::ListBullet: line = QString(p.listLevel * 2, QLatin1Char(' ')) + QLatin1String("- ") + body; break;
        case Paragraph::ListNumber: line = QString(p.listLevel * 3, QLatin1Char(' ')) + QLatin1String("1. ") + body; break;
        default: line = body; break;
        }
        if (line.trimmed().isEmpty())
            continue;
        if (prevList != -1 && (!isList || p.listId != prevList))
            out += QLatin1Char('\n');
        out += line + (isList ? QLatin1String("\n") : QLatin1String("\n\n"));
        prevList = isList ? p.listId : -1;
    }
    if (!w.notes.isEmpty())
        out += QLatin1Char('\n') + w.notes.join(QLatin1Char('\n')) + QLatin1Char('\n');

    QSaveFile f(path);
    const QByteArray data = out.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (!w.error.isEmpty() && error)
        *error = w.error;
    return w.error.isEmpty();
}
