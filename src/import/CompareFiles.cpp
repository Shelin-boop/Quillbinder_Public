#include "CompareFiles.h"

#include "core/Zip.h"
#include "import/FileImporter.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextDocument>

namespace qb {
namespace compare {

using xml::Node;

// ---------------------------------------------------------------- base

bool FileText::replace(int, int, int, const QString &, const QVector<quint8> &) { return false; }
bool FileText::setFormat(int, int, int, const QVector<quint8> &) { return false; }
bool FileText::insertParagraphs(int, int, const QList<Para> &) { return false; }
bool FileText::deleteParagraph(int) { return false; }
bool FileText::save(const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("This kind of file cannot be written.");
    return false;
}

// the same file with one entry replaced; "mimetype" first and stored (ODF)
static bool rewriteZip(const QString &from, const QString &to, const QString &entry, const QByteArray &data, QString *error)
{
    ZipReader zr(from);
    if (!zr.isValid()) {
        if (error)
            *error = QStringLiteral("The file is no longer readable: %1").arg(from);
        return false;
    }
    QStringList names = zr.fileNames();
    if (names.removeAll(QStringLiteral("mimetype")))
        names.prepend(QStringLiteral("mimetype"));
    ZipWriter zw(to);
    if (!zw.isOpen()) {
        if (error)
            *error = zw.errorString();
        return false;
    }
    for (const QString &n : names) {
        const QByteArray d = n == entry ? data : zr.fileData(n);
        if (!zw.addFile(n, d, n != QLatin1String("mimetype"))) {
            if (error)
                *error = zw.errorString();
            return false;
        }
    }
    if (!zw.close()) {
        if (error)
            *error = zw.errorString();
        return false;
    }
    return true;
}

// pieces of equal formatting
static QList<QPair<QString, quint8>> segments(const QString &text, const QVector<quint8> &fmt)
{
    QList<QPair<QString, quint8>> out;
    for (int i = 0; i < text.size(); ++i) {
        const quint8 f = i < fmt.size() ? fmt.at(i) : 0;
        if (out.isEmpty() || out.last().second != f)
            out.append({QString(), f});
        out.last().first += text.at(i);
    }
    return out;
}

// ---------------------------------------------------------------- Word

static bool onOff(const Node *e)
{
    if (!e)
        return false;
    const QString v = e->attr(QStringLiteral("w:val"));
    return !(v == QLatin1String("0") || v == QLatin1String("false") || v == QLatin1String("off"));
}

static quint8 runFmt(const Node *r)
{
    const Node *rPr = r ? r->child("w:rPr") : nullptr;
    if (!rPr)
        return 0;
    quint8 f = 0;
    if (onOff(rPr->child("w:b")))
        f |= Bold;
    if (onOff(rPr->child("w:i")))
        f |= Italic;
    if (const Node *u = rPr->child("w:u"))
        if (u->attr(QStringLiteral("w:val")) != QLatin1String("none"))
            f |= Underline;
    return f;
}

// the order of the children of w:rPr (Word is strict about it)
static int rPrRank(const QString &name)
{
    static const QStringList order = {
        QStringLiteral("w:ins"), QStringLiteral("w:del"), QStringLiteral("w:moveFrom"), QStringLiteral("w:moveTo"),
        QStringLiteral("w:rStyle"), QStringLiteral("w:rFonts"), QStringLiteral("w:b"), QStringLiteral("w:bCs"),
        QStringLiteral("w:i"), QStringLiteral("w:iCs"), QStringLiteral("w:caps"), QStringLiteral("w:smallCaps"),
        QStringLiteral("w:strike"), QStringLiteral("w:dstrike"), QStringLiteral("w:outline"), QStringLiteral("w:shadow"),
        QStringLiteral("w:emboss"), QStringLiteral("w:imprint"), QStringLiteral("w:noProof"), QStringLiteral("w:snapToGrid"),
        QStringLiteral("w:vanish"), QStringLiteral("w:webHidden"), QStringLiteral("w:color"), QStringLiteral("w:spacing"),
        QStringLiteral("w:w"), QStringLiteral("w:kern"), QStringLiteral("w:position"), QStringLiteral("w:sz"),
        QStringLiteral("w:szCs"), QStringLiteral("w:highlight"), QStringLiteral("w:u"), QStringLiteral("w:effect"),
        QStringLiteral("w:bdr"), QStringLiteral("w:shd"), QStringLiteral("w:fitText"), QStringLiteral("w:vertAlign"),
        QStringLiteral("w:rtl"), QStringLiteral("w:cs"), QStringLiteral("w:em"), QStringLiteral("w:lang"),
        QStringLiteral("w:eastAsianLayout"), QStringLiteral("w:specVanish"), QStringLiteral("w:oMath"),
        QStringLiteral("w:rPrChange")};
    const int i = int(order.indexOf(name));
    return i < 0 ? 40 : i;
}

static void putInOrder(Node *parent, std::unique_ptr<Node> n, const std::function<int(const QString &)> &rank)
{
    const int r = rank(n->name);
    int at = int(parent->children.size());
    for (size_t i = 0; i < parent->children.size(); ++i)
        if (parent->children[i]->type == Node::Element && rank(parent->children[i]->name) > r) {
            at = int(i);
            break;
        }
    parent->insert(at, std::move(n));
}

static void setRunFlag(Node *rPr, const char *name, bool on)
{
    if (Node *e = rPr->child(name))
        rPr->take(e);
    if (!on && QLatin1String(name) != QLatin1String("w:u"))
        return; // off: no element (the style's value counts)
    auto e = xml::element(QLatin1String(name));
    if (QLatin1String(name) == QLatin1String("w:u"))
        e->setAttr(QStringLiteral("w:val"), on ? QStringLiteral("single") : QStringLiteral("none"));
    putInOrder(rPr, std::move(e), rPrRank);
}

static void setRunFmt(Node *rPr, quint8 f, quint8 before)
{
    if ((f & Bold) != (before & Bold))
        setRunFlag(rPr, "w:b", f & Bold);
    if ((f & Italic) != (before & Italic))
        setRunFlag(rPr, "w:i", f & Italic);
    if ((f & Underline) != (before & Underline)) {
        if (f & Underline)
            setRunFlag(rPr, "w:u", true);
        else if (Node *u = rPr->child("w:u"))
            rPr->take(u);
    }
}

// pPr children order: rPr comes after almost everything, before sectPr and pPrChange
static int pPrRank(const QString &name)
{
    if (name == QLatin1String("w:pStyle"))
        return 0;
    if (name == QLatin1String("w:rPr"))
        return 90;
    if (name == QLatin1String("w:sectPr"))
        return 91;
    if (name == QLatin1String("w:pPrChange"))
        return 92;
    return 50;
}

QVector<DocxText::Loc> DocxText::scan(Node *p, QString *text, QVector<quint8> *fmt) const
{
    QVector<Loc> locs;
    bool inCode = false;
    std::function<void(Node *)> visit = [&](Node *n) {
        for (size_t i = 0; i < n->children.size(); ++i) {
            Node *c = n->children[i].get();
            if (c->type != Node::Element)
                continue;
            const QString &nm = c->name;
            if (nm == QLatin1String("w:r")) {
                const quint8 f = runFmt(c);
                for (size_t k = 0; k < c->children.size(); ++k) {
                    Node *e = c->children[k].get();
                    if (e->type != Node::Element)
                        continue;
                    const QString &en = e->name;
                    auto add = [&](QChar ch, int off) {
                        text->append(ch);
                        fmt->append(f);
                        locs.append({c, e, off});
                    };
                    if (en == QLatin1String("w:fldChar")) {
                        const QString t = e->attr(QStringLiteral("w:fldCharType"));
                        if (t == QLatin1String("begin"))
                            inCode = true;
                        else if (t == QLatin1String("separate") || t == QLatin1String("end"))
                            inCode = false;
                    } else if (en == QLatin1String("w:t") && !inCode) {
                        const QString t = e->textContent();
                        for (int j = 0; j < t.size(); ++j)
                            add(t.at(j), j);
                    } else if (en == QLatin1String("w:tab") && !inCode) {
                        add(QLatin1Char('\t'), -1);
                    } else if ((en == QLatin1String("w:br") || en == QLatin1String("w:cr")) && !inCode) {
                        const QString t = e->attr(QStringLiteral("w:type"));
                        if (t != QLatin1String("page") && t != QLatin1String("column"))
                            add(QLatin1Char('\n'), -1);
                    } else if (en == QLatin1String("w:noBreakHyphen") && !inCode) {
                        add(QLatin1Char('-'), -1);
                    }
                }
            } else if (nm == QLatin1String("w:ins") || nm == QLatin1String("w:moveTo") || nm == QLatin1String("w:hyperlink")
                       || nm == QLatin1String("w:smartTag") || nm == QLatin1String("w:customXml") || nm == QLatin1String("w:fldSimple")
                       || nm == QLatin1String("w:sdt") || nm == QLatin1String("w:sdtContent") || nm == QLatin1String("w:bdo")
                       || nm == QLatin1String("w:dir")) {
                visit(c);
            }
            // w:del, w:moveFrom (deleted text), w:pPr, bookmarks, comments ...: not text
        }
    };
    visit(p);
    return locs;
}

bool DocxText::load(const QString &path, QString *error)
{
    m_path = path;
    kind = QStringLiteral("docx");
    ZipReader zr(path);
    const QByteArray data = zr.isValid() ? zr.fileData(QStringLiteral("word/document.xml")) : QByteArray();
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("This is not a Word file (.docx), or it is damaged.");
        return false;
    }
    if (!m_doc.parse(data, error))
        return false;
    // heading levels of the paragraph styles
    xml::Document styles;
    if (styles.parse(zr.fileData(QStringLiteral("word/styles.xml")))) {
        static const QRegularExpression headingName(QStringLiteral("^heading\\s*(\\d)$"), QRegularExpression::CaseInsensitiveOption);
        for (Node *s : styles.documentElement()->elements("w:style")) {
            const QString id = s->attr(QStringLiteral("w:styleId"));
            const Node *nm = s->child("w:name");
            const QString name = nm ? nm->attr(QStringLiteral("w:val")) : id;
            int level = 0;
            const auto m = headingName.match(name);
            if (m.hasMatch())
                level = m.captured(1).toInt();
            else if (name.compare(QLatin1String("Title"), Qt::CaseInsensitive) == 0)
                level = 1;
            if (const Node *pPr = s->child("w:pPr"))
                if (const Node *ol = pPr->child("w:outlineLvl"))
                    level = ol->attr(QStringLiteral("w:val")).toInt() + 1;
            if (level > 0 && level < 10)
                m_styleHeading.insert(id, level);
        }
    }
    // the paragraphs of the main text (text boxes and fallbacks left out)
    int maxId = 0;
    Node *body = m_doc.documentElement() ? m_doc.documentElement()->child("w:body") : nullptr;
    if (!body) {
        if (error)
            *error = QStringLiteral("The Word file has no text.");
        return false;
    }
    xml::walk(m_doc.documentElement(), [&](Node *n) {
        const QString id = n->attr(QStringLiteral("w:id"));
        bool ok = false;
        const int v = id.toInt(&ok);
        if (ok)
            maxId = qMax(maxId, v);
        return true;
    });
    m_nextId = maxId + 1000;
    xml::walk(body, [&](Node *n) {
        if (n->name == QLatin1String("w:p")) {
            m_pNodes << n;
            return false;
        }
        return !(n->name == QLatin1String("w:txbxContent") || n->name == QLatin1String("mc:Fallback")
                 || n->name == QLatin1String("w:del") || n->name == QLatin1String("w:moveFrom") || n->name == QLatin1String("w:sectPr"));
    });
    for (int i = 0; i < m_pNodes.size(); ++i) {
        Para p;
        p.source = i;
        scan(m_pNodes.at(i), &p.text, &p.fmt);
        if (const Node *pPr = m_pNodes.at(i)->child("w:pPr")) {
            if (const Node *ps = pPr->child("w:pStyle"))
                p.heading = m_styleHeading.value(ps->attr(QStringLiteral("w:val")));
            if (const Node *ol = pPr->child("w:outlineLvl")) {
                const int l = ol->attr(QStringLiteral("w:val")).toInt();
                if (l < 9)
                    p.heading = l + 1;
            }
        }
        paras << p;
    }
    date = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
    return true;
}

std::unique_ptr<Node> DocxText::changeMark(const char *name)
{
    auto e = xml::element(QLatin1String(name));
    e->setAttr(QStringLiteral("w:id"), nextId());
    e->setAttr(QStringLiteral("w:author"), author);
    e->setAttr(QStringLiteral("w:date"), date);
    return e;
}

static void preserveSpace(Node *t)
{
    const QString s = t->textContent();
    if (!s.isEmpty() && (s.front().isSpace() || s.back().isSpace()))
        t->setAttr(QStringLiteral("xml:space"), QStringLiteral("preserve"));
}

Node *DocxText::splitAt(Node *p, int pos)
{
    QString text;
    QVector<quint8> fmt;
    QVector<Loc> locs = scan(p, &text, &fmt);
    if (pos < 0 || pos >= locs.size())
        return nullptr;
    Loc L = locs.at(pos);
    Node *run = L.run;
    Node *node = L.node;
    if (node->name == QLatin1String("w:t") && L.offset > 0) {
        const QString s = node->textContent();
        auto second = xml::element(QStringLiteral("w:t"));
        second->attrs = node->attrs;
        second->append(xml::textNode(s.mid(L.offset)));
        node->children.clear();
        node->append(xml::textNode(s.left(L.offset)));
        preserveSpace(node);
        preserveSpace(second.get());
        node = run->insert(run->indexOf(node) + 1, std::move(second));
    }
    // is it the first content of its run?
    const int idx = run->indexOf(node);
    int firstContent = -1;
    for (size_t i = 0; i < run->children.size(); ++i)
        if (run->children[i]->type == Node::Element && run->children[i]->name != QLatin1String("w:rPr")) {
            firstContent = int(i);
            break;
        }
    if (idx == firstContent)
        return run;
    // a new run from here on, with the same properties
    auto nr = xml::element(QStringLiteral("w:r"));
    nr->attrs = run->attrs;
    if (const Node *rPr = run->child("w:rPr"))
        nr->append(rPr->clone());
    while (int(run->children.size()) > idx)
        nr->append(run->take(run->children[size_t(idx)].get()));
    Node *parent = run->parent;
    return parent->insert(parent->indexOf(run) + 1, std::move(nr));
}

std::unique_ptr<Node> DocxText::newRun(const Node *likeRun, const QString &text, quint8 fmt) const
{
    auto r = xml::element(QStringLiteral("w:r"));
    quint8 before = 0;
    auto rPr = xml::element(QStringLiteral("w:rPr"));
    if (likeRun) {
        if (const Node *old = likeRun->child("w:rPr")) {
            rPr = old->clone();
            if (Node *ch = rPr->child("w:rPrChange"))
                rPr->take(ch);
            before = runFmt(likeRun);
        }
    }
    setRunFmt(rPr.get(), fmt, before);
    if (!rPr->children.empty())
        r->append(std::move(rPr));
    // text, tabs and line breaks
    QString buf;
    auto flush = [&] {
        if (buf.isEmpty())
            return;
        auto t = xml::element(QStringLiteral("w:t"));
        t->append(xml::textNode(buf));
        preserveSpace(t.get());
        r->append(std::move(t));
        buf.clear();
    };
    for (const QChar c : text) {
        if (c == QLatin1Char('\t')) {
            flush();
            r->append(xml::element(QStringLiteral("w:tab")));
        } else if (c == QLatin1Char('\n')) {
            flush();
            r->append(xml::element(QStringLiteral("w:br")));
        } else {
            buf += c;
        }
    }
    flush();
    return r;
}

static void toDeleted(Node *run)
{
    for (auto &c : run->children) {
        if (c->type != Node::Element)
            continue;
        if (c->name == QLatin1String("w:t")) {
            c->name = QStringLiteral("w:delText");
            preserveSpace(c.get());
        } else if (c->name == QLatin1String("w:instrText")) {
            c->name = QStringLiteral("w:delInstrText");
        }
    }
}

bool DocxText::replace(int para, int start, int end, const QString &text, const QVector<quint8> &fmt)
{
    if (para < 0 || para >= m_pNodes.size())
        return false;
    Node *p = m_pNodes.at(para);
    QString cur;
    QVector<quint8> curFmt;
    QVector<Loc> locs = scan(p, &cur, &curFmt);
    if (start < 0 || end > locs.size() || start > end)
        return false;
    // the run whose look new text takes: the one at the start, else the one before
    Node *like = start < locs.size() ? locs.at(start).run : (locs.isEmpty() ? nullptr : locs.last().run);
    Node *insertParent = nullptr;
    int insertIndex = -1;
    if (end > start) {
        splitAt(p, start);
        splitAt(p, end);
        locs = scan(p, &cur, &curFmt);
        QList<Node *> runs;
        for (int i = start; i < end; ++i)
            if (!runs.contains(locs.at(i).run))
                runs << locs.at(i).run;
        Node *lastDel = nullptr;
        for (Node *r : runs) {
            Node *parent = r->parent;
            const int at = parent->indexOf(r);
            auto del = changeMark("w:del");
            Node *d = parent->insert(at, std::move(del));
            d->append(parent->take(r));
            toDeleted(r);
            lastDel = d;
        }
        if (lastDel) {
            insertParent = lastDel->parent;
            insertIndex = insertParent->indexOf(lastDel) + 1;
        }
    } else if (start < locs.size()) {
        Node *r = splitAt(p, start);
        insertParent = r->parent;
        insertIndex = insertParent->indexOf(r);
    } else if (!locs.isEmpty()) {
        Node *r = locs.last().run;
        insertParent = r->parent;
        insertIndex = insertParent->indexOf(r) + 1;
    } else {
        insertParent = p;
        insertIndex = -1;
    }
    if (!text.isEmpty()) {
        auto ins = changeMark("w:ins");
        for (const auto &seg : segments(text, fmt))
            ins->append(newRun(like, seg.first, seg.second));
        insertParent->insert(insertIndex, std::move(ins));
    }
    ++changes;
    return true;
}

bool DocxText::setFormat(int para, int start, int end, const QVector<quint8> &fmt)
{
    if (para < 0 || para >= m_pNodes.size() || start >= end)
        return false;
    Node *p = m_pNodes.at(para);
    QString cur;
    QVector<quint8> curFmt;
    QVector<Loc> locs = scan(p, &cur, &curFmt);
    if (end > locs.size())
        return false;
    // runs split where the wanted look changes
    splitAt(p, start);
    for (int i = start + 1; i < end; ++i)
        if (fmt.value(i - start) != fmt.value(i - start - 1))
            splitAt(p, i);
    splitAt(p, end);
    locs = scan(p, &cur, &curFmt);
    QList<Node *> done;
    for (int i = start; i < end; ++i) {
        Node *r = locs.at(i).run;
        if (done.contains(r))
            continue;
        done << r;
        const quint8 want = fmt.value(i - start);
        const quint8 have = runFmt(r);
        if (want == have)
            continue;
        Node *rPr = r->child("w:rPr");
        if (!rPr)
            rPr = r->insert(0, xml::element(QStringLiteral("w:rPr")));
        auto old = rPr->clone();
        if (Node *ch = old->child("w:rPrChange"))
            old->take(ch);
        if (Node *ch = rPr->child("w:rPrChange"))
            rPr->take(ch);
        setRunFmt(rPr, want, have);
        auto change = changeMark("w:rPrChange");
        change->append(std::move(old));
        putInOrder(rPr, std::move(change), rPrRank);
    }
    ++changes;
    return true;
}

static Node *paraMarkRPr(Node *p)
{
    Node *pPr = p->child("w:pPr");
    if (!pPr)
        pPr = p->insert(0, xml::element(QStringLiteral("w:pPr")));
    Node *rPr = pPr->child("w:rPr");
    if (!rPr) {
        putInOrder(pPr, xml::element(QStringLiteral("w:rPr")), pPrRank);
        rPr = pPr->child("w:rPr");
    }
    return rPr;
}

bool DocxText::insertParagraphs(int after, int styleLike, const QList<Para> &ps)
{
    if (ps.isEmpty())
        return true;
    Node *like = styleLike >= 0 && styleLike < m_pNodes.size() ? m_pNodes.at(styleLike) : nullptr;
    Node *anchor = after >= 0 && after < m_pNodes.size() ? m_pNodes.at(after) : nullptr;
    Node *parent = anchor ? anchor->parent : (m_pNodes.isEmpty() ? nullptr : m_pNodes.first()->parent);
    if (!parent)
        return false;
    int at = anchor ? parent->indexOf(anchor) + 1 : parent->indexOf(m_pNodes.first());
    // the run whose look the text takes: the first run of the model paragraph
    const Node *likeRun = nullptr;
    if (like) {
        QString t;
        QVector<quint8> f;
        const QVector<Loc> l = scan(like, &t, &f);
        if (!l.isEmpty())
            likeRun = l.first().run;
    }
    for (const Para &para : ps) {
        auto np = xml::element(QStringLiteral("w:p"));
        if (like) {
            if (const Node *pPr = like->child("w:pPr")) {
                auto c = pPr->clone();
                for (const char *drop : {"w:sectPr", "w:pPrChange", "w:rPr"})
                    if (Node *d = c->child(drop))
                        c->take(d);
                np->append(std::move(c));
            }
        }
        Node *rPr = paraMarkRPr(np.get());
        rPr->insert(0, changeMark("w:ins"));
        auto ins = changeMark("w:ins");
        for (const auto &seg : segments(para.text, para.fmt))
            ins->append(newRun(likeRun, seg.first, seg.second));
        np->append(std::move(ins));
        parent->insert(at++, std::move(np));
    }
    ++changes;
    return true;
}

bool DocxText::deleteParagraph(int para)
{
    if (para < 0 || para >= m_pNodes.size())
        return false;
    Node *p = m_pNodes.at(para);
    QString t;
    QVector<quint8> f;
    const QVector<Loc> locs = scan(p, &t, &f);
    QList<Node *> runs;
    for (const Loc &l : locs)
        if (!runs.contains(l.run))
            runs << l.run;
    for (Node *r : runs) {
        Node *parent = r->parent;
        Node *d = parent->insert(parent->indexOf(r), changeMark("w:del"));
        d->append(parent->take(r));
        toDeleted(r);
    }
    Node *rPr = paraMarkRPr(p);
    int at = 0;
    if (rPr->child("w:ins"))
        at = 1;
    rPr->insert(at, changeMark("w:del"));
    ++changes;
    return true;
}

bool DocxText::save(const QString &path, QString *error)
{
    return rewriteZip(m_path, path, QStringLiteral("word/document.xml"), m_doc.serialize(), error);
}

// ---------------------------------------------------------------- LibreOffice

static quint8 textPropsFmt(const Node *tp, quint8 *mask)
{
    quint8 f = 0, m = 0;
    if (!tp)
        return 0;
    if (tp->hasAttr(QStringLiteral("fo:font-weight"))) {
        m |= Bold;
        const QString w = tp->attr(QStringLiteral("fo:font-weight"));
        if (w == QLatin1String("bold") || w.toInt() >= 600)
            f |= Bold;
    }
    if (tp->hasAttr(QStringLiteral("fo:font-style"))) {
        m |= Italic;
        const QString s = tp->attr(QStringLiteral("fo:font-style"));
        if (s == QLatin1String("italic") || s == QLatin1String("oblique"))
            f |= Italic;
    }
    if (tp->hasAttr(QStringLiteral("style:text-underline-style"))) {
        m |= Underline;
        if (tp->attr(QStringLiteral("style:text-underline-style")) != QLatin1String("none"))
            f |= Underline;
    }
    *mask = m;
    return f;
}

bool OdtText::load(const QString &path, QString *error)
{
    m_path = path;
    kind = QStringLiteral("odt");
    ZipReader zr(path);
    const QByteArray data = zr.isValid() ? zr.fileData(QStringLiteral("content.xml")) : QByteArray();
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("This is not a LibreOffice text (.odt), or it is damaged.");
        return false;
    }
    if (!m_doc.parse(data, error))
        return false;
    Node *root = m_doc.documentElement();
    if (Node *as = root->child("office:automatic-styles"))
        for (Node *s : as->elements("style:style")) {
            const QString name = s->attr(QStringLiteral("style:name"));
            quint8 mask = 0;
            const quint8 f = textPropsFmt(s->child("style:text-properties"), &mask);
            m_styleFmt.insert(name, f);
            m_styleMask.insert(name, mask);
            m_styleParent.insert(name, s->attr(QStringLiteral("style:parent-style-name")));
        }
    Node *body = root->child("office:body");
    Node *textEl = body ? body->child("office:text") : nullptr;
    if (!textEl) {
        if (error)
            *error = QStringLiteral("The LibreOffice file has no text.");
        return false;
    }
    xml::walk(textEl, [&](Node *n) {
        if (n->name == QLatin1String("text:p") || n->name == QLatin1String("text:h")) {
            m_pNodes << n;
            return false;
        }
        return !(n->name == QLatin1String("text:note") || n->name == QLatin1String("office:annotation")
                 || n->name == QLatin1String("draw:frame") || n->name == QLatin1String("text:tracked-changes")
                 || n->name == QLatin1String("text:table-of-content") || n->name == QLatin1String("text:sequence-decls"));
    });
    for (int i = 0; i < m_pNodes.size(); ++i) {
        Para p;
        p.source = i;
        scan(m_pNodes.at(i), &p.text, &p.fmt);
        if (m_pNodes.at(i)->name == QLatin1String("text:h")) {
            const int l = m_pNodes.at(i)->attr(QStringLiteral("text:outline-level")).toInt();
            p.heading = l > 0 ? l : 1;
        }
        paras << p;
    }
    return true;
}

