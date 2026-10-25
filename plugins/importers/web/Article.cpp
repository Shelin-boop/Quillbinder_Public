#include "Article.h"

#include "Html.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using qb::importers::Content;
using qb::importers::Image;
using qb::importers::Paragraph;
using qb::importers::Run;

namespace qbweb {

namespace {

const QRegularExpression &rx(const char *pattern)
{
    // cached per pattern (patterns are string literals)
    static QHash<const char *, QRegularExpression> cache;
    auto it = cache.find(pattern);
    if (it == cache.end())
        it = cache.insert(pattern, QRegularExpression(QString::fromLatin1(pattern), QRegularExpression::CaseInsensitiveOption));
    return it.value();
}

const char *kUnlikely = "-ad-|ai2html|banner|breadcrumbs|combx|comment|community|cover-wrap|disqus|extra|footer|gdpr|"
                        "header|legends|menu|related|remark|replies|rss|shoutbox|sidebar|skyscraper|social|sponsor|"
                        "supplemental|ad-break|agegate|pagination|pager|popup|yom-remote|newsletter|cookie|consent";
const char *kMaybe = "and|article|body|column|content|main|shadow|story";
const char *kPositive = "article|body|content|entry|hentry|h-entry|main|page|pagination|post|text|blog|story";
const char *kNegative = "-ad-|hidden|^hid$| hid$| hid |^hid |banner|combx|comment|com-|contact|footer|gdpr|masthead|"
                        "media|meta|outbrain|promo|related|scroll|share|shoutbox|sidebar|skyscraper|sponsor|shopping|"
                        "tags|widget|newsletter|subscribe|cookie|consent";
const char *kByline = "byline|author|dateline|writtenby|p-author";
const char *kShare = "(\\b|_)(share|sharedaddy)(\\b|_)";

QString norm(const QString &s) { return s.simplified(); }

bool isHeading(const Node *n)
{
    return n->type == Node::Element && n->tag.size() == 2 && n->tag.at(0) == QLatin1Char('h') && n->tag.at(1) >= QLatin1Char('1')
           && n->tag.at(1) <= QLatin1Char('6');
}

bool hasAncestor(const Node *n, const char *tag, int maxDepth = 50)
{
    for (const Node *p = n->parent; p && maxDepth-- > 0; p = p->parent)
        if (p->isElement(tag))
            return true;
    return false;
}

int countChar(const QString &s, QChar c) { return int(s.count(c)); }

class Finder
{
public:
    struct Flags {
        bool stripUnlikely = true, weightClasses = true, cleanConditionally = true;
    };
    Finder(Document &doc, const PageInfo &info, Flags f) : m_doc(doc), m_info(info), m_flags(f) {}
    Node *run();

private:
    double classWeight(const Node *n) const
    {
        if (!m_flags.weightClasses)
            return 0;
        double w = 0;
        for (const QString &v : {n->attr(QStringLiteral("class")), n->attr(QStringLiteral("id"))}) {
            if (v.isEmpty())
                continue;
            if (rx(kNegative).match(v).hasMatch())
                w -= 25;
            if (rx(kPositive).match(v).hasMatch())
                w += 25;
        }
        return w;
    }
    void initialize(Node *n)
    {
        n->scored = true;
        double s = 0;
        const QString &t = n->tag;
        if (t == QLatin1String("div"))
            s = 5;
        else if (t == QLatin1String("pre") || t == QLatin1String("td") || t == QLatin1String("blockquote"))
            s = 3;
        else if (t == QLatin1String("address") || t == QLatin1String("ol") || t == QLatin1String("ul") || t == QLatin1String("dl")
                 || t == QLatin1String("dd") || t == QLatin1String("dt") || t == QLatin1String("li") || t == QLatin1String("form"))
            s = -3;
        else if (isHeading(n) || t == QLatin1String("th"))
            s = -5;
        n->score = s + classWeight(n);
    }
    void cleanConditionally(Node *root, const QString &tag);
    bool isDataTable(Node *t) const;

