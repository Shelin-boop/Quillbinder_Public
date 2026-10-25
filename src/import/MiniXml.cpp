#include "MiniXml.h"

#include <QXmlStreamReader>

namespace qb {
namespace xml {

QString Node::attr(const QString &n) const
{
    for (const auto &a : attrs)
        if (a.first == n)
            return a.second;
    return QString();
}

bool Node::hasAttr(const QString &n) const
{
    for (const auto &a : attrs)
        if (a.first == n)
            return true;
    return false;
}

void Node::setAttr(const QString &n, const QString &v)
{
    for (auto &a : attrs)
        if (a.first == n) {
            a.second = v;
            return;
        }
    attrs.append({n, v});
}

void Node::removeAttr(const QString &n)
{
    for (int i = 0; i < attrs.size(); ++i)
        if (attrs.at(i).first == n) {
            attrs.removeAt(i);
            return;
        }
}

Node *Node::child(const char *n) const
{
    for (const auto &c : children)
        if (c->type == Element && c->name == QLatin1String(n))
            return c.get();
    return nullptr;
}

QList<Node *> Node::elements(const char *n) const
{
    QList<Node *> out;
    for (const auto &c : children)
        if (c->type == Element && (!n || c->name == QLatin1String(n)))
            out << c.get();
    return out;
}

int Node::indexOf(const Node *c) const
{
    for (size_t i = 0; i < children.size(); ++i)
        if (children[i].get() == c)
            return int(i);
    return -1;
}

Node *Node::insert(int index, std::unique_ptr<Node> n)
{
    n->parent = this;
    Node *raw = n.get();
    if (index < 0 || index >= int(children.size()))
        children.push_back(std::move(n));
    else
        children.insert(children.begin() + index, std::move(n));
    return raw;
}

std::unique_ptr<Node> Node::take(Node *c)
{
    const int i = indexOf(c);
    if (i < 0)
        return nullptr;
    std::unique_ptr<Node> out = std::move(children[size_t(i)]);
    children.erase(children.begin() + i);
    out->parent = nullptr;
    return out;
}

std::unique_ptr<Node> Node::clone() const
{
    auto n = std::make_unique<Node>();
    n->type = type;
    n->name = name;
    n->attrs = attrs;
    n->text = text;
    for (const auto &c : children)
        n->append(c->clone());
    return n;
}

QString Node::textContent() const
{
    if (type == Text || type == CData)
        return text;
    QString s;
    for (const auto &c : children)
        s += c->textContent();
    return s;
}

std::unique_ptr<Node> element(const QString &name)
{
    auto n = std::make_unique<Node>();
    n->type = Node::Element;
    n->name = name;
    return n;
}

std::unique_ptr<Node> textNode(const QString &text)
{
    auto n = std::make_unique<Node>();
    n->type = Node::Text;
    n->text = text;
    return n;
}

bool Document::parse(const QByteArray &data, QString *error)
{
    m_top = std::make_unique<Node>();
    QXmlStreamReader r(data);
    r.setNamespaceProcessing(false);
    Node *cur = m_top.get();
    while (!r.atEnd()) {
        r.readNext();
        switch (r.tokenType()) {
        case QXmlStreamReader::StartDocument:
            m_hasDecl = true;
            m_version = r.documentVersion().toString();
            if (m_version.isEmpty())
                m_version = QStringLiteral("1.0");
            m_standalone = r.isStandaloneDocument();
            m_hasStandalone = data.left(200).contains("standalone");
            break;
        case QXmlStreamReader::StartElement: {
            auto n = element(r.qualifiedName().toString());
            for (const QXmlStreamNamespaceDeclaration &d : r.namespaceDeclarations())
                n->attrs.append({d.prefix().isEmpty() ? QStringLiteral("xmlns") : QStringLiteral("xmlns:") + d.prefix().toString(),
                                 d.namespaceUri().toString()});
            for (const QXmlStreamAttribute &a : r.attributes()) {
                const QString q = a.qualifiedName().toString();
                if (!n->hasAttr(q))
                    n->attrs.append({q, a.value().toString()});
            }
            cur = cur->append(std::move(n));
            break;
        }
        case QXmlStreamReader::EndElement:
            if (cur->parent)
                cur = cur->parent;
            break;
        case QXmlStreamReader::Characters: {
            auto n = textNode(r.text().toString());
            if (r.isCDATA())
                n->type = Node::CData;
            // a text right after another text (entities split them): one node
            if (n->type == Node::Text && !cur->children.empty() && cur->children.back()->type == Node::Text)
                cur->children.back()->text += n->text;
            else
                cur->append(std::move(n));
            break;
        }
        case QXmlStreamReader::Comment: {
            auto n = std::make_unique<Node>();
            n->type = Node::Comment;
            n->text = r.text().toString();
            cur->append(std::move(n));
            break;
        }
        case QXmlStreamReader::ProcessingInstruction: {
            auto n = std::make_unique<Node>();
            n->type = Node::ProcessingInstruction;
            n->name = r.processingInstructionTarget().toString();
            n->text = r.processingInstructionData().toString();
            cur->append(std::move(n));
            break;
        }
        default:
            break;
        }
    }
    if (r.hasError()) {
        if (error)
            *error = QStringLiteral("XML: %1 (line %2)").arg(r.errorString()).arg(r.lineNumber());
        return false;
    }
    return documentElement() != nullptr;
}

Node *Document::documentElement() const
{
    for (const auto &c : m_top->children)
        if (c->type == Node::Element)
            return c.get();
    return nullptr;
}

static void escape(QString &out, const QString &s, bool attribute)
{
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"':
            if (attribute)
                out += QLatin1String("&quot;");
            else
                out += c;
            break;
        case '\n':
            if (attribute)
                out += QLatin1String("&#10;");
            else
                out += c;
            break;
        case '\r': out += QLatin1String("&#13;"); break;
        case '\t':
            if (attribute)
                out += QLatin1String("&#9;");
            else
                out += c;
            break;
        default: out += c;
        }
    }
}