quint8 OdtText::fmtAt(Node *n) const
{
    quint8 f = 0, decided = 0;
    for (Node *a = n; a; a = a->parent) {
        if (a->type != Node::Element)
            continue;
        const QString st = a->attr(QStringLiteral("text:style-name"));
        if (!st.isEmpty() && m_styleMask.contains(st)) {
            const quint8 m = m_styleMask.value(st) & ~decided;
            f |= m_styleFmt.value(st) & m;
            decided |= m;
        }
        if (a->name == QLatin1String("text:p") || a->name == QLatin1String("text:h"))
            break;
    }
    return f;
}

QVector<OdtText::Loc> OdtText::scan(Node *p, QString *text, QVector<quint8> *fmt) const
{
    QVector<Loc> locs;
    std::function<void(Node *)> visit = [&](Node *n) {
        for (size_t i = 0; i < n->children.size(); ++i) {
            Node *c = n->children[i].get();
            if (c->type == Node::Text) {
                const quint8 f = fmtAt(n);
                for (int j = 0; j < c->text.size(); ++j) {
                    QChar ch = c->text.at(j);
                    if (ch == QLatin1Char('\n') || ch == QLatin1Char('\t') || ch == QLatin1Char('\r'))
                        ch = QLatin1Char(' ');
                    text->append(ch);
                    fmt->append(f);
                    locs.append({c, j});
                }
                continue;
            }
            if (c->type != Node::Element)
                continue;
            const QString &nm = c->name;
            if (nm == QLatin1String("text:s")) {
                const int count = qMax(1, c->attr(QStringLiteral("text:c")).toInt());
                const quint8 f = fmtAt(n);
                for (int k = 0; k < count; ++k) {
                    text->append(QLatin1Char(' '));
                    fmt->append(f);
                    locs.append({c, k});
                }
            } else if (nm == QLatin1String("text:tab")) {
                text->append(QLatin1Char('\t'));
                fmt->append(fmtAt(n));
                locs.append({c, 0});
            } else if (nm == QLatin1String("text:line-break")) {
                text->append(QLatin1Char('\n'));
                fmt->append(fmtAt(n));
                locs.append({c, 0});
            } else if (nm == QLatin1String("text:span") || nm == QLatin1String("text:a") || nm == QLatin1String("text:meta")
                       || nm == QLatin1String("text:ruby-base") || nm == QLatin1String("text:ruby")) {
                visit(c);
            }
            // notes, comments, frames, bookmarks, fields' own markers: not text
        }
    };
    visit(p);
    return locs;
}

