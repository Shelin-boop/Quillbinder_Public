#include "Html.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

namespace qbweb {

// ------------------------------------------------------------------ nodes

QString Node::attr(const QString &name) const
{
    for (const auto &a : attrs)
        if (a.first == name)
            return a.second;
    return QString();
}

bool Node::hasAttr(const QString &name) const
{
    for (const auto &a : attrs)
        if (a.first == name)
            return true;
    return false;
}

void Node::setAttr(const QString &name, const QString &value)
{
    for (auto &a : attrs)
        if (a.first == name) {
            a.second = value;
            return;
        }
    attrs.append({name, value});
}

QString Node::textContent() const
{
    if (type == Text)
        return text;
    QString out;
    for (const Node *c : children)
        out += c->textContent();
    return out;
}

Node *Node::firstElement(const QString &t) const
{
    for (Node *c : children) {
        if (c->type == Element && c->tag == t)
            return c;
        if (Node *f = c->firstElement(t))
            return f;
    }
    return nullptr;
}

QVector<Node *> Node::elements(const QString &t) const
{
    QVector<Node *> out;
    for (Node *c : children) {
        if (c->type != Element)
            continue;
        if (c->tag == t)
            out << c;
        out << c->elements(t);
    }
    return out;
}

void Node::forEach(const std::function<void(Node *)> &f)
{
    f(this);
    const QVector<Node *> kids = children; // f may change the tree below
    for (Node *c : kids)
        if (c->parent == this)
            c->forEach(f);
}

// ------------------------------------------------------------------ entities

static const QHash<QString, uint> &entityTable()
{
    static QHash<QString, uint> t;
    if (!t.isEmpty())
        return t;
    static const char *list =
        "quot 34 amp 38 lt 60 gt 62 apos 39 nbsp 160 iexcl 161 cent 162 pound 163 curren 164 yen 165 brvbar 166 sect 167 "
        "uml 168 copy 169 ordf 170 laquo 171 not 172 shy 173 reg 174 macr 175 deg 176 plusmn 177 sup2 178 sup3 179 acute 180 "
        "micro 181 para 182 middot 183 cedil 184 sup1 185 ordm 186 raquo 187 frac14 188 frac12 189 frac34 190 iquest 191 "
        "Agrave 192 Aacute 193 Acirc 194 Atilde 195 Auml 196 Aring 197 AElig 198 Ccedil 199 Egrave 200 Eacute 201 Ecirc 202 "
        "Euml 203 Igrave 204 Iacute 205 Icirc 206 Iuml 207 ETH 208 Ntilde 209 Ograve 210 Oacute 211 Ocirc 212 Otilde 213 "
        "Ouml 214 times 215 Oslash 216 Ugrave 217 Uacute 218 Ucirc 219 Uuml 220 Yacute 221 THORN 222 szlig 223 agrave 224 "
        "aacute 225 acirc 226 atilde 227 auml 228 aring 229 aelig 230 ccedil 231 egrave 232 eacute 233 ecirc 234 euml 235 "
        "igrave 236 iacute 237 icirc 238 iuml 239 eth 240 ntilde 241 ograve 242 oacute 243 ocirc 244 otilde 245 ouml 246 "
        "divide 247 oslash 248 ugrave 249 uacute 250 ucirc 251 uuml 252 yacute 253 thorn 254 yuml 255 OElig 338 oelig 339 "
        "Scaron 352 scaron 353 Yuml 376 fnof 402 circ 710 tilde 732 Alpha 913 Beta 914 Gamma 915 Delta 916 Epsilon 917 "
        "Zeta 918 Eta 919 Theta 920 Iota 921 Kappa 922 Lambda 923 Mu 924 Nu 925 Xi 926 Omicron 927 Pi 928 Rho 929 Sigma 931 "
        "Tau 932 Upsilon 933 Phi 934 Chi 935 Psi 936 Omega 937 alpha 945 beta 946 gamma 947 delta 948 epsilon 949 zeta 950 "
        "eta 951 theta 952 iota 953 kappa 954 lambda 955 mu 956 nu 957 xi 958 omicron 959 pi 960 rho 961 sigmaf 962 "
        "sigma 963 tau 964 upsilon 965 phi 966 chi 967 psi 968 omega 969 thetasym 977 upsih 978 piv 982 ensp 8194 "
        "emsp 8195 thinsp 8201 hairsp 8202 zwnj 8204 zwj 8205 lrm 8206 rlm 8207 hyphen 8208 dash 8208 ndash 8211 "
        "mdash 8212 horbar 8213 lsquo 8216 rsquo 8217 rsquor 8217 sbquo 8218 lsquor 8218 ldquo 8220 rdquo 8221 rdquor 8221 "
        "bdquo 8222 ldquor 8222 dagger 8224 Dagger 8225 bull 8226 bullet 8226 nldr 8229 hellip 8230 mldr 8230 permil 8240 "
        "prime 8242 Prime 8243 lsaquo 8249 rsaquo 8250 oline 8254 frasl 8260 euro 8364 image 8465 weierp 8472 real 8476 "
        "trade 8482 TRADE 8482 alefsym 8501 larr 8592 uarr 8593 rarr 8594 darr 8595 harr 8596 crarr 8629 lArr 8656 uArr 8657 "
        "rArr 8658 dArr 8659 hArr 8660 forall 8704 part 8706 exist 8707 empty 8709 nabla 8711 isin 8712 notin 8713 ni 8715 "
        "prod 8719 sum 8721 minus 8722 lowast 8727 radic 8730 prop 8733 infin 8734 ang 8736 and 8743 or 8744 cap 8745 "
        "cup 8746 int 8747 there4 8756 sim 8764 cong 8773 asymp 8776 ne 8800 equiv 8801 le 8804 ge 8805 sub 8834 sup 8835 "
        "nsub 8836 sube 8838 supe 8839 oplus 8853 otimes 8855 perp 8869 sdot 8901 lceil 8968 rceil 8969 lfloor 8970 "
        "rfloor 8971 lang 10216 rang 10217 loz 9674 spades 9824 clubs 9827 hearts 9829 diams 9830 check 10003 cross 10007 "
        "star 9734 starf 9733 Tab 9 NewLine 10 excl 33 num 35 dollar 36 percnt 37 lpar 40 rpar 41 ast 42 plus 43 comma 44 "
        "period 46 sol 47 colon 58 semi 59 equals 61 quest 63 commat 64 lsqb 91 lbrack 91 bsol 92 rsqb 93 rbrack 93 "
        "Hat 94 lowbar 95 grave 96 lcub 123 lbrace 123 verbar 124 vert 124 rcub 125 rbrace 125 half 189 centerdot 183 "
        "ZeroWidthSpace 8203 NonBreakingSpace 160 ThinSpace 8201 copysr 8471 dot 729 laquo 171 raquo 187";
    const QStringList parts = QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i + 1 < parts.size(); i += 2)
        t.insert(parts.at(i), parts.at(i + 1).toUInt());
    return t;
}