static void write(QString &out, const Node *n)
{
    switch (n->type) {
    case Node::Text: escape(out, n->text, false); break;
    case Node::CData: out += QLatin1String("<![CDATA[") + n->text + QLatin1String("]]>"); break;
    case Node::Comment: out += QLatin1String("<!--") + n->text + QLatin1String("-->"); break;
    case Node::ProcessingInstruction:
        out += QLatin1String("<?") + n->name + (n->text.isEmpty() ? QString() : QLatin1String(" ") + n->text) + QLatin1String("?>");
        break;
    case Node::Element:
        out += QLatin1Char('<') + n->name;
        for (const auto &a : n->attrs) {
            out += QLatin1Char(' ') + a.first + QLatin1String("=\"");
            escape(out, a.second, true);
            out += QLatin1Char('"');
        }
        if (n->children.empty()) {
            out += QLatin1String("/>");
        } else {
            out += QLatin1Char('>');
            for (const auto &c : n->children)
                write(out, c.get());
            out += QLatin1String("</") + n->name + QLatin1Char('>');
        }
        break;
    }
}

QByteArray Document::serialize() const
{
    QString out;
    out.reserve(1 << 20);
    if (m_hasDecl) {
        out += QLatin1String("<?xml version=\"") + m_version + QLatin1String("\" encoding=\"UTF-8\"");
        if (m_hasStandalone)
            out += m_standalone ? QLatin1String(" standalone=\"yes\"") : QLatin1String(" standalone=\"no\"");
        out += QLatin1String("?>\n");
    }
    for (const auto &c : m_top->children)
        write(out, c.get());
    return out.toUtf8();
}

void walk(Node *n, const std::function<bool(Node *)> &enter)
{
    for (size_t i = 0; i < n->children.size(); ++i) {
        Node *c = n->children[i].get();
        if (c->type != Node::Element)
            continue;
        if (enter(c))
            walk(c, enter);
    }
}

} // namespace xml
} // namespace qb