    Document &m_doc;
    const PageInfo &m_info;
    Flags m_flags;
};

double linkDensity(const Node *n)
{
    const int len = norm(n->textContent()).size();
    if (len == 0)
        return 0;
    double links = 0;
    for (const Node *a : n->elements(QStringLiteral("a"))) {
        const double coef = a->attr(QStringLiteral("href")).startsWith(QLatin1Char('#')) ? 0.3 : 1.0;
        links += norm(a->textContent()).size() * coef;
    }
    return links / len;
}

bool hasBlockChild(const Node *n)
{
    for (const Node *c : n->children)
        if (c->type == Node::Element && (isBlockTag(c->tag) || hasBlockChild(c)))
            return true;
    return false;
}

bool isPhrasing(const Node *n)
{
    if (n->type == Node::Text)
        return true;
    static const QSet<QString> phrasing = {
        QStringLiteral("abbr"), QStringLiteral("audio"), QStringLiteral("b"), QStringLiteral("bdo"), QStringLiteral("br"),
        QStringLiteral("button"), QStringLiteral("cite"), QStringLiteral("code"), QStringLiteral("data"),
        QStringLiteral("datalist"), QStringLiteral("dfn"), QStringLiteral("em"), QStringLiteral("embed"), QStringLiteral("i"),
        QStringLiteral("img"), QStringLiteral("input"), QStringLiteral("kbd"), QStringLiteral("label"), QStringLiteral("mark"),
        QStringLiteral("math"), QStringLiteral("meter"), QStringLiteral("noscript"), QStringLiteral("object"),
        QStringLiteral("output"), QStringLiteral("progress"), QStringLiteral("q"), QStringLiteral("ruby"),
        QStringLiteral("samp"), QStringLiteral("script"), QStringLiteral("select"), QStringLiteral("small"),
        QStringLiteral("span"), QStringLiteral("strong"), QStringLiteral("sub"), QStringLiteral("sup"),
        QStringLiteral("textarea"), QStringLiteral("time"), QStringLiteral("var"), QStringLiteral("wbr"),
        QStringLiteral("u"), QStringLiteral("s"), QStringLiteral("strike"), QStringLiteral("del"), QStringLiteral("ins"),
        QStringLiteral("font"), QStringLiteral("tt"), QStringLiteral("big")};
    if (n->tag == QLatin1String("a") || n->tag == QLatin1String("del") || n->tag == QLatin1String("ins")) {
        for (const Node *c : n->children)
            if (!isPhrasing(c))
                return false;
        return true;
    }
    return phrasing.contains(n->tag);
}

bool Finder::isDataTable(Node *t) const
{
    if (t->attr(QStringLiteral("role")) == QLatin1String("presentation"))
        return false;
    if (t->attr(QStringLiteral("datatable")) == QLatin1String("0"))
        return false;
    if (!t->attr(QStringLiteral("summary")).isEmpty() || t->firstElement(QStringLiteral("caption"))
        || t->firstElement(QStringLiteral("th")) || t->firstElement(QStringLiteral("thead")))
        return true;
    if (t->firstElement(QStringLiteral("table")))
        return false; // tables of tables are layout
    const int rows = t->elements(QStringLiteral("tr")).size();
    int cols = 0;
    for (Node *tr : t->elements(QStringLiteral("tr")))
        cols = qMax(cols, int(tr->elements(QStringLiteral("td")).size()));
    if (rows == 1 || cols == 1)
        return false;
    return rows >= 10 || cols > 4 || rows * cols > 10;
}

void Finder::cleanConditionally(Node *root, const QString &tag)
{
    if (!m_flags.cleanConditionally)
        return;
    QVector<Node *> nodes = root->elements(tag);
    std::reverse(nodes.begin(), nodes.end());
    for (Node *n : nodes) {
        if (!n->parent)
            continue;
        if (tag == QLatin1String("table") && isDataTable(n))
            continue;
        if (hasAncestor(n, "table") && tag != QLatin1String("table")) {
            // inside a data table: keep
            bool data = false;
            for (Node *p = n->parent; p; p = p->parent)
                if (p->isElement("table") && isDataTable(p))
                    data = true;
            if (data)
                continue;
        }
        if (hasAncestor(n, "code") || hasAncestor(n, "pre"))
            continue;
        const double weight = classWeight(n);
        const double score = n->scored ? n->score : 0;
        if (weight + score < 0) {
            Document::detach(n);
            continue;
        }
        const QString text = norm(n->textContent());
        if (countChar(text, QLatin1Char(',')) >= 10)
            continue;
        const int p = n->elements(QStringLiteral("p")).size();
        const int img = n->elements(QStringLiteral("img")).size();
        const int li = n->elements(QStringLiteral("li")).size() - 100;
        const int input = n->elements(QStringLiteral("input")).size();
        int embeds = 0;
        for (const char *e : {"object", "embed", "iframe", "video"})
            embeds += n->elements(QString::fromLatin1(e)).size();
        const double ld = linkDensity(n);
        const int len = text.size();
        bool isList = tag == QLatin1String("ul") || tag == QLatin1String("ol");
        if (!isList && len > 0) {
            int listLen = 0;
            for (const char *l : {"ul", "ol"})
                for (Node *ln : n->elements(QString::fromLatin1(l)))
                    listLen += norm(ln->textContent()).size();
            isList = double(listLen) / len > 0.9;
        }
        const bool inFigure = hasAncestor(n, "figure");
        const bool remove = (img > 1 && double(p) / img < 0.5 && !inFigure) || (!isList && li > p)
                            || (input > p / 3.0 + 0.0001 && input > 0) || (!isList && len < 25 && (img == 0 || img > 2) && !inFigure)
                            || (!isList && weight < 25 && ld > 0.2) || (weight >= 25 && ld > 0.5)
                            || ((embeds == 1 && len < 75) || embeds > 1);
        if (remove)
            Document::detach(n);
    }
}

Node *Finder::run()
{
    Node *body = m_doc.body();
    QVector<Node *> toScore;
    // walk the page in order, removing what cannot be the article
    QVector<Node *> todo{body};
    while (!todo.isEmpty()) {
        Node *n = todo.takeLast();
        if (n->type != Node::Element)
            continue;
        if (n != body) {
            const QString match = n->attr(QStringLiteral("class")) + QLatin1Char(' ') + n->attr(QStringLiteral("id"));
            const QString role = n->attr(QStringLiteral("role"));
            if (m_flags.stripUnlikely && rx(kUnlikely).match(match).hasMatch() && !rx(kMaybe).match(match).hasMatch()
                && n->tag != QLatin1String("a") && n->tag != QLatin1String("body") && !hasAncestor(n, "table")
                && !hasAncestor(n, "code") && role != QLatin1String("main") && role != QLatin1String("article")) {
                Document::detach(n);
                continue;
            }
            static const QSet<QString> badRoles = {QStringLiteral("menu"), QStringLiteral("menubar"), QStringLiteral("complementary"),
                                                   QStringLiteral("navigation"), QStringLiteral("alert"), QStringLiteral("alertdialog"),
                                                   QStringLiteral("dialog")};
            if (m_flags.stripUnlikely && badRoles.contains(role)) {
                Document::detach(n);
                continue;
            }
            // byline: remember and take out (also when the page's data names the author already)
            if (rx(kByline).match(match).hasMatch() || n->attr(QStringLiteral("rel")) == QLatin1String("author")
                || n->attr(QStringLiteral("itemprop")).contains(QLatin1String("author"))) {
                const QString t = norm(n->textContent());
                const bool known = !m_info.byline.isEmpty();
                if (!t.isEmpty() && t.size() < 100 && (!known || t.contains(m_info.byline, Qt::CaseInsensitive))) {
                    if (!known)
                        const_cast<PageInfo &>(m_info).byline = t;
                    Document::detach(n);
                    continue;
                }
            }
            const QString &t = n->tag;
            if ((t == QLatin1String("div") || t == QLatin1String("section") || t == QLatin1String("header") || isHeading(n))
                && norm(n->textContent()).isEmpty() && !n->firstElement(QStringLiteral("img"))
                && !n->firstElement(QStringLiteral("picture"))) {
                Document::detach(n);
                continue;
            }
            static const QSet<QString> scoreTags = {QStringLiteral("section"), QStringLiteral("h2"), QStringLiteral("h3"),
                                                    QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
                                                    QStringLiteral("p"), QStringLiteral("td"), QStringLiteral("pre")};
            if (scoreTags.contains(t))
                toScore << n;
            if (t == QLatin1String("div")) {
                if (!hasBlockChild(n)) {
                    n->tag = QStringLiteral("p");
                    toScore << n;
                } else {
                    // runs of inline content inside a div become paragraphs
                    Node *wrap = nullptr;
                    const QVector<Node *> kids = n->children;
                    for (Node *k : kids) {
                        if (isPhrasing(k)) {
                            if (!wrap) {
                                if (k->type == Node::Text && k->text.trimmed().isEmpty())
                                    continue;
                                wrap = m_doc.create(Node::Element, QStringLiteral("p"));
                                Document::insert(n, k->index(), wrap);
                            }
                            Document::insert(wrap, -1, k);
                        } else {
                            wrap = nullptr;
                        }
                    }
                }
            }
        }
        for (int k = n->children.size() - 1; k >= 0; --k)
            todo << n->children.at(k);
    }
    // points for the paragraphs' ancestors
    QVector<Node *> candidates;
    for (Node *el : toScore) {
        if (!el->parent)
            continue;
        bool attached = false;
        for (Node *p = el; p; p = p->parent)
            if (p == body)
                attached = true;
        if (!attached)
            continue;
        const QString text = norm(el->textContent());
        if (text.size() < 25)
            continue;
        double score = 1 + countChar(text, QLatin1Char(',')) + countChar(text, QChar(0xFF0C)) + qMin(std::floor(text.size() / 100.0), 3.0);
        int level = 0;
        for (Node *a = el->parent; a && level < 5; a = a->parent, ++level) {
            if (a->tag.isEmpty() || a->tag == QLatin1String("html"))
                break;
            if (!a->scored) {
                initialize(a);
                candidates << a;
            }
            const double div = level == 0 ? 1 : level == 1 ? 2 : level * 3;
            a->score += score / div;
            if (a == body)
                break;
        }
    }
    Node *top = nullptr;
    QVector<Node *> tops;
    for (Node *c : candidates) {
        c->score *= (1 - linkDensity(c));
        tops << c;
    }
    std::sort(tops.begin(), tops.end(), [](Node *a, Node *b) { return a->score > b->score; });
    if (tops.size() > 5)
        tops.resize(5);
    if (!tops.isEmpty())
        top = tops.first();
    if (!top || top == body || top->tag == QLatin1String("body")) {
        // everything in one box
        Node *div = m_doc.create(Node::Element, QStringLiteral("div"));
        const QVector<Node *> kids = body->children;
        for (Node *k : kids)
            Document::insert(div, -1, k);
        Document::insert(body, -1, div);
        initialize(div);
        top = div;
    } else {
        // several good candidates in one box: the box
        QVector<Node *> alts;
        for (int i = 1; i < tops.size(); ++i)
            if (tops.at(i)->score / top->score >= 0.75)
                alts << tops.at(i);
        if (alts.size() >= 3) {
            for (Node *p = top->parent; p && p != body && p->tag != QLatin1String("body"); p = p->parent) {
                int in = 0;
                for (Node *a : alts)
                    for (Node *x = a; x; x = x->parent)
                        if (x == p) {
                            ++in;
                            break;
                        }
                if (in >= 3) {
                    top = p;
                    if (!top->scored)
                        initialize(top);
                    break;
                }
            }
        }
        // a parent with more points
        double last = top->score;
        const double threshold = last / 3;
        for (Node *p = top->parent; p && p != body && p->tag != QLatin1String("body"); p = p->parent) {
            if (!p->scored)
                continue;
            if (p->score < threshold)
                break;
            if (p->score > last) {
                top = p;
                break;
            }
            last = p->score;
        }
        // a wrapper with nothing else inside
        while (top->parent && top->parent != body && top->parent->tag != QLatin1String("body")) {
            int elements = 0;
            for (Node *c : top->parent->children)
                if (c->type == Node::Element || !c->text.trimmed().isEmpty())
                    ++elements;
            if (elements != 1)
                break;
            top = top->parent;
            if (!top->scored)
                initialize(top);
        }
    }
    // the article: the best element and the neighbors that belong to it
    Node *article = m_doc.create(Node::Element, QStringLiteral("div"));
    const double threshold = qMax(10.0, top->score * 0.2);
    Node *parent = top->parent;
    const QVector<Node *> siblings = parent ? parent->children : QVector<Node *>{top};
    const QString topClass = top->attr(QStringLiteral("class"));
    for (Node *s : siblings) {
        bool append = s == top;
        if (!append && s->type == Node::Element) {
            double bonus = 0;
            if (!topClass.isEmpty() && s->attr(QStringLiteral("class")) == topClass)
                bonus += top->score * 0.2;
            if (s->scored && s->score + bonus >= threshold) {
                append = true;
            } else if (s->tag == QLatin1String("p")) {
                const double ld = linkDensity(s);
                const QString t = norm(s->textContent());
                if (t.size() > 80 && ld < 0.25)
                    append = true;
                else if (t.size() < 80 && t.size() > 0 && ld == 0 && t.contains(QRegularExpression(QStringLiteral("\\.( |$)"))))
                    append = true;
            }
        }
        if (append)
            Document::insert(article, -1, s);
    }
    // clean the article
    for (const char *t : {"form", "fieldset", "object", "embed", "iframe", "input", "textarea", "select", "button",
                          "footer", "aside", "nav", "link", "svg", "canvas", "audio", "video"})
        for (Node *n : article->elements(QString::fromLatin1(t)))
            if (QString::fromLatin1(t) != QLatin1String("form") || !m_flags.cleanConditionally || norm(n->textContent()).size() < 200)
                Document::detach(n);
    QVector<Node *> all;
    article->forEach([&all, article](Node *n) {
        if (n != article && n->type == Node::Element)
            all << n;
    });
    for (Node *n : all)
        if (n->parent && rx(kShare).match(n->attr(QStringLiteral("class")) + QLatin1Char(' ') + n->attr(QStringLiteral("id"))).hasMatch()
            && norm(n->textContent()).size() < 500)
            Document::detach(n);
    for (const char *t : {"table", "ul", "div", "section"})
        cleanConditionally(article, QString::fromLatin1(t));
    // headings with negative weight, empty paragraphs
    for (Node *h : article->elements(QStringLiteral("h1")) + article->elements(QStringLiteral("h2")))
        if (classWeight(h) < 0)
            Document::detach(h);
    for (Node *p : article->elements(QStringLiteral("p")))
        if (norm(p->textContent()).isEmpty() && !p->firstElement(QStringLiteral("img")) && !p->firstElement(QStringLiteral("picture")))
            Document::detach(p);
    return article;
}

// ------------------------------------------------------------------ metadata

QString jsonName(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isArray()) {
        QStringList names;
        for (const QJsonValue &e : v.toArray()) {
            const QString n = jsonName(e);
            if (!n.isEmpty())
                names << n;
        }
        return names.join(QStringLiteral(", "));
    }
    if (v.isObject())
        return v.toObject().value(QStringLiteral("name")).toString();
    return QString();
}

// Share of the words of a that are in b.
double similarity(const QString &a, const QString &b)
{
    const QStringList wa = a.toLower().split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
    const QStringList wb = b.toLower().split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
    if (wa.isEmpty())
        return 0;
    int in = 0;
    for (const QString &w : wa)
        if (wb.contains(w))
            ++in;
    return double(in) / wa.size();
}

void readJsonLd(Node *root, PageInfo *info, QString *articleBody, const QString &htmlTitle)
{
    static const QSet<QString> types = {QStringLiteral("Article"), QStringLiteral("NewsArticle"), QStringLiteral("BlogPosting"),
                                        QStringLiteral("Report"), QStringLiteral("ScholarlyArticle"), QStringLiteral("TechArticle"),
                                        QStringLiteral("AnalysisNewsArticle"), QStringLiteral("OpinionNewsArticle"),
                                        QStringLiteral("ReportageNewsArticle"), QStringLiteral("ReviewNewsArticle"),
                                        QStringLiteral("SocialMediaPosting"), QStringLiteral("LiveBlogPosting"),
                                        QStringLiteral("WebPage"), QStringLiteral("Recipe")};
    std::function<void(const QJsonValue &)> visit = [&](const QJsonValue &v) {
        if (v.isArray()) {
            for (const QJsonValue &e : v.toArray())
                visit(e);
            return;
        }
        if (!v.isObject())
            return;
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("@graph")))
            visit(o.value(QStringLiteral("@graph")));
        QStringList t;
        const QJsonValue tv = o.value(QStringLiteral("@type"));
        if (tv.isArray())
            for (const QJsonValue &e : tv.toArray())
                t << e.toString();
        else
            t << tv.toString();
        bool match = false;
        for (const QString &x : t)
            match = match || types.contains(x);
        if (!match)
            return;
        const bool page = t.contains(QStringLiteral("WebPage")) && t.size() == 1;
        auto take = [](QString *dst, const QString &value) {
            if (dst->isEmpty() && !value.trimmed().isEmpty())
                *dst = value.trimmed();
        };
        if (!page) {
            // name and headline should be the same; some sites put the site's name or a description in one of them
            const QString name = o.value(QStringLiteral("name")).toString().trimmed();
            const QString headline = o.value(QStringLiteral("headline")).toString().trimmed();
            if (!name.isEmpty() && !headline.isEmpty() && name != headline)
                take(&info->title, similarity(headline, htmlTitle) > similarity(name, htmlTitle) ? headline : name);
            take(&info->title, headline);
            take(&info->title, name);
            take(&info->byline, jsonName(o.value(QStringLiteral("author"))));
            take(&info->published, o.value(QStringLiteral("datePublished")).toString());
            take(articleBody, o.value(QStringLiteral("articleBody")).toString());
        }
        take(&info->siteName, jsonName(o.value(QStringLiteral("publisher"))));
        take(&info->excerpt, o.value(QStringLiteral("description")).toString());
    };
    for (Node *s : root->elements(QStringLiteral("script"))) {
        if (!s->attr(QStringLiteral("type")).contains(QLatin1String("ld+json"), Qt::CaseInsensitive))
            continue;
        QString j = s->textContent().trimmed();
        if (j.startsWith(QLatin1String("<![CDATA[")))
            j = j.mid(9, j.size() - 12);
        QJsonParseError err;
        const QJsonDocument d = QJsonDocument::fromJson(j.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError)
            visit(d.isArray() ? QJsonValue(d.array()) : QJsonValue(d.object()));
    }
}