static QString fromCodePoint(uint cp)
{
    // Windows-1252 numbers that pages use for quotes and dashes
    static const ushort win[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                   0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                   0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    if (cp >= 0x80 && cp < 0xA0)
        cp = win[cp - 0x80];
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return QString(QChar(0xFFFD));
    const char32_t c = cp;
    return QString::fromUcs4(&c, 1);
}

QString decodeEntities(const QString &s)
{
    if (!s.contains(QLatin1Char('&')))
        return s;
    QString out;
    out.reserve(s.size());
    const QHash<QString, uint> &t = entityTable();
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c != QLatin1Char('&')) {
            out += c;
            continue;
        }
        int j = i + 1;
        if (j < s.size() && s.at(j) == QLatin1Char('#')) {
            ++j;
            bool hex = j < s.size() && (s.at(j) == QLatin1Char('x') || s.at(j) == QLatin1Char('X'));
            if (hex)
                ++j;
            const int start = j;
            while (j < s.size() && (hex ? isxdigit(s.at(j).toLatin1()) : s.at(j).isDigit()) && j - start < 8)
                ++j;
            if (j > start) {
                bool ok = false;
                const uint cp = s.mid(start, j - start).toUInt(&ok, hex ? 16 : 10);
                if (ok) {
                    out += fromCodePoint(cp);
                    if (j < s.size() && s.at(j) == QLatin1Char(';'))
                        ++j;
                    i = j - 1;
                    continue;
                }
            }
            out += c;
            continue;
        }
        const int start = j;
        while (j < s.size() && s.at(j).isLetterOrNumber() && j - start < 32)
            ++j;
        const QString name = s.mid(start, j - start);
        if (j < s.size() && s.at(j) == QLatin1Char(';') && t.contains(name)) {
            out += fromCodePoint(t.value(name));
            i = j;
            continue;
        }
        // old entities without ";" ("&copy 2026"), but not inside addresses ("&lang=en")
        bool done = false;
        for (int len = name.size(); len >= 2 && !done; --len) {
            const auto it = t.constFind(name.left(len));
            if (it == t.constEnd() || it.value() >= 256)
                continue;
            const QChar next = start + len < s.size() ? s.at(start + len) : QChar();
            if (next == QLatin1Char('=') || next.isLetterOrNumber())
                break;
            out += fromCodePoint(it.value());
            i = start + len - 1;
            done = true;
        }
        if (done)
            continue;
        out += c;
    }
    return out;
}