void OdtText::splitText(Node *p, int pos)
{
    QString t;
    QVector<quint8> f;
    const QVector<Loc> locs = scan(p, &t, &f);
    if (pos <= 0 || pos >= locs.size())
        return;
    const Loc L = locs.at(pos);
    if (L.offset <= 0)
        return;
    Node *parent = L.node->parent;
    if (L.node->type == Node::Text) {
        auto second = xml::textNode(L.node->text.mid(L.offset));
        L.node->text = L.node->text.left(L.offset);
        parent->insert(parent->indexOf(L.node) + 1, std::move(second));
    } else if (L.node->name == QLatin1String("text:s")) {
        const int count = qMax(1, L.node->attr(QStringLiteral("text:c")).toInt());
        auto second = xml::element(QStringLiteral("text:s"));
        if (count - L.offset > 1)
            second->setAttr(QStringLiteral("text:c"), QString::number(count - L.offset));
        if (L.offset > 1)
            L.node->setAttr(QStringLiteral("text:c"), QString::number(L.offset));
        else
            L.node->removeAttr(QStringLiteral("text:c"));
        parent->insert(parent->indexOf(L.node) + 1, std::move(second));
    }
}

QString OdtText::styleFor(quint8 fmt)
{
    if (m_madeStyles.contains(fmt))
        return m_madeStyles.value(fmt);
    const QString name = QStringLiteral("QB_T%1").arg(fmt);
    Node *root = m_doc.documentElement();
    Node *as = root->child("office:automatic-styles");
    if (!as) {
        int at = 0;
        for (size_t i = 0; i < root->children.size(); ++i)
            if (root->children[i]->name == QLatin1String("office:body"))
                at = int(i);
        as = root->insert(at, xml::element(QStringLiteral("office:automatic-styles")));
    }
    auto s = xml::element(QStringLiteral("style:style"));
    s->setAttr(QStringLiteral("style:name"), name);
    s->setAttr(QStringLiteral("style:family"), QStringLiteral("text"));
    auto tp = xml::element(QStringLiteral("style:text-properties"));
    const QString w = (fmt & Bold) ? QStringLiteral("bold") : QStringLiteral("normal");
    const QString it = (fmt & Italic) ? QStringLiteral("italic") : QStringLiteral("normal");
    tp->setAttr(QStringLiteral("fo:font-weight"), w);
    tp->setAttr(QStringLiteral("style:font-weight-asian"), w);
    tp->setAttr(QStringLiteral("style:font-weight-complex"), w);
    tp->setAttr(QStringLiteral("fo:font-style"), it);
    tp->setAttr(QStringLiteral("style:font-style-asian"), it);
    tp->setAttr(QStringLiteral("style:font-style-complex"), it);
    tp->setAttr(QStringLiteral("style:text-underline-style"), (fmt & Underline) ? QStringLiteral("solid") : QStringLiteral("none"));
    if (fmt & Underline) {
        tp->setAttr(QStringLiteral("style:text-underline-width"), QStringLiteral("auto"));
        tp->setAttr(QStringLiteral("style:text-underline-color"), QStringLiteral("font-color"));
    }
    s->append(std::move(tp));
    as->append(std::move(s));
    m_madeStyles.insert(fmt, name);
    m_styleFmt.insert(name, fmt);
    m_styleMask.insert(name, Bold | Italic | Underline);
    return name;
}