PageInfo readMeta(Node *root, QString *articleBody)
{
    PageInfo info;
    Node *titleEl = root->firstElement(QStringLiteral("title"));
    readJsonLd(root, &info, articleBody, titleEl ? norm(titleEl->textContent()) : QString());
    QHash<QString, QString> meta;
    for (Node *m : root->elements(QStringLiteral("meta"))) {
        const QString key = (m->attr(QStringLiteral("property")) + QLatin1Char(' ') + m->attr(QStringLiteral("name"))
                             + QLatin1Char(' ') + m->attr(QStringLiteral("itemprop")))
                                .trimmed()
                                .toLower();
        const QString content = m->attr(QStringLiteral("content")).trimmed();
        if (content.isEmpty())
            continue;
        for (const QString &k : key.split(QLatin1Char(' '), Qt::SkipEmptyParts))
            if (!meta.contains(k))
                meta.insert(k, content);
    }
    auto first = [&meta](std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            const QString v = meta.value(QString::fromLatin1(k));
            if (!v.isEmpty())
                return v;
        }
        return QString();
    };
    const QString ogTitle = first({"og:title", "twitter:title", "dc.title", "dcterm:title", "parsely-title", "weibo:article:title"});
    if (!ogTitle.isEmpty())
        info.title = ogTitle;
    if (info.siteName.isEmpty())
        info.siteName = first({"og:site_name", "application-name"});
    if (info.byline.isEmpty()) {
        const QString a = first({"author", "dc.creator", "parsely-author", "article:author", "byl", "sailthru.author"});
        if (!a.startsWith(QLatin1String("http")))
            info.byline = a;
    }
    if (info.published.isEmpty())
        info.published = first({"article:published_time", "og:article:published_time", "datepublished", "date", "dc.date",
                                "dcterm:created", "parsely-pub-date", "sailthru.date", "publishdate"});
    const QString desc = first({"og:description", "description", "twitter:description", "dc.description", "dcterm:description"});
    if (!desc.isEmpty())
        info.excerpt = desc;
    // <title>, with the site's name taken off
    if (info.title.isEmpty()) {
        Node *t = root->firstElement(QStringLiteral("title"));
        QString title = t ? norm(t->textContent()) : QString();
        static const QRegularExpression sep(QStringLiteral("\\s+[\\|\\-–—»:/·]\\s+"));
        const QStringList parts = title.split(sep);
        if (parts.size() > 1) {
            if (!info.siteName.isEmpty() && norm(parts.last()).compare(info.siteName, Qt::CaseInsensitive) == 0)
                title = title.left(title.size() - parts.last().size()).remove(QRegularExpression(QStringLiteral("\\s+[\\|\\-–—»:/·]\\s+$")));
            else if (parts.first().split(QLatin1Char(' ')).size() >= 3)
                title = parts.first();
        }
        info.title = norm(title);
    }
    if (info.title.isEmpty())
        if (Node *h1 = root->firstElement(QStringLiteral("h1")))
            info.title = norm(h1->textContent());
    if (info.published.isEmpty())
        if (Node *t = root->firstElement(QStringLiteral("time")))
            info.published = t->attr(QStringLiteral("datetime")).isEmpty() ? norm(t->textContent()) : t->attr(QStringLiteral("datetime"));
    if (Node *html = root->firstElement(QStringLiteral("html")))
        info.lang = html->attr(QStringLiteral("lang"));
    info.title = info.title.simplified();
    info.excerpt = info.excerpt.simplified();
    return info;
}