// ------------------------------------------------------------------ decoding

static QString decodeWith(const QByteArray &data, QString name, bool *ok)
{
    *ok = true;
    name = name.trimmed().toLower();
    if (name.isEmpty() || name == QLatin1String("utf-8") || name == QLatin1String("utf8")) {
        QStringDecoder dec(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        const QString s = dec(data);
        *ok = !dec.hasError();
        return s;
    }
    if (name == QLatin1String("iso-8859-1") || name == QLatin1String("latin1") || name == QLatin1String("us-ascii")
        || name == QLatin1String("windows-1252") || name == QLatin1String("cp1252") || name == QLatin1String("ascii")) {
        // browsers read all of these as Windows-1252
        QString s;
        s.reserve(data.size());
        for (char ch : data) {
            const uint b = uchar(ch);
            s += (b >= 0x80 && b < 0xA0) ? fromCodePoint(b) : QString(QChar(b));
        }
        return s;
    }
    QStringDecoder dec(name.toLatin1().constData());
    if (dec.isValid())
        return dec(data);
    *ok = false;
    return QString();
}

QString decodeHtml(const QByteArray &data, const QString &httpCharset)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.mid(3));
    if (data.startsWith("\xFF\xFE"))
        return QStringDecoder(QStringConverter::Utf16LE)(data.mid(2));
    if (data.startsWith("\xFE\xFF"))
        return QStringDecoder(QStringConverter::Utf16BE)(data.mid(2));
    QString declared = httpCharset;
    if (declared.isEmpty()) {
        const QString head = QString::fromLatin1(data.left(8192));
        static const QRegularExpression meta(QStringLiteral("<meta[^>]+charset\\s*=\\s*[\"']?([\\w\\-:.]+)"),
                                             QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression xml(QStringLiteral("<\\?xml[^>]+encoding\\s*=\\s*[\"']([\\w\\-:.]+)"),
                                            QRegularExpression::CaseInsensitiveOption);
        auto m = meta.match(head);
        if (m.hasMatch())
            declared = m.captured(1);
        else if ((m = xml.match(head)).hasMatch())
            declared = m.captured(1);
    }
    bool ok = false;
    // a page saved again as UTF-8 that still names its old character set
    if (!declared.isEmpty() && declared.compare(QLatin1String("utf-8"), Qt::CaseInsensitive) != 0) {
        bool nonAscii = false;
        for (char ch : data)
            if (uchar(ch) >= 0x80) {
                nonAscii = true;
                break;
            }
        if (nonAscii) {
            const QString u = decodeWith(data, QString(), &ok);
            if (ok)
                return u;
        }
    }
    QString s = decodeWith(data, declared, &ok);
    if (ok)
        return s;
    // wrong or unknown declaration: UTF-8 if it is valid, else Windows-1252
    s = decodeWith(data, QString(), &ok);
    if (ok)
        return s;
    return decodeWith(data, QStringLiteral("windows-1252"), &ok);
}

// ------------------------------------------------------------------ parser

bool isBlockTag(const QString &tag)
{
    static const QSet<QString> block = {
        QStringLiteral("address"), QStringLiteral("article"), QStringLiteral("aside"), QStringLiteral("blockquote"),
        QStringLiteral("center"), QStringLiteral("details"), QStringLiteral("dialog"), QStringLiteral("dir"),
        QStringLiteral("div"), QStringLiteral("dl"), QStringLiteral("dd"), QStringLiteral("dt"), QStringLiteral("fieldset"),
        QStringLiteral("figcaption"), QStringLiteral("figure"), QStringLiteral("footer"), QStringLiteral("form"),
        QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"),
        QStringLiteral("h6"), QStringLiteral("header"), QStringLiteral("hgroup"), QStringLiteral("hr"), QStringLiteral("li"),
        QStringLiteral("main"), QStringLiteral("menu"), QStringLiteral("nav"), QStringLiteral("ol"), QStringLiteral("p"),
        QStringLiteral("pre"), QStringLiteral("section"), QStringLiteral("table"), QStringLiteral("ul"),
        QStringLiteral("tr"), QStringLiteral("td"), QStringLiteral("th"), QStringLiteral("tbody"), QStringLiteral("thead"),
        QStringLiteral("tfoot"), QStringLiteral("caption"), QStringLiteral("summary"), QStringLiteral("body"),
        QStringLiteral("html"), QStringLiteral("picture"), QStringLiteral("video"), QStringLiteral("audio"),
        QStringLiteral("noscript"), QStringLiteral("legend")};
    return block.contains(tag);
}