void OdtText::appendOdfText(Node *parent, int index, const QString &text, quint8 fmt, quint8 context)
{
    Node *target = parent;
    int at = index;
    if (fmt != context) {
        auto span = xml::element(QStringLiteral("text:span"));
        span->setAttr(QStringLiteral("text:style-name"), styleFor(fmt));
        target = parent->insert(index, std::move(span));
        at = -1;
    }
    auto put = [&](std::unique_ptr<Node> n) {
        target->insert(at, std::move(n));
        if (at >= 0)
            ++at;
    };
    QString buf;
    auto flush = [&] {
        if (!buf.isEmpty())
            put(xml::textNode(buf));
        buf.clear();
    };
    int spaces = 0;
    auto flushSpaces = [&] {
        if (spaces > 0) {
            auto s = xml::element(QStringLiteral("text:s"));
            if (spaces > 1)
                s->setAttr(QStringLiteral("text:c"), QString::number(spaces));
            put(std::move(s));
        }
        spaces = 0;
    };
    QChar prev;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char(' ') && (i == 0 || prev == QLatin1Char(' '))) {
            // a space that would collapse: text:s
            flush();
            ++spaces;
        } else if (c == QLatin1Char('\t')) {
            flush();
            flushSpaces();
            put(xml::element(QStringLiteral("text:tab")));
        } else if (c == QLatin1Char('\n')) {
            flush();
            flushSpaces();
            put(xml::element(QStringLiteral("text:line-break")));
        } else {
            flushSpaces();
            buf += c;
        }
        prev = c;
    }
    flush();
    flushSpaces();
}