// ------------------------------------------------------------------ preparing the page

void prepare(Document &doc, bool articleOnly)
{
    Node *root = doc.root();
    for (const char *t : {"script", "style", "template", "iframe", "object", "embed", "svg", "canvas", "link", "meta",
                          "head", "input", "select", "textarea", "button", "noembed", "noframes", "dialog"})
        for (Node *n : root->elements(QString::fromLatin1(t)))
            Document::detach(n);
    if (articleOnly)
        for (const char *t : {"nav", "aside", "footer", "form"})
            for (Node *n : root->elements(QString::fromLatin1(t)))
                if (QString::fromLatin1(t) != QLatin1String("form") || norm(n->textContent()).size() < 300)
                    Document::detach(n);
    // hidden parts
    root->forEach([](Node *n) {
        if (n->type != Node::Element || !n->parent)
            return;
        const QString style = n->attr(QStringLiteral("style")).toLower().remove(QLatin1Char(' '));
        if (n->hasAttr(QStringLiteral("hidden")) || style.contains(QLatin1String("display:none"))
            || style.contains(QLatin1String("visibility:hidden"))
            || (n->attr(QStringLiteral("aria-hidden")) == QLatin1String("true") && !n->firstElement(QStringLiteral("img"))
                && !n->isElement("img")))
            Document::detach(n);
    });
    // pictures that load late: their real address is in another attribute
    for (Node *img : root->elements(QStringLiteral("img")) + root->elements(QStringLiteral("source"))) {
        for (const char *a : {"data-src", "data-original", "data-lazy-src", "data-url", "data-hi-res-src", "data-full-src",
                              "data-lazy", "data-echo", "data-orig-file"}) {
            const QString v = img->attr(QString::fromLatin1(a));
            if (!v.isEmpty() && !v.startsWith(QLatin1String("data:"))) {
                img->setAttr(QStringLiteral("src"), v);
                break;
            }
        }
        for (const char *a : {"data-srcset", "data-lazy-srcset"}) {
            const QString v = img->attr(QString::fromLatin1(a));
            if (!v.isEmpty()) {
                img->setAttr(QStringLiteral("srcset"), v);
                break;
            }
        }
    }
    // <noscript> holds the real picture of a lazy one: use it
    for (Node *ns : root->elements(QStringLiteral("noscript"))) {
        if (!ns->parent)
            continue;
        Node *inner = ns->firstElement(QStringLiteral("img"));
        const int idx = ns->index();
        Node *prev = nullptr;
        for (int k = idx - 1; k >= 0; --k) {
            Node *c = ns->parent->children.at(k);
            if (c->type == Node::Text && c->text.trimmed().isEmpty())
                continue;
            prev = c;
            break;
        }
        Node *prevImg = prev ? (prev->isElement("img") ? prev : prev->firstElement(QStringLiteral("img"))) : nullptr;
        if (inner && prevImg && norm(ns->textContent()).isEmpty()) {
            // the picture of <noscript> takes the place of the placeholder
            Node *at = prevImg->parent;
            const int i = prevImg->index();
            Document::detach(prevImg);
            Document::insert(at, i, inner);
        }
        // what a browser without scripts shows ("turn on JavaScript", cookie questions) does not belong to the page
        Document::detach(ns);
    }
    // two <br> in a row start a new paragraph
    for (Node *br : root->elements(QStringLiteral("br"))) {
        if (!br->parent)
            continue;
        Node *p = br->parent;
        int i = br->index() + 1;
        while (i < p->children.size() && p->children.at(i)->type == Node::Text && p->children.at(i)->text.trimmed().isEmpty())
            ++i;
        if (i >= p->children.size() || !p->children.at(i)->isElement("br"))
            continue;
        if (p->isElement("p") || p->isElement("pre"))
            continue;
        // remove the following <br>s, collect what comes after into a <p>
        while (i < p->children.size() && (p->children.at(i)->isElement("br")
                                          || (p->children.at(i)->type == Node::Text && p->children.at(i)->text.trimmed().isEmpty())))
            Document::detach(p->children.at(i));
        Node *np = doc.create(Node::Element, QStringLiteral("p"));
        const int at = br->index();
        Document::detach(br);
        while (at < p->children.size()) {
            Node *next = p->children.at(at);
            if (next->isElement("br")) {
                // stop at the next double break
                int k = at + 1;
                while (k < p->children.size() && p->children.at(k)->type == Node::Text && p->children.at(k)->text.trimmed().isEmpty())
                    ++k;
                if (k < p->children.size() && p->children.at(k)->isElement("br"))
                    break;
            }
            if (next->type == Node::Element && isBlockTag(next->tag))
                break;
            Document::insert(np, -1, next);
        }
        Document::insert(p, at, np);
    }
    for (Node *f : root->elements(QStringLiteral("font")))
        f->tag = QStringLiteral("span");
}

