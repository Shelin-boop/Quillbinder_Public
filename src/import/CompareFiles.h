#pragma once
// The file side of Compare and Transfer (0.18): a Word, LibreOffice or other
// file as a list of paragraphs, and, for .docx and .odt, the changes written
// into a copy of the file. In a Word file every change is a tracked change
// (insertion, deletion, formatting change by "Quillbinder"), so that Word
// or LibreOffice shows it and it can be accepted or rejected there; in a
// LibreOffice file the changes are made directly. Everything else in the
// file (layout, styles, headers, pictures, tables, notes) stays as it was.

#include "import/Compare.h"
#include "import/MiniXml.h"

#include <QHash>
#include <QStringList>

namespace qb {
namespace compare {

class FileText
{
public:
    virtual ~FileText() = default;
    QList<Para> paras;              // every paragraph of the main text, in order (source = index)
    QString kind;                   // suffix
    virtual bool canWrite() const { return false; }
    // Edits; positions are characters of paras[i].text as read. Within one
    // paragraph the edits must be made from the end to the start.
    virtual bool replace(int para, int start, int end, const QString &text, const QVector<quint8> &fmt);
    virtual bool setFormat(int para, int start, int end, const QVector<quint8> &fmt);
    // new paragraphs after paragraph `after` (-1: before the first one), with
    // the look of paragraph `styleLike`
    virtual bool insertParagraphs(int after, int styleLike, const QList<Para> &ps);
    virtual bool deleteParagraph(int para);
    virtual bool save(const QString &path, QString *error);
    int changes = 0;                // edits made
};

// .docx: word/document.xml, changes tracked
class DocxText : public FileText
{
public:
    bool load(const QString &path, QString *error);
    bool canWrite() const override { return true; }
    bool replace(int para, int start, int end, const QString &text, const QVector<quint8> &fmt) override;
    bool setFormat(int para, int start, int end, const QVector<quint8> &fmt) override;
    bool insertParagraphs(int after, int styleLike, const QList<Para> &ps) override;
    bool deleteParagraph(int para) override;
    bool save(const QString &path, QString *error) override;
    QString author = QStringLiteral("Quillbinder");
    QString date;                   // ISO time of the changes (now)

    struct Loc {                    // where a character of a paragraph is
        xml::Node *run = nullptr;   // its w:r
        xml::Node *node = nullptr;  // w:t (text) or w:tab, w:br ...
        int offset = -1;            // in the w:t's text
    };
    QVector<Loc> scan(xml::Node *p, QString *text, QVector<quint8> *fmt) const;

private:
    QString m_path;
    xml::Document m_doc;
    QList<xml::Node *> m_pNodes;    // the w:p of each paragraph
    QHash<QString, int> m_styleHeading; // style id -> heading level
    int m_nextId = 1;
    QString nextId() { return QString::number(m_nextId++); }
    std::unique_ptr<xml::Node> changeMark(const char *name);
    xml::Node *splitAt(xml::Node *p, int pos);  // the run that starts at character pos (runs split there)
    std::unique_ptr<xml::Node> newRun(const xml::Node *likeRun, const QString &text, quint8 fmt) const;
};

// .odt: content.xml, changes made directly
class OdtText : public FileText
{
public:
    bool load(const QString &path, QString *error);
    bool canWrite() const override { return true; }
    bool replace(int para, int start, int end, const QString &text, const QVector<quint8> &fmt) override;
    bool setFormat(int para, int start, int end, const QVector<quint8> &fmt) override;
    bool insertParagraphs(int after, int styleLike, const QList<Para> &ps) override;
    bool deleteParagraph(int para) override;
    bool save(const QString &path, QString *error) override;

    struct Loc {
        xml::Node *node = nullptr;  // a text node, text:s, text:tab, text:line-break
        int offset = 0;             // in the text node, or the space of a text:s
    };
    QVector<Loc> scan(xml::Node *p, QString *text, QVector<quint8> *fmt) const;

private:
    QString m_path;
    xml::Document m_doc;
    QList<xml::Node *> m_pNodes;
    QHash<QString, quint8> m_styleFmt;     // automatic text / paragraph style -> its bold, italic, underline
    QHash<QString, quint8> m_styleMask;    // which of the three the style sets
    QHash<QString, QString> m_styleParent; // automatic style -> parent style
    QHash<quint8, QString> m_madeStyles;   // our automatic styles for bold / italic / underline
    quint8 fmtAt(xml::Node *n) const;      // the bold, italic, underline of a node (its spans and paragraph)
    QString styleFor(quint8 fmt);
    void splitText(xml::Node *p, int pos);
    void appendOdfText(xml::Node *parent, int index, const QString &text, quint8 fmt, quint8 context);
};

// everything else: read with the program's importers (no writing)
class ReadOnlyText : public FileText
{
public:
    bool load(const QString &path, const Formatting &f, QString *error);
};

} // namespace compare
} // namespace qb