bool OdtText::replace(int para, int start, int end, const QString &text, const QVector<quint8> &fmt)
{
    if (para < 0 || para >= m_pNodes.size())
        return false;
    Node *p = m_pNodes.at(para);
    QString cur;
    QVector<quint8> curFmt;
    QVector<Loc> locs = scan(p, &cur, &curFmt);
    if (start < 0 || end > locs.size() || start > end)
        return false;
    splitText(p, start);
    splitText(p, end);
    locs = scan(p, &cur, &curFmt);
    Node *parent = nullptr;
    int index = -1;
    if (end > start) {
        QList<Node *> nodes;
        for (int i = start; i < end; ++i)
            if (!nodes.contains(locs.at(i).node))
                nodes << locs.at(i).node;
        parent = nodes.first()->parent;
        index = parent->indexOf(nodes.first());
        for (Node *n : nodes)
            n->parent->take(n);
    } else if (start < locs.size()) {
        parent = locs.at(start).node->parent;
        index = parent->indexOf(locs.at(start).node);
    } else if (!locs.isEmpty()) {
        parent = locs.last().node->parent;
        index = parent->indexOf(locs.last().node) + 1;
    } else {
        parent = p;
        index = -1;
    }
    const quint8 context = fmtAt(parent);
    // the pieces in reverse, each at the same index, keep their order
    const auto segs = segments(text, fmt);
    for (int i = int(segs.size()) - 1; i >= 0; --i)
        appendOdfText(parent, index < 0 ? int(parent->children.size()) : index, segs.at(i).first, segs.at(i).second, context);
    ++changes;
    return true;
}