// ------------------------------------------------------------------ to content

struct Builder {
    Content c;
    QVector<ImageRef> images;
    QUrl base;
    bool pictures = true;
    Paragraph cur;
    bool open = false;
    struct Fmt {
        bool bold = false, italic = false, underline = false, strike = false, code = false, mark = false;
        int vertical = 0;
        QString link;
    };
    QVector<Fmt> fmt{Fmt()};
    QString blockStyle;                 // style of paragraphs in the current block
    int headingLevel = 0;
    int pre = 0;
    struct List {
        bool numbered;
        int id;
    };
    QVector<List> lists;
    int nextListId = 1;
    int cell = 0;                       // inside a table cell
    bool rowOpen = false;
    QHash<QString, int> imageByUrl;
    int lastImage = -1;
    QString lastImageFile;
    int textSinceImage = 0;

    void begin()
    {
        if (open)
            return;
        cur = Paragraph();
        cur.style = blockStyle;
        cur.headingLevel = headingLevel;
        if (!lists.isEmpty() && !rowOpen) {
            cur.listLevel = lists.size() - 1;
            cur.numbered = lists.last().numbered;
            cur.listId = lists.first().id;
        }
        open = true;
    }
    QString lastText() const
    {
        for (int i = cur.runs.size() - 1; i >= 0; --i)
            if (cur.runs.at(i).image < 0)
                return cur.runs.at(i).text;
        return QString();
    }
    void end()
    {
        if (!open)
            return;
        open = false;
        if (rowOpen)
            return; // a table row ends at </tr>
        // trailing spaces and line breaks
        while (!cur.runs.isEmpty() && cur.runs.last().image < 0) {
            Run &r = cur.runs.last();
            while (!r.text.isEmpty() && (r.text.endsWith(QLatin1Char(' ')) || r.text.endsWith(QChar(QChar::LineSeparator))))
                r.text.chop(1);
            if (r.text.isEmpty())
                cur.runs.removeLast();
            else
                break;
        }
        if (cur.runs.isEmpty())
            return;
        c.paragraphs << cur;
    }
    void text(const QString &raw)
    {
        QString t = raw;
        if (!pre) {
            t.replace(QRegularExpression(QStringLiteral("[ \\t\\r\\n\\f]+")), QStringLiteral(" "));
            if (t == QLatin1String(" ") && !open)
                return;
        }
        if (t.isEmpty())
            return;
        begin();
        if (!pre) {
            const QString last = lastText();
            const bool atStart = cur.runs.isEmpty() || last.isEmpty() || last.endsWith(QLatin1Char(' '))
                                 || last.endsWith(QChar(QChar::LineSeparator)) || last.endsWith(QLatin1Char('\t'));
            if (atStart && t.startsWith(QLatin1Char(' ')))
                t.remove(0, 1);
            if (t.isEmpty())
                return;
        }
        if (!t.trimmed().isEmpty())
            ++textSinceImage;
        const Fmt &f = fmt.last();
        Run r;
        r.text = t;
        r.bold = f.bold;
        r.italic = f.italic;
        r.underline = f.underline;
        r.strike = f.strike;
        r.code = f.code && !pre;
        r.vertical = f.vertical;
        r.link = f.link;
        if (f.mark)
            r.highlight = QColor(255, 240, 120);
        if (!cur.runs.isEmpty()) {
            Run &l = cur.runs.last();
            if (l.image < 0 && l.bold == r.bold && l.italic == r.italic && l.underline == r.underline && l.strike == r.strike
                && l.code == r.code && l.vertical == r.vertical && l.link == r.link && l.highlight == r.highlight) {
                l.text += r.text;
                return;
            }
        }
        cur.runs << r;
    }
    void lineBreak()
    {
        if (!open)
            return;
        Run r;
        r.text = QString(QChar(QChar::LineSeparator));
        cur.runs << r;
    }
    QUrl resolve(const QString &href) const
    {
        const QString h = href.trimmed();
        if (h.isEmpty())
            return QUrl();
        return base.resolved(QUrl(h));
    }
    QString pickSrc(Node *img) const
    {
        // the best candidate of a srcset: the widest up to 1600 pixels
        auto fromSet = [](const QString &set) {
            QString best;
            double bestW = -1;
            QString smallest;
            double smallestW = 1e9;
            for (const QString &cand : set.split(QRegularExpression(QStringLiteral(",\\s+")), Qt::SkipEmptyParts)) {
                const QStringList parts = cand.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.isEmpty())
                    continue;
                double w = 1;
                if (parts.size() > 1) {
                    QString d = parts.at(1);
                    if (d.endsWith(QLatin1Char('w')))
                        w = d.chopped(1).toDouble();
                    else if (d.endsWith(QLatin1Char('x')))
                        w = d.chopped(1).toDouble() * 800;
                }
                if (w <= 1600 && w > bestW) {
                    bestW = w;
                    best = parts.first();
                }
                if (w < smallestW) {
                    smallestW = w;
                    smallest = parts.first();
                }
            }
            return best.isEmpty() ? smallest : best;
        };
        // <picture>: a <source> in a format the program reads
        if (img->parent && img->parent->isElement("picture")) {
            for (Node *s : img->parent->children) {
                if (!s->isElement("source"))
                    continue;
                const QString type = s->attr(QStringLiteral("type")).toLower();
                if (!type.isEmpty() && !type.contains(QLatin1String("jpeg")) && !type.contains(QLatin1String("png"))
                    && !type.contains(QLatin1String("webp")) && !type.contains(QLatin1String("gif")))
                    continue;
                const QString src = fromSet(s->attr(QStringLiteral("srcset")));
                if (!src.isEmpty())
                    return src;
            }
        }
        const QString set = img->attr(QStringLiteral("srcset"));
        if (!set.isEmpty()) {
            const QString src = fromSet(set);
            if (!src.isEmpty())
                return src;
        }
        return img->attr(QStringLiteral("src"));
    }
    void image(Node *img)
    {
        if (!pictures)
            return;
        const QString w = img->attr(QStringLiteral("width")), h = img->attr(QStringLiteral("height"));
        if ((!w.isEmpty() && w.toInt() > 0 && w.toInt() <= 4) || (!h.isEmpty() && h.toInt() > 0 && h.toInt() <= 4))
            return; // a counting pixel
        const QString src = pickSrc(img).trimmed();
        if (src.isEmpty())
            return;
        ImageRef ref;
        ref.alt = norm(img->attr(QStringLiteral("alt")));
        if (src.startsWith(QLatin1String("data:"))) {
            const int comma = src.indexOf(QLatin1Char(','));
            if (comma < 0 || src.size() < 300)
                return; // a placeholder
            const QString headerPart = src.left(comma);
            ref.data = headerPart.contains(QLatin1String(";base64")) ? QByteArray::fromBase64(src.mid(comma + 1).toLatin1())
                                                                      : QByteArray::fromPercentEncoding(src.mid(comma + 1).toUtf8());
        } else {
            ref.url = resolve(src);
            if (!ref.url.isValid() || (ref.url.scheme() != QLatin1String("http") && ref.url.scheme() != QLatin1String("https")))
                return;
        }
        // a small placeholder and the real picture one after the other (same file name): keep the real one
        const QString fileName = ref.url.isValid() ? ref.url.fileName() : QString();
        if (!fileName.isEmpty() && fileName == lastImageFile && lastImage >= 0 && lastImage < images.size()
            && textSinceImage == 0) {
            images[lastImage].url = ref.url;
            if (!ref.alt.isEmpty())
                c.images[lastImage].alt = ref.alt;
            return;
        }
        int idx = -1;
        const QString key = ref.url.isValid() ? ref.url.toString() : QString::number(qHash(ref.data));
        if (imageByUrl.contains(key)) {
            idx = imageByUrl.value(key);
        } else {
            Image im;
            im.alt = ref.alt;
            bool okW = false, okH = false;
            const double pw = w.toDouble(&okW), ph = h.toDouble(&okH);
            if (okW && pw > 0)
                im.width = pw * 0.75; // CSS pixels -> points
            if (okH && ph > 0 && okW && pw > 0)
                im.height = ph * 0.75;
            c.images << im;
            images << ref;
            idx = c.images.size() - 1;
            imageByUrl.insert(key, idx);
        }
        begin();
        Run r;
        r.image = idx;
        cur.runs << r;
        lastImage = idx;
        lastImageFile = fileName;
        textSinceImage = 0;
    }
    void walk(Node *n)
    {
        if (n->type == Node::Text) {
            text(n->text);
            return;
        }
        const QString &t = n->tag;
        if (t == QLatin1String("head") || t == QLatin1String("title") || t == QLatin1String("script") || t == QLatin1String("style"))
            return;
        if (t == QLatin1String("br")) {
            if (open && !rowOpen)
                lineBreak();
            else if (rowOpen)
                text(QStringLiteral(" "));
            return;
        }
        if (t == QLatin1String("img")) {
            image(n);
            return;
        }
        if (t == QLatin1String("hr")) {
            end();
            Paragraph p;
            p.align = 1;
            Run r;
            r.text = QStringLiteral("* * *");
            p.runs << r;
            c.paragraphs << p;
            return;
        }
        // tables: a paragraph per row
        if (t == QLatin1String("tr")) {
            end();
            rowOpen = true;
            cur = Paragraph();
            cur.tableRow = true;
            open = true;
            cell = 0;
            for (Node *k : n->children)
                walk(k);
            rowOpen = false;
            open = true;
            end();
            return;
        }
        if ((t == QLatin1String("td") || t == QLatin1String("th")) && rowOpen) {
            if (cell++ > 0) {
                Run r;
                r.text = QStringLiteral("\t");
                cur.runs << r;
            }
            Fmt f = fmt.last();
            if (t == QLatin1String("th"))
                f.bold = true;
            fmt << f;
            for (Node *k : n->children)
                walk(k);
            fmt.removeLast();
            return;
        }
        Fmt f = fmt.last();
        bool pushed = false;
        auto push = [&] {
            fmt << f;
            pushed = true;
        };
        if (t == QLatin1String("b") || t == QLatin1String("strong")) {
            f.bold = true;
            push();
        } else if (t == QLatin1String("i") || t == QLatin1String("em") || t == QLatin1String("cite") || t == QLatin1String("dfn")
                   || t == QLatin1String("var")) {
            f.italic = true;
            push();
        } else if (t == QLatin1String("u") || t == QLatin1String("ins")) {
            f.underline = true;
            push();
        } else if (t == QLatin1String("s") || t == QLatin1String("strike") || t == QLatin1String("del")) {
            f.strike = true;
            push();
        } else if (t == QLatin1String("sup")) {
            f.vertical = 1;
            push();
        } else if (t == QLatin1String("sub")) {
            f.vertical = -1;
            push();
        } else if (t == QLatin1String("code") || t == QLatin1String("kbd") || t == QLatin1String("samp") || t == QLatin1String("tt")) {
            f.code = true;
            push();
        } else if (t == QLatin1String("mark")) {
            f.mark = true;
            push();
        } else if (t == QLatin1String("a")) {
            const QString href = n->attr(QStringLiteral("href")).trimmed();
            if (!href.isEmpty() && !href.startsWith(QLatin1Char('#')) && !href.startsWith(QLatin1String("javascript:"), Qt::CaseInsensitive)) {
                f.link = resolve(href).toString();
                push();
            }
        } else if (t == QLatin1String("span") || t == QLatin1String("font")) {
            const QString st = n->attr(QStringLiteral("style")).toLower().remove(QLatin1Char(' '));
            if (st.contains(QLatin1String("font-weight:bold")) || st.contains(QLatin1String("font-weight:700"))
                || st.contains(QLatin1String("font-weight:600")) || st.contains(QLatin1String("font-weight:800")))
                f.bold = true;
            if (st.contains(QLatin1String("font-style:italic")))
                f.italic = true;
            if (st.contains(QLatin1String("text-decoration:underline")))
                f.underline = true;
            if (st.contains(QLatin1String("text-decoration:line-through")))
                f.strike = true;
            push();
        }
        if (pushed) {
            for (Node *k : n->children)
                walk(k);
            fmt.removeLast();
            return;
        }
        // blocks
        const bool block = isBlockTag(t) && !rowOpen;
        QString savedStyle = blockStyle;
        int savedHeading = headingLevel;
        bool list = false;
        if (block)
            end();
        if (isHeading(n) && !rowOpen) {
            headingLevel = t.at(1).digitValue();
            blockStyle.clear();
        } else if (t == QLatin1String("blockquote")) {
            blockStyle = QStringLiteral("Block Quote");
        } else if (t == QLatin1String("pre")) {
            blockStyle = QStringLiteral("Code Block");
            ++pre;
        } else if (t == QLatin1String("figcaption") || t == QLatin1String("caption")) {
            blockStyle = QStringLiteral("Caption");
        } else if ((t == QLatin1String("ul") || t == QLatin1String("ol") || t == QLatin1String("menu")) && !rowOpen) {
            lists << List{t == QLatin1String("ol"), lists.isEmpty() ? nextListId++ : lists.first().id};
            list = true;
        } else if (t == QLatin1String("dt")) {
            f.bold = true;
            fmt << f;
            pushed = true;
        }
        if (t == QLatin1String("pre")) {
            // a paragraph per line
            const QString all = n->textContent();
            const QStringList lines = QString(all).replace(QLatin1String("\r\n"), QLatin1String("\n")).split(QLatin1Char('\n'));
            int first = 0, last = lines.size() - 1;
            while (first <= last && lines.at(first).trimmed().isEmpty())
                ++first;
            while (last >= first && lines.at(last).trimmed().isEmpty())
                --last;
            for (int i = first; i <= last; ++i) {
                Paragraph p;
                p.style = QStringLiteral("Code Block");
                Run r;
                r.text = lines.at(i);
                p.runs << r;
                if (r.text.isEmpty())
                    p.runs.clear(), r.text = QStringLiteral(" "), p.runs << r;
                c.paragraphs << p;
            }
        } else {
            for (Node *k : n->children)
                walk(k);
        }
        if (block)
            end();
        if (t == QLatin1String("pre"))
            --pre;
        if (list)
            lists.removeLast();
        if (pushed)
            fmt.removeLast();
        blockStyle = savedStyle;
        headingLevel = savedHeading;
    }
};