Document::Document()
{
    m_root = create(Node::Element);
}

Node *Document::create(Node::Type type, const QString &tag)
{
    m_nodes.push_back(std::make_unique<Node>());
    Node *n = m_nodes.back().get();
    n->type = type;
    n->tag = tag;
    return n;
}

Node *Document::body() const
{
    if (Node *b = m_root->firstElement(QStringLiteral("body")))
        return b;
    return m_root;
}

void Document::detach(Node *n)
{
    if (!n || !n->parent)
        return;
    n->parent->children.removeOne(n);
    n->parent = nullptr;
}

void Document::insert(Node *parent, int at, Node *n)
{
    detach(n);
    if (at < 0 || at > parent->children.size())
        at = parent->children.size();
    parent->children.insert(at, n);
    n->parent = parent;
}

void Document::replaceWithChildren(Node *n)
{
    Node *p = n->parent;
    if (!p)
        return;
    int at = n->index();
    const QVector<Node *> kids = n->children;
    detach(n);
    for (Node *k : kids)
        insert(p, at++, k);
}

void Document::parse(const QString &h)
{
    static const QSet<QString> voids = {QStringLiteral("area"),  QStringLiteral("base"),   QStringLiteral("br"),
                                        QStringLiteral("col"),   QStringLiteral("embed"),  QStringLiteral("hr"),
                                        QStringLiteral("img"),   QStringLiteral("input"),  QStringLiteral("link"),
                                        QStringLiteral("meta"),  QStringLiteral("param"),  QStringLiteral("source"),
                                        QStringLiteral("track"), QStringLiteral("wbr"),    QStringLiteral("keygen"),
                                        QStringLiteral("basefont"), QStringLiteral("bgsound"), QStringLiteral("frame")};
    static const QSet<QString> raw = {QStringLiteral("script"), QStringLiteral("style"), QStringLiteral("textarea"),
                                      QStringLiteral("title"), QStringLiteral("xmp"), QStringLiteral("iframe"),
                                      QStringLiteral("noembed"), QStringLiteral("noframes"), QStringLiteral("template")};
    static const QSet<QString> closesP = {
        QStringLiteral("address"), QStringLiteral("article"), QStringLiteral("aside"), QStringLiteral("blockquote"),
        QStringLiteral("center"), QStringLiteral("details"), QStringLiteral("dialog"), QStringLiteral("dir"),
        QStringLiteral("div"), QStringLiteral("dl"), QStringLiteral("fieldset"), QStringLiteral("figcaption"),
        QStringLiteral("figure"), QStringLiteral("footer"), QStringLiteral("form"), QStringLiteral("h1"),
        QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("header"), QStringLiteral("hgroup"), QStringLiteral("hr"), QStringLiteral("main"),
        QStringLiteral("menu"), QStringLiteral("nav"), QStringLiteral("ol"), QStringLiteral("p"), QStringLiteral("pre"),
        QStringLiteral("section"), QStringLiteral("table"), QStringLiteral("ul"), QStringLiteral("li"),
        QStringLiteral("dd"), QStringLiteral("dt"), QStringLiteral("summary"), QStringLiteral("listing")};
    static const QSet<QString> pScope = {QStringLiteral("button"), QStringLiteral("table"), QStringLiteral("td"),
                                         QStringLiteral("th"), QStringLiteral("caption"), QStringLiteral("html"),
                                         QStringLiteral("object"), QStringLiteral("marquee"), QStringLiteral("applet")};
    QVector<Node *> stack{m_root};
    int foreign = 0; // inside svg / math: "/>" closes
    auto top = [&]() { return stack.last(); };
    auto closeIfOpen = [&](const QSet<QString> &names, const QSet<QString> &boundary) {
        for (int k = stack.size() - 1; k >= 1; --k) {
            const QString &t = stack.at(k)->tag;
            if (names.contains(t)) {
                stack.resize(k);
                return true;
            }
            if (boundary.contains(t))
                return false;
        }
        return false;
    };
    QString text;
    auto flush = [&]() {
        if (text.isEmpty())
            return;
        Node *p = top();
        // join with a text node before (entities split over chunks)
        if (!p->children.isEmpty() && p->children.last()->type == Node::Text) {
            p->children.last()->text += decodeEntities(text);
        } else {
            Node *t = create(Node::Text);
            t->text = decodeEntities(text);
            t->parent = p;
            p->children << t;
        }
        text.clear();
    };
    auto isNameChar = [](QChar c) {
        return c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char(':') || c == QLatin1Char('_');
    };
    const int n = h.size();
    int i = 0;
    while (i < n) {
        const QChar c = h.at(i);
        if (c != QLatin1Char('<') || i + 1 >= n) {
            // plain text up to the next tag
            const int next = h.indexOf(QLatin1Char('<'), i + 1);
            const int end = next < 0 ? n : next;
            text += QStringView(h).mid(i, end - i);
            i = end;
            continue;
        }
        const QChar d = h.at(i + 1);
        if (d == QLatin1Char('!')) {
            if (QStringView(h).mid(i, 4) == QLatin1String("<!--")) {
                const int e = h.indexOf(QLatin1String("-->"), i + 4);
                i = e < 0 ? n : e + 3;
            } else if (QStringView(h).mid(i, 9).compare(QLatin1String("<![CDATA["), Qt::CaseInsensitive) == 0) {
                const int e = h.indexOf(QLatin1String("]]>"), i + 9);
                QString cdata = h.mid(i + 9, (e < 0 ? n : e) - (i + 9));
                cdata.replace(QLatin1Char('&'), QLatin1String("&amp;"));
                text += cdata;
                i = e < 0 ? n : e + 3;
            } else {
                const int e = h.indexOf(QLatin1Char('>'), i);
                i = e < 0 ? n : e + 1;
            }
            continue;
        }
        if (d == QLatin1Char('?')) {
            const int e = h.indexOf(QLatin1Char('>'), i);
            i = e < 0 ? n : e + 1;
            continue;
        }
        if (d == QLatin1Char('/')) {
            int j = i + 2;
            while (j < n && isNameChar(h.at(j)))
                ++j;
            const QString name = h.mid(i + 2, j - i - 2).toLower();
            const int e = h.indexOf(QLatin1Char('>'), j);
            i = e < 0 ? n : e + 1;
            if (name.isEmpty())
                continue;
            flush();
            if (name == QLatin1String("br")) {
                Node *br = create(Node::Element, name);
                br->parent = top();
                top()->children << br;
                continue;
            }
            if (name == QLatin1String("p") && !closeIfOpen({QStringLiteral("p")}, pScope)) {
                continue; // a stray </p>
            }
            if (name == QLatin1String("p"))
                continue;
            if (name == QLatin1String("svg") || name == QLatin1String("math"))
                foreign = qMax(0, foreign - 1);
            for (int k = stack.size() - 1; k >= 1; --k)
                if (stack.at(k)->tag == name) {
                    stack.resize(k);
                    break;
                }
            continue;
        }
        if (!d.isLetter()) {
            text += c;
            ++i;
            continue;
        }
        // a start tag
        int j = i + 1;
        while (j < n && isNameChar(h.at(j)))
            ++j;
        QString name = h.mid(i + 1, j - i - 1).toLower();
        if (name == QLatin1String("image"))
            name = QStringLiteral("img");
        QVector<QPair<QString, QString>> attrs;
        bool selfClosing = false;
        while (j < n) {
            while (j < n && h.at(j).isSpace())
                ++j;
            if (j >= n)
                break;
            if (h.at(j) == QLatin1Char('>')) {
                ++j;
                break;
            }
            if (h.at(j) == QLatin1Char('/')) {
                if (j + 1 < n && h.at(j + 1) == QLatin1Char('>')) {
                    selfClosing = true;
                    j += 2;
                    break;
                }
                ++j;
                continue;
            }
            const int a0 = j;
            while (j < n && !h.at(j).isSpace() && h.at(j) != QLatin1Char('=') && h.at(j) != QLatin1Char('>')
                   && !(h.at(j) == QLatin1Char('/') && j + 1 < n && h.at(j + 1) == QLatin1Char('>')))
                ++j;
            const QString an = h.mid(a0, j - a0).toLower();
            while (j < n && h.at(j).isSpace())
                ++j;
            QString value;
            if (j < n && h.at(j) == QLatin1Char('=')) {
                ++j;
                while (j < n && h.at(j).isSpace())
                    ++j;
                if (j < n && (h.at(j) == QLatin1Char('"') || h.at(j) == QLatin1Char('\''))) {
                    const QChar q = h.at(j);
                    const int e = h.indexOf(q, j + 1);
                    value = h.mid(j + 1, (e < 0 ? n : e) - j - 1);
                    j = e < 0 ? n : e + 1;
                } else {
                    const int v0 = j;
                    while (j < n && !h.at(j).isSpace() && h.at(j) != QLatin1Char('>'))
                        ++j;
                    value = h.mid(v0, j - v0);
                }
            }
            if (!an.isEmpty() && an != QLatin1String("/"))
                attrs.append({an, decodeEntities(value)});
        }
        i = j;
        flush();
        // the end tags HTML leaves out
        if (name == QLatin1String("html") || name == QLatin1String("head") || name == QLatin1String("body")) {
            Node *existing = m_root->firstElement(name);
            if (existing) {
                for (const auto &a : attrs)
                    if (!existing->hasAttr(a.first))
                        existing->attrs.append(a);
                continue;
            }
            if (name == QLatin1String("body"))
                closeIfOpen({QStringLiteral("head")}, {});
        }
        if (closesP.contains(name))
            closeIfOpen({QStringLiteral("p")}, pScope);
        if (name == QLatin1String("li"))
            closeIfOpen({QStringLiteral("li")}, {QStringLiteral("ul"), QStringLiteral("ol"), QStringLiteral("menu"),
                                                QStringLiteral("table"), QStringLiteral("td"), QStringLiteral("th")});
        else if (name == QLatin1String("dt") || name == QLatin1String("dd"))
            closeIfOpen({QStringLiteral("dt"), QStringLiteral("dd")}, {QStringLiteral("dl"), QStringLiteral("table")});
        else if (name == QLatin1String("tr"))
            closeIfOpen({QStringLiteral("tr")}, {QStringLiteral("table"), QStringLiteral("thead"), QStringLiteral("tbody"),
                                                QStringLiteral("tfoot")});
        else if (name == QLatin1String("td") || name == QLatin1String("th"))
            closeIfOpen({QStringLiteral("td"), QStringLiteral("th")}, {QStringLiteral("tr"), QStringLiteral("table")});
        else if (name == QLatin1String("thead") || name == QLatin1String("tbody") || name == QLatin1String("tfoot"))
            closeIfOpen({QStringLiteral("thead"), QStringLiteral("tbody"), QStringLiteral("tfoot")}, {QStringLiteral("table")});
        else if (name == QLatin1String("option"))
            closeIfOpen({QStringLiteral("option")}, {QStringLiteral("select"), QStringLiteral("datalist"), QStringLiteral("optgroup")});
        else if (name == QLatin1String("a"))
            closeIfOpen({QStringLiteral("a")}, {QStringLiteral("table"), QStringLiteral("td"), QStringLiteral("th")});
        else if (name.size() == 2 && name.at(0) == QLatin1Char('h') && name.at(1) >= QLatin1Char('1') && name.at(1) <= QLatin1Char('6')) {
            const QString &t = top()->tag;
            if (t.size() == 2 && t.at(0) == QLatin1Char('h') && t.at(1) >= QLatin1Char('1') && t.at(1) <= QLatin1Char('6'))
                stack.removeLast();
        }
        Node *el = create(Node::Element, name);
        el->attrs = attrs;
        el->parent = top();
        top()->children << el;
        if (raw.contains(name)) {
            // text up to the end tag
            const QString close = QStringLiteral("</") + name;
            int e = h.indexOf(close, i, Qt::CaseInsensitive);
            if (e < 0)
                e = n;
            QString content = h.mid(i, e - i);
            if (name == QLatin1String("title") || name == QLatin1String("textarea"))
                content = decodeEntities(content);
            if (!content.isEmpty()) {
                Node *t = create(Node::Text);
                t->text = content;
                t->parent = el;
                el->children << t;
            }
            const int gt = h.indexOf(QLatin1Char('>'), e);
            i = gt < 0 ? n : gt + 1;
            continue;
        }
        if (name == QLatin1String("svg") || name == QLatin1String("math")) {
            if (!selfClosing) {
                ++foreign;
                stack << el;
            }
            continue;
        }
        if (voids.contains(name) || (selfClosing && foreign > 0))
            continue;
        stack << el;
    }
    flush();
}

} // namespace qbweb
