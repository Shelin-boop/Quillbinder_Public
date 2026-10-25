#pragma once
// A small XML tree that keeps a file as it was (0.18): every text node,
// also one of spaces only (Qt 6.4's QDom drops those, which would glue the
// words of a Word file together), attributes in their order, namespace
// declarations, comments and processing instructions. Used to change Word
// (.docx) and LibreOffice (.odt) files in place.

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace qb {
namespace xml {

struct Node {
    enum Type { Element, Text, Comment, ProcessingInstruction, CData };
    Type type = Element;
    QString name;                              // element: qualified name ("w:p"); PI: target
    QList<QPair<QString, QString>> attrs;      // qualified name, value (also xmlns declarations)
    QString text;                              // text, comment, CDATA; PI: data
    std::vector<std::unique_ptr<Node>> children;
    Node *parent = nullptr;

    bool isElement(const char *n) const { return type == Element && name == QLatin1String(n); }
    QString attr(const QString &n) const;
    bool hasAttr(const QString &n) const;
    void setAttr(const QString &n, const QString &v);
    void removeAttr(const QString &n);
    Node *child(const char *n) const;          // first child element of that name
    QList<Node *> elements(const char *n = nullptr) const; // child elements (of that name)
    int indexOf(const Node *c) const;
    // takes ownership; index -1: at the end
    Node *insert(int index, std::unique_ptr<Node> n);
    Node *append(std::unique_ptr<Node> n) { return insert(-1, std::move(n)); }
    std::unique_ptr<Node> take(Node *c);       // removes a child, hands it back
    std::unique_ptr<Node> clone() const;       // deep copy (no parent)
    QString textContent() const;               // all text below
};

std::unique_ptr<Node> element(const QString &name);
std::unique_ptr<Node> textNode(const QString &text);

class Document
{
public:
    bool parse(const QByteArray &data, QString *error = nullptr);
    QByteArray serialize() const;
    Node *root() const { return m_top.get(); }        // holds the top-level nodes
    Node *documentElement() const;

private:
    std::unique_ptr<Node> m_top = std::make_unique<Node>();
    QString m_version = QStringLiteral("1.0");
    bool m_standalone = false;
    bool m_hasDecl = false;
    bool m_hasStandalone = false;
};

// all elements below n (document order) for which pred is true
void walk(Node *n, const std::function<bool(Node *)> &enter);

} // namespace xml
} // namespace qb