QString plainOf(const Paragraph &p)
{
    QString t;
    for (const Run &r : p.runs)
        if (r.image < 0 && r.note < 0)
            t += r.text;
    return t.simplified();
}

} // namespace

int countWords(const Content &c)
{
    int n = 0;
    static const QRegularExpression w(QStringLiteral("\\w+"), QRegularExpression::UseUnicodePropertiesOption);
    for (const Paragraph &p : c.paragraphs)
        for (const Run &r : p.runs)
            if (r.image < 0)
                for (auto it = w.globalMatch(r.text); it.hasNext(); it.next())
                    ++n;
    return n;
}

Extracted extract(const QString &html, const QUrl &pageUrl, const ExtractOptions &o)
{
    Extracted out;
    QUrl base = pageUrl;
    QString articleBody;
    {
        Document meta;
        meta.parse(html);
        out.info = readMeta(meta.root(), &articleBody);
        if (Node *b = meta.root()->firstElement(QStringLiteral("base"))) {
            const QString href = b->attr(QStringLiteral("href"));
            if (!href.isEmpty())
                base = pageUrl.resolved(QUrl(href));
        }
    }
    auto build = [&](Node *root, bool includeRoot) {
        Builder b;
        b.base = base;
        b.pictures = o.pictures;
        if (includeRoot)
            b.walk(root);
        else
            for (Node *k : root->children)
                b.walk(k);
        b.end();
        return b;
    };
    Builder best;
    int bestLen = -1;
    if (o.articleOnly) {
        // stricter first, then less strict while too little text is found
        const QList<Finder::Flags> attempts = {{true, true, true}, {false, true, true}, {false, false, true}, {false, false, false}};
        for (const Finder::Flags &f : attempts) {
            Document doc;
            doc.parse(html);
            prepare(doc, true);
            PageInfo info = out.info;
            Finder finder(doc, info, f);
            Node *article = finder.run();
            Builder b = build(article, false);
            int len = 0;
            for (const Paragraph &p : b.c.paragraphs)
                len += plainOf(p).size();
            if (out.info.byline.isEmpty() && !info.byline.isEmpty())
                out.info.byline = info.byline;
            if (len > bestLen) {
                best = b;
                bestLen = len;
            }
            if (len >= 500)
                break;
        }
        // pages built by scripts: the text of their JSON-LD
        if (bestLen < 200 && articleBody.size() > qMax(bestLen, 200)) {
            Builder b;
            for (const QString &para : articleBody.split(QRegularExpression(QStringLiteral("\\n\\s*\\n|\\n")), Qt::SkipEmptyParts)) {
                Paragraph p;
                Run r;
                r.text = para.simplified();
                p.runs << r;
                if (!r.text.isEmpty())
                    b.c.paragraphs << p;
            }
            best = b;
            out.fromScriptData = true;
        }
    } else {
        Document doc;
        doc.parse(html);
        prepare(doc, false);
        best = build(doc.body(), false);
    }
    out.content = best.c;
    out.images = best.images;
    // the title: the first heading if it says the same, else a Title paragraph
    QList<Paragraph> &ps = out.content.paragraphs;
    const QString title = out.info.title;
    bool titled = false;
    for (int i = 0; i < ps.size() && i < 3; ++i) {
        if (ps.at(i).headingLevel > 0 && plainOf(ps.at(i)).compare(title, Qt::CaseInsensitive) == 0) {
            ps[i].headingLevel = 0;
            ps[i].style = QStringLiteral("Title");
            // what stood before it (a category line, a date) stays below
            if (i > 0)
                ps.move(i, 0);
            titled = true;
            break;
        }
    }
    if (!titled && o.addTitle && !title.isEmpty()) {
        Paragraph p;
        p.style = QStringLiteral("Title");
        Run r;
        r.text = title;
        p.runs << r;
        ps.prepend(p);
    }
    // the levels of headings: the largest one below the title becomes Heading 1
    int minLevel = 9;
    for (const Paragraph &p : ps)
        if (p.headingLevel > 0)
            minLevel = qMin(minLevel, p.headingLevel);
    if (minLevel < 9 && minLevel > 1)
        for (Paragraph &p : ps)
            if (p.headingLevel > 0)
                p.headingLevel = qMax(1, p.headingLevel - minLevel + 1);
    out.content.title = title;
    out.words = countWords(out.content);
    return out;
}