bool OdtText::setFormat(int para, int start, int end, const QVector<quint8> &fmt)
{
    if (para < 0 || para >= m_pNodes.size() || start >= end)
        return false;
    Node *p = m_pNodes.at(para);
    splitText(p, start);
    for (int i = start + 1; i < end; ++i)
        if (fmt.value(i - start) != fmt.value(i - start - 1))
            splitText(p, i);
    splitText(p, end);
    QString cur;
    QVector<quint8> curFmt;
    const QVector<Loc> locs = scan(p, &cur, &curFmt);
    if (end > locs.size())
        return false;
    QList<Node *> done;
    for (int i = start; i < end; ++i) {
        Node *n = locs.at(i).node;
        if (done.contains(n))
            continue;
        done << n;
        const quint8 want = fmt.value(i - start);
        if (curFmt.at(i) == want)
            continue;
        Node *parent = n->parent;
        const int at = parent->indexOf(n);
        auto span = xml::element(QStringLiteral("text:span"));
        span->setAttr(QStringLiteral("text:style-name"), styleFor(want));
        Node *s = parent->insert(at, std::move(span));
        s->append(parent->take(n));
    }
    ++changes;
    return true;
}

bool OdtText::insertParagraphs(int after, int styleLike, const QList<Para> &ps)
{
    if (ps.isEmpty())
        return true;
    Node *like = styleLike >= 0 && styleLike < m_pNodes.size() ? m_pNodes.at(styleLike) : nullptr;
    Node *anchor = after >= 0 && after < m_pNodes.size() ? m_pNodes.at(after) : nullptr;
    Node *parent = anchor ? anchor->parent : (m_pNodes.isEmpty() ? nullptr : m_pNodes.first()->parent);
    if (!parent)
        return false;
    int at = anchor ? parent->indexOf(anchor) + 1 : parent->indexOf(m_pNodes.first());
    const quint8 context = like ? fmtAt(like) : 0;
    for (const Para &para : ps) {
        auto np = xml::element(like && like->name == QLatin1String("text:h") ? QStringLiteral("text:h") : QStringLiteral("text:p"));
        if (like)
            for (const char *a : {"text:style-name", "text:outline-level"})
                if (like->hasAttr(QLatin1String(a)))
                    np->setAttr(QLatin1String(a), like->attr(QLatin1String(a)));
        Node *n = parent->insert(at++, std::move(np));
        for (const auto &seg : segments(para.text, para.fmt))
            appendOdfText(n, -1, seg.first, seg.second, context);
    }
    ++changes;
    return true;
}

