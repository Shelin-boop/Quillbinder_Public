#pragma once
// A forgiving HTML parser for web pages: elements, attributes, text; void
// elements, raw text (script, style), entities, and the implied end tags of
// HTML (p, li, td …). Enough for reading articles, not a browser's parser
// (no adoption of misnested formatting, no foster parenting of tables).

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <deque>
#include <functional>
#include <memory>

namespace qbweb {

struct Node {
    enum Type { Element, Text };
    Type type = Element;
    QString tag;                               // lower case; empty for the root
    QVector<QPair<QString, QString>> attrs;    // names in lower case
    QString text;                              // text nodes
    Node *parent = nullptr;
    QVector<Node *> children;
    // for the article finder
    double score = 0;
    bool scored = false;

    bool isElement(const char *name) const { return type == Element && tag == QLatin1String(name); }
    QString attr(const QString &name) const;
    bool hasAttr(const QString &name) const;
    void setAttr(const QString &name, const QString &value);
    QString textContent() const;               // all text below, as in the DOM
    int index() const { return parent ? int(parent->children.indexOf(const_cast<Node *>(this))) : -1; }
    Node *firstElement(const QString &tag) const;  // depth first
    QVector<Node *> elements(const QString &tag) const;
    void forEach(const std::function<void(Node *)> &f);
};

class Document
{
public:
    Document();
    void parse(const QString &html);
    Node *root() const { return m_root; }
    Node *body() const;                        // <body> or the root
    Node *create(Node::Type type, const QString &tag = QString());
    // Takes a node out of the tree (it stays allocated until the document goes).
    static void detach(Node *n);
    static void insert(Node *parent, int at, Node *n);
    static void replaceWithChildren(Node *n);

private:
    std::deque<std::unique_ptr<Node>> m_nodes;
    Node *m_root = nullptr;
};

QString decodeEntities(const QString &s);
// Decoding of the page's bytes: BOM, HTTP charset, <meta charset>, else UTF-8 / Windows-1252.
QString decodeHtml(const QByteArray &data, const QString &httpCharset);
bool isBlockTag(const QString &tag);

} // namespace qbweb