// ------------------------------------------------------------------ HTML out

QString toHtml(const Content &c, const QStringList &imageNames, const QString &title, const QString &sourceLine)
{
    auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
    QString out = QStringLiteral("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>%1</title></head><body>\n").arg(esc(title));
    if (!sourceLine.isEmpty())
        out += QStringLiteral("<p><small>%1</small></p>\n").arg(sourceLine);
    QVector<QPair<bool, int>> openLists; // numbered, level
    bool inTable = false;
    auto closeLists = [&](int toLevel) {
        while (!openLists.isEmpty() && openLists.last().second >= toLevel) {
            out += openLists.last().first ? QStringLiteral("</ol>\n") : QStringLiteral("</ul>\n");
            openLists.removeLast();
        }
    };
    for (const Paragraph &p : c.paragraphs) {
        QString inner;
        for (const Run &r : p.runs) {
            if (r.image >= 0) {
                if (r.image < imageNames.size() && !imageNames.at(r.image).isEmpty()) {
                    const Image &im = c.images.value(r.image);
                    QString size;
                    if (im.width > 0)
                        size = QStringLiteral(" width=\"%1\"").arg(qRound(qMin(im.width / 0.75, 640.0)));
                    else
                        size = QStringLiteral(" width=\"640\"");
                    inner += QStringLiteral("<img src=\"%1\" alt=\"%2\"%3>").arg(esc(imageNames.at(r.image)), esc(im.alt), size);
                }
                continue;
            }
            QString t = esc(r.text);
            t.replace(QChar(QChar::LineSeparator), QStringLiteral("<br>"));
            if (p.tableRow)
                t.replace(QLatin1Char('\t'), QStringLiteral("</td><td>"));
            if (r.code)
                t = QStringLiteral("<code>") + t + QStringLiteral("</code>");
            if (r.bold)
                t = QStringLiteral("<b>") + t + QStringLiteral("</b>");
            if (r.italic)
                t = QStringLiteral("<i>") + t + QStringLiteral("</i>");
            if (r.underline)
                t = QStringLiteral("<u>") + t + QStringLiteral("</u>");
            if (r.strike)
                t = QStringLiteral("<s>") + t + QStringLiteral("</s>");
            if (r.vertical > 0)
                t = QStringLiteral("<sup>") + t + QStringLiteral("</sup>");
            else if (r.vertical < 0)
                t = QStringLiteral("<sub>") + t + QStringLiteral("</sub>");
            if (r.highlight.isValid())
                t = QStringLiteral("<span style=\"background-color:%1\">%2</span>").arg(r.highlight.name(), t);
            if (!r.link.isEmpty())
                t = QStringLiteral("<a href=\"%1\">%2</a>").arg(esc(r.link), t);
            inner += t;
        }
        if (p.tableRow) {
            closeLists(0);
            if (!inTable)
                out += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n");
            inTable = true;
            out += QStringLiteral("<tr><td>%1</td></tr>\n").arg(inner);
            continue;
        }
        if (inTable) {
            out += QStringLiteral("</table>\n");
            inTable = false;
        }
        if (p.listLevel >= 0) {
            closeLists(p.listLevel + 1);
            if (!openLists.isEmpty() && openLists.last().second == p.listLevel && openLists.last().first != p.numbered)
                closeLists(p.listLevel);
            while (openLists.isEmpty() || openLists.last().second < p.listLevel) {
                const int level = openLists.isEmpty() ? 0 : openLists.last().second + 1;
                if (level > p.listLevel)
                    break;
                out += p.numbered ? QStringLiteral("<ol>\n") : QStringLiteral("<ul>\n");
                openLists << qMakePair(p.numbered, level);
                if (level == p.listLevel)
                    break;
            }
            out += QStringLiteral("<li>%1</li>\n").arg(inner);
            continue;
        }
        closeLists(0);
        const QString align = p.align == 1 ? QStringLiteral(" align=\"center\"") : p.align == 2 ? QStringLiteral(" align=\"right\"") : QString();
        if (p.style == QLatin1String("Title"))
            out += QStringLiteral("<h1>%1</h1>\n").arg(inner);
        else if (p.headingLevel > 0)
            out += QStringLiteral("<h%1>%2</h%1>\n").arg(qMin(6, p.headingLevel + 1)).arg(inner);
        else if (p.style == QLatin1String("Block Quote"))
            out += QStringLiteral("<blockquote><p>%1</p></blockquote>\n").arg(inner);
        else if (p.style == QLatin1String("Code Block"))
            out += QStringLiteral("<pre>%1</pre>\n").arg(inner);
        else if (p.style == QLatin1String("Caption"))
            out += QStringLiteral("<p align=\"center\"><small><i>%1</i></small></p>\n").arg(inner);
        else
            out += QStringLiteral("<p%1>%2</p>\n").arg(align, inner);
    }
    closeLists(0);
    if (inTable)
        out += QStringLiteral("</table>\n");
    out += QStringLiteral("</body></html>\n");
    return out;
}

} // namespace qbweb