bool OdtText::deleteParagraph(int para)
{
    if (para < 0 || para >= m_pNodes.size())
        return false;
    Node *p = m_pNodes.at(para);
    Node *parent = p->parent;
    int siblings = 0;
    for (Node *c : parent->elements())
        if (c->name == QLatin1String("text:p") || c->name == QLatin1String("text:h") || c->name == QLatin1String("text:list"))
            ++siblings;
    if (siblings <= 1 && parent->name == QLatin1String("table:table-cell")) {
        p->children.clear(); // a cell keeps one (empty) paragraph
    } else if (siblings <= 1 && parent->name == QLatin1String("text:list-item") && parent->parent) {
        Node *list = parent->parent;
        list->take(parent);
    } else {
        parent->take(p);
    }
    ++changes;
    return true;
}

bool OdtText::save(const QString &path, QString *error)
{
    return rewriteZip(m_path, path, QStringLiteral("content.xml"), m_doc.serialize(), error);
}

// ---------------------------------------------------------------- other files

bool ReadOnlyText::load(const QString &path, const Formatting &f, QString *error)
{
    kind = QFileInfo(path).suffix().toLower();
    QTextDocument doc;
    const importer::RawReading raw; // the file as it is, whatever File > Options > Sharing says
    if (!importer::readTextFile(path, &doc, f, true, error))
        return false;
    paras = projectParagraphs(&doc, f);
    for (int i = 0; i < paras.size(); ++i)
        paras[i].pos.clear();
    return true;
}

std::unique_ptr<FileText> openFile(const QString &path, const Formatting &f, QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("docx")) {
        auto d = std::make_unique<DocxText>();
        if (d->load(path, error))
            return d;
        return nullptr;
    }
    if (suffix == QLatin1String("odt")) {
        auto o = std::make_unique<OdtText>();
        if (o->load(path, error))
            return o;
        return nullptr;
    }
    auto r = std::make_unique<ReadOnlyText>();
    if (r->load(path, f, error))
        return r;
    return nullptr;
}

} // namespace compare
} // namespace qb
