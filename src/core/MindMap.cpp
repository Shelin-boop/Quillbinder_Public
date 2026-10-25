#include "core/MindMap.h"
#include "text/TextUtil.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <functional>

namespace qb {
namespace mindmap {

bool Appearance::operator==(const Appearance &o) const
{
    return fill == o.fill && textColor == o.textColor && borderColor == o.borderColor && borderWeight == o.borderWeight
           && borderStyle == o.borderStyle && shadow == o.shadow && faded == o.faded;
}

bool TextRange::operator==(const TextRange &o) const
{
    return start == o.start && length == o.length && bold == o.bold && italic == o.italic && underline == o.underline
           && strike == o.strike && link == o.link;
}

QColor Settings::effectiveTextColor() const
{
    if (textColor.isValid())
        return textColor;
    return background.lightnessF() < 0.5 ? QColor(240, 240, 240) : QColor(20, 20, 20);
}

QString fileName() { return QStringLiteral("mindmap.scap"); }
QString scrivenerFileName() { return QStringLiteral("quillbinder-mindmap.scap"); }
QString pathFor(const QString &contentDir) { return contentDir + QLatin1Char('/') + fileName(); }

// ------------------------------------------------------------------ board

Board::Board()
{
    uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    styles = defaultStyles();
}

QList<NoteStyle> Board::defaultStyles()
{
    // a few useful styles to begin with (each board can change or delete them)
    QList<NoteStyle> l;
    auto add = [&](const QString &name, std::function<void(NoteStyle &)> fn) {
        NoteStyle s;
        s.name = name;
        s.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
        s.affectsBody = false;
        fn(s);
        l << s;
    };
    add(QObject::tr("Title"), [](NoteStyle &s) {
        s.affectsFont = true;
        s.fontSize = 18;
        s.bold = true;
    });
    add(QObject::tr("Red Text"), [](NoteStyle &s) {
        s.affectsTextColor = true;
        s.look.textColor = QColor(200, 30, 30);
    });
    add(QObject::tr("Blue Text"), [](NoteStyle &s) {
        s.affectsTextColor = true;
        s.look.textColor = QColor(30, 80, 200);
    });
    auto bubble = [&](const QString &name, const QColor &fill, const QColor &border) {
        add(name, [fill, border](NoteStyle &s) {
            s.affectsBody = true;
            s.look.fill = fill;
            s.look.borderColor = border;
            s.look.borderWeight = 1;
            s.look.borderStyle = BorderStyle::Rounded;
        });
    };
    bubble(QObject::tr("Yellow Bubble"), QColor(255, 244, 168), QColor(200, 170, 40));
    bubble(QObject::tr("Green Bubble"), QColor(208, 240, 192), QColor(90, 160, 70));
    bubble(QObject::tr("Blue Bubble"), QColor(204, 228, 250), QColor(70, 130, 190));
    bubble(QObject::tr("Pink Bubble"), QColor(250, 212, 228), QColor(200, 90, 140));
    add(QObject::tr("Square Box"), [](NoteStyle &s) {
        s.affectsBody = true;
        s.look.borderWeight = 2;
        s.look.borderStyle = BorderStyle::Square;
    });
    add(QObject::tr("Cloud"), [](NoteStyle &s) {
        s.affectsBody = true;
        s.look.fill = QColor(255, 255, 255);
        s.look.borderWeight = 1;
        s.look.borderStyle = BorderStyle::Cloud;
    });
    return l;
}

int Board::nextId() const
{
    // ids start at 1 (0 means "none" in the program); Scapple's own files start at 0, see fromXml
    int m = 0;
    for (const Note &n : notes)
        m = qMax(m, n.id);
    for (const Shape &s : shapes)
        m = qMax(m, s.id);
    return m + 1;
}

Note *Board::note(int id)
{
    for (Note &n : notes)
        if (n.id == id)
            return &n;
    return nullptr;
}

const Note *Board::note(int id) const
{
    for (const Note &n : notes)
        if (n.id == id)
            return &n;
    return nullptr;
}

Shape *Board::shape(int id)
{
    for (Shape &s : shapes)
        if (s.id == id)
            return &s;
    return nullptr;
}

const Shape *Board::shape(int id) const
{
    for (const Shape &s : shapes)
        if (s.id == id)
            return &s;
    return nullptr;
}

QRectF Board::rectOf(int id) const
{
    if (const Note *n = note(id))
        return QRectF(n->pos, QSizeF(n->width, n->isImage() ? n->height : noteHeight(*n)));
    if (const Shape *s = shape(id))
        return s->rect;
    return QRectF();
}

QRectF Board::itemsRect() const
{
    QRectF r;
    for (const Note &n : notes)
        r = r.united(rectOf(n.id));
    for (const Shape &s : shapes)
        r = r.united(s.rect);
    return r;
}

QString Board::lineKey(int a, int b) { return QStringLiteral("%1-%2").arg(qMin(a, b)).arg(qMax(a, b)); }

static QSet<int> *connSet(Board &b, int id)
{
    if (Note *n = b.note(id))
        return &n->connected;
    if (Shape *s = b.shape(id))
        return &s->connected;
    return nullptr;
}

static QSet<int> *arrowSet(Board &b, int id)
{
    if (Note *n = b.note(id))
        return &n->pointsTo;
    if (Shape *s = b.shape(id))
        return &s->pointsTo;
    return nullptr;
}

bool Board::isConnected(int a, int b) const
{
    if (const Note *n = note(a))
        return n->connected.contains(b);
    if (const Shape *s = shape(a))
        return s->connected.contains(b);
    return false;
}

bool Board::pointsTo(int a, int b) const
{
    if (const Note *n = note(a))
        return n->pointsTo.contains(b);
    if (const Shape *s = shape(a))
        return s->pointsTo.contains(b);
    return false;
}

void Board::connect(int a, int b, bool arrowToB, bool arrowToA)
{
    if (a == b || !exists(a) || !exists(b))
        return;
    connSet(*this, a)->insert(b);
    connSet(*this, b)->insert(a);
    if (arrowToB)
        arrowSet(*this, a)->insert(b);
    if (arrowToA)
        arrowSet(*this, b)->insert(a);
}

void Board::disconnect(int a, int b)
{
    for (auto [x, y] : {std::pair<int, int>{a, b}, {b, a}}) {
        if (QSet<int> *c = connSet(*this, x))
            c->remove(y);
        if (QSet<int> *p = arrowSet(*this, x))
            p->remove(y);
    }
    lineLabels.remove(lineKey(a, b));
    lineLooks.remove(lineKey(a, b));
}

void Board::removeArrows(int a, int b)
{
    if (QSet<int> *p = arrowSet(*this, a))
        p->remove(b);
    if (QSet<int> *p = arrowSet(*this, b))
        p->remove(a);
}

void Board::setArrow(int from, int to, bool on)
{
    if (!isConnected(from, to))
        return;
    if (QSet<int> *p = arrowSet(*this, from)) {
        if (on)
            p->insert(to);
        else
            p->remove(to);
    }
}

void Board::setArrows(int a, int b, bool toB, bool toA)
{
    if (!isConnected(a, b))
        return;
    setArrow(a, b, toB);
    setArrow(b, a, toA);
}

LineLook Board::lineLook(int a, int b) const { return lineLooks.value(lineKey(a, b)); }

void Board::setLineLook(int a, int b, const LineLook &look)
{
    if (look.isEmpty() || !isConnected(a, b))
        lineLooks.remove(lineKey(a, b));
    else
        lineLooks.insert(lineKey(a, b), look);
}

IconMap Board::iconsOf(int id) const
{
    if (const Note *n = note(id))
        return n->icons;
    if (const Shape *s = shape(id))
        return s->icons;
    return {};
}

void Board::setIcon(int id, int position, const QString &iconId)
{
    if (position < 0 || position >= kIconPositions)
        return;
    IconMap *m = nullptr;
    if (Note *n = note(id))
        m = &n->icons;
    else if (Shape *s = shape(id))
        m = &s->icons;
    if (!m)
        return;
    if (iconId.isEmpty())
        m->remove(position);
    else
        m->insert(position, iconId);
}

QSet<int> Board::connectionsOf(int id) const
{
    if (const Note *n = note(id))
        return n->connected;
    if (const Shape *s = shape(id))
        return s->connected;
    return {};
}

QString Board::lineLabel(int a, int b) const { return lineLabels.value(lineKey(a, b)); }

void Board::setLineLabel(int a, int b, const QString &label)
{
    if (label.trimmed().isEmpty())
        lineLabels.remove(lineKey(a, b));
    else
        lineLabels.insert(lineKey(a, b), label.trimmed());
}

void Board::remove(int id)
{
    for (int other : connectionsOf(id))
        disconnect(id, other);
    for (int i = 0; i < notes.size(); ++i)
        if (notes[i].id == id) {
            notes.removeAt(i);
            break;
        }
    for (int i = 0; i < shapes.size(); ++i)
        if (shapes[i].id == id) {
            shapes.removeAt(i);
            break;
        }
    // stray references (files of other programs)
    for (Note &n : notes) {
        n.connected.remove(id);
        n.pointsTo.remove(id);
    }
    for (Shape &s : shapes) {
        s.connected.remove(id);
        s.pointsTo.remove(id);
    }
}

void Board::bringToFront(const QList<int> &ids)
{
    for (int id : ids) {
        for (int i = 0; i < notes.size(); ++i)
            if (notes[i].id == id) {
                notes.append(notes.takeAt(i));
                break;
            }
        for (int i = 0; i < shapes.size(); ++i)
            if (shapes[i].id == id) {
                shapes.append(shapes.takeAt(i));
                break;
            }
    }
}

void Board::sendToBack(const QList<int> &ids)
{
    for (int k = ids.size() - 1; k >= 0; --k) {
        const int id = ids[k];
        for (int i = 0; i < notes.size(); ++i)
            if (notes[i].id == id) {
                notes.prepend(notes.takeAt(i));
                break;
            }
        for (int i = 0; i < shapes.size(); ++i)
            if (shapes[i].id == id) {
                shapes.prepend(shapes.takeAt(i));
                break;
            }
    }
}

// ------------------------------------------------------------------ text layout

QString Board::fontFamily() const
{
    if (!settings.font.isEmpty())
        return settings.font;
    return QGuiApplication::font().family();
}

void Board::fillDocument(QTextDocument *doc, const Note &n) const
{
    QFont f(fontFamily());
    f.setPointSizeF(qMax(4.0, n.fontSize));
    f.setBold(n.bold);
    f.setItalic(n.italic);
    f.setUnderline(n.underline);
    f.setStrikeOut(n.strike);
    doc->setDefaultFont(f);
    doc->setDocumentMargin(0);
    QTextOption opt = doc->defaultTextOption();
    opt.setAlignment(n.align == Align::Center    ? Qt::AlignHCenter
                     : n.align == Align::Right   ? Qt::AlignRight
                     : n.align == Align::Justify ? Qt::AlignJustify
                                                 : Qt::AlignLeft);
    opt.setTextDirection(settings.rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc->setDefaultTextOption(opt);
    doc->setPlainText(n.text);
    QTextCursor all(doc);
    all.select(QTextCursor::Document);
    QTextBlockFormat bf;
    bf.setAlignment(opt.alignment());
    bf.setLayoutDirection(settings.rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    all.mergeBlockFormat(bf);
    const int len = n.text.size();
    for (const TextRange &r : n.ranges) {
        const int s = qBound(0, r.start, len), e = qBound(0, r.start + r.length, len);
        if (s >= e)
            continue;
        QTextCursor c(doc);
        c.setPosition(s);
        c.setPosition(e, QTextCursor::KeepAnchor);
        QTextCharFormat cf;
        if (r.bold)
            cf.setFontWeight(QFont::Bold);
        if (r.italic)
            cf.setFontItalic(true);
        if (r.underline)
            cf.setFontUnderline(true);
        if (r.strike)
            cf.setFontStrikeOut(true);
        if (!r.link.isEmpty()) {
            cf.setAnchor(true);
            cf.setAnchorHref(r.link);
            cf.setFontUnderline(true);
            cf.setForeground(QColor(30, 90, 200));
        }
        c.mergeCharFormat(cf);
    }
}

void Board::readDocument(const QTextDocument *doc, Note &n)
{
    n.text = doc->toPlainText();
    n.ranges.clear();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            TextRange r;
            r.start = fr.position();
            r.length = fr.length();
            r.bold = cf.fontWeight() >= QFont::Bold && !n.bold;
            r.italic = cf.fontItalic() && !n.italic;
            r.link = cf.isAnchor() ? cf.anchorHref() : QString();
            r.underline = cf.fontUnderline() && !n.underline && r.link.isEmpty();
            r.strike = cf.fontStrikeOut() && !n.strike;
            if (!(r.bold || r.italic || r.underline || r.strike || !r.link.isEmpty()))
                continue;
            // joins with the range before when they are alike and touch
            if (!n.ranges.isEmpty()) {
                TextRange &last = n.ranges.last();
                if (last.start + last.length == r.start && last.bold == r.bold && last.italic == r.italic
                    && last.underline == r.underline && last.strike == r.strike && last.link == r.link) {
                    last.length += r.length;
                    continue;
                }
            }
            n.ranges << r;
        }
    }
}

double Board::noteHeight(const Note &n) const
{
    if (n.isImage())
        return n.height;
    QTextDocument doc;
    fillDocument(&doc, n);
    doc.setTextWidth(qMax(10.0, n.width - 2 * kPadX - n.indent * kIndentStep));
    return std::ceil(doc.size().height() + 2 * kPadY);
}

double Board::fittingWidth(const Note &n) const
{
    QTextDocument doc;
    fillDocument(&doc, n);
    doc.setTextWidth(-1);
    return std::ceil(doc.idealWidth() + 2 * kPadX + n.indent * kIndentStep + 2);
}

// ------------------------------------------------------------------ stacks

QList<int> Board::stackBelow(int id) const
{
    QList<int> out;
    QSet<int> seen{id};
    int cur = id;
    for (;;) {
        const QRectF r = rectOf(cur);
        int next = 0;
        bool found = false;
        for (const Note &n : notes) {
            if (seen.contains(n.id))
                continue;
            if (qAbs(n.pos.x() - r.left()) < 0.6 && qAbs(n.pos.y() - r.bottom()) < 0.6) {
                next = n.id;
                found = true;
                break;
            }
        }
        if (!found)
            break;
        out << next;
        seen.insert(next);
        cur = next;
    }
    return out;
}

int Board::stackAbove(int id) const
{
    const Note *me = note(id);
    if (!me)
        return 0;
    for (const Note &n : notes) {
        if (n.id == id)
            continue;
        const QRectF r = rectOf(n.id);
        if (qAbs(me->pos.x() - r.left()) < 0.6 && qAbs(me->pos.y() - r.bottom()) < 0.6)
            return n.id;
    }
    return 0;
}

void Board::restack(int id, double oldBottom)
{
    const Note *me = note(id);
    if (!me)
        return;
    const double newBottom = rectOf(id).bottom();
    if (qAbs(newBottom - oldBottom) < 0.01)
        return;
    // the notes that hung under the old bottom move with it
    QList<int> chain;
    {
        QSet<int> seen{id};
        double bottom = oldBottom;
        const double left = me->pos.x();
        for (;;) {
            int next = 0;
            for (const Note &n : notes)
                if (!seen.contains(n.id) && qAbs(n.pos.x() - left) < 0.6 && qAbs(n.pos.y() - bottom) < 0.6) {
                    next = n.id;
                    break;
                }
            if (!next)
                break;
            chain << next;
            seen.insert(next);
            bottom = rectOf(next).bottom();
        }
    }
    const double dy = newBottom - oldBottom;
    for (int nid : chain)
        if (Note *n = note(nid))
            n->pos.ry() += dy;
}

// ------------------------------------------------------------------ order and overlap

QList<int> Board::shapesUnder(int id) const
{
    const QRectF r = rectOf(id);
    QList<int> out;
    for (const Shape &s : shapes)
        if (s.id != id && s.rect.intersects(r))
            out << s.id;
    return out;
}

QList<int> Board::overlapping(int shapeId) const
{
    QList<int> out;
    const Shape *sh = shape(shapeId);
    if (!sh)
        return out;
    for (const Note &n : notes)
        if (sh->rect.intersects(rectOf(n.id)))
            out << n.id;
    for (const Shape &s : shapes)
        if (s.id != shapeId && sh->rect.contains(s.rect))
            out << s.id;
    return out;
}

QList<int> Board::readingOrder(const QSet<int> &only) const
{
    QList<const Note *> scan;
    for (const Note &n : notes)
        if (only.isEmpty() || only.contains(n.id))
            scan << &n;
    const bool rtl = settings.rightToLeft;
    std::stable_sort(scan.begin(), scan.end(), [&](const Note *a, const Note *b) {
        // rows of 24 px: notes beside each other count as one row
        const int ra = int(std::floor(a->pos.y() / 24.0)), rb = int(std::floor(b->pos.y() / 24.0));
        if (ra != rb)
            return ra < rb;
        const double xa = rtl ? -(a->pos.x() + a->width) : a->pos.x();
        const double xb = rtl ? -(b->pos.x() + b->width) : b->pos.x();
        return xa < xb;
    });
    QList<int> out;
    QSet<int> done;
    std::function<void(int)> visit = [&](int id) {
        if (done.contains(id) || (!only.isEmpty() && !only.contains(id)) || !note(id))
            return;
        // the whole stack, from its top
        int top = id;
        for (int above = stackAbove(top), guard = 0; above && guard < 10000; above = stackAbove(top), ++guard) {
            if (done.contains(above))
                break;
            top = above;
        }
        QList<int> stack{top};
        stack << stackBelow(top);
        QList<int> added;
        for (int s : stack)
            if (!done.contains(s) && (only.isEmpty() || only.contains(s))) {
                done.insert(s);
                out << s;
                added << s;
            }
        // then what hangs on them, nearest first
        for (int s : added) {
            const QPointF c = rectOf(s).center();
            QList<int> next = connectionsOf(s).values();
            std::sort(next.begin(), next.end(), [&](int a, int b) {
                const QPointF da = rectOf(a).center() - c, db = rectOf(b).center() - c;
                return QPointF::dotProduct(da, da) < QPointF::dotProduct(db, db);
            });
            for (int n : next)
                visit(n);
        }
    };
    for (const Note *n : scan)
        visit(n->id);
    return out;
}

// ------------------------------------------------------------------ text exports

QString Board::toPlainText(const QSet<int> &only, const QString &separator) const
{
    QStringList parts;
    for (int id : readingOrder(only)) {
        const Note *n = note(id);
        if (!n || n->isImage())
            continue;
        parts << n->text.trimmed();
    }
    const QString sep = separator.isEmpty() ? QStringLiteral("\n\n") : QStringLiteral("\n\n") + separator + QStringLiteral("\n\n");
    return parts.join(sep) + QLatin1Char('\n');
}

QString Board::toPlainList(const QSet<int> &only) const
{
    QStringList lines;
    for (int id : readingOrder(only)) {
        const Note *n = note(id);
        if (!n || n->isImage())
            continue;
        lines << n->text.simplified();
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

static QString xmlAttr(QString s)
{
    s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    s.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    s.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    s.replace(QLatin1Char('\n'), QLatin1String("&#10;"));
    return s;
}

QByteArray Board::toOpml(const QSet<int> &only, bool notesField, const QString &title) const
{
    // shapes become groups of the notes on them: each note goes to the
    // smallest shape it lies on, each shape into the smallest one around it
    auto area = [](const QRectF &r) { return r.width() * r.height(); };
    QHash<int, int> parentOf; // item -> shape
    for (const Note &n : notes) {
        const QRectF r = rectOf(n.id);
        int best = 0;
        double bestArea = 0;
        for (const Shape &s : shapes)
            if (s.rect.intersects(r) && (!best || area(s.rect) < bestArea)) {
                best = s.id;
                bestArea = area(s.rect);
            }
        if (best)
            parentOf.insert(n.id, best);
    }
    for (const Shape &a : shapes) {
        int best = 0;
        double bestArea = 0;
        for (const Shape &b : shapes)
            if (a.id != b.id && b.rect.contains(a.rect) && (!best || area(b.rect) < bestArea)) {
                best = b.id;
                bestArea = area(b.rect);
            }
        if (best)
            parentOf.insert(a.id, best);
    }
    const QList<int> order = readingOrder(only);
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<opml version=\"2.0\">\n  <head>\n    <title>%1</title>\n  </head>\n  <body>\n")
                      .arg(xmlAttr(title));
    QSet<int> usedShapes;
    for (int id : order) {
        int p = parentOf.value(id);
        while (p) {
            usedShapes.insert(p);
            p = parentOf.value(p);
        }
    }
    std::function<void(int, int)> emitShape;
    auto emitNote = [&](int id, int depth) {
        const Note *n = note(id);
        if (!n)
            return;
        const QString ind(depth * 2 + 4, QLatin1Char(' '));
        QString text = n->isImage() ? QObject::tr("[Picture]") : n->text.trimmed();
        if (notesField) {
            QString head = text.section(QLatin1Char('\n'), 0, 0);
            if (head.size() > 50)
                head = head.left(50).trimmed() + QStringLiteral("...");
            out += ind + QStringLiteral("<outline text=\"%1\" _note=\"%2\"/>\n").arg(xmlAttr(head), xmlAttr(text));
        } else {
            out += ind + QStringLiteral("<outline text=\"%1\"/>\n").arg(xmlAttr(text));
        }
    };
    QSet<int> emitted;
    emitShape = [&](int sid, int depth) {
        if (emitted.contains(sid))
            return;
        emitted.insert(sid);
        const QString ind(depth * 2 + 4, QLatin1Char(' '));
        out += ind + QStringLiteral("<outline text=\"%1\">\n").arg(xmlAttr(QObject::tr("Group")));
        for (int id : order)
            if (parentOf.value(id) == sid && !emitted.contains(id)) {
                // a shape inside comes where its first note would
                emitted.insert(id);
                emitNote(id, depth + 1);
            }
        for (const Shape &s : shapes)
            if (parentOf.value(s.id) == sid && usedShapes.contains(s.id))
                emitShape(s.id, depth + 1);
        out += ind + QStringLiteral("</outline>\n");
    };
    for (int id : order) {
        if (emitted.contains(id))
            continue;
        int top = parentOf.value(id);
        while (top && parentOf.value(top))
            top = parentOf.value(top);
        if (top) {
            emitShape(top, 0);
            continue;
        }
        emitted.insert(id);
        emitNote(id, 0);
    }
    out += QStringLiteral("  </body>\n</opml>\n");
    return out.toUtf8();
}

// ------------------------------------------------------------------ file

namespace {

QString colorText(const QColor &c)
{
    auto n = [](double v) { return QString::number(v, 'f', 6).remove(QRegularExpression(QStringLiteral("0+$"))).remove(QRegularExpression(QStringLiteral("\\.$"))); };
    return QStringLiteral("%1 %2 %3").arg(n(c.redF()), n(c.greenF()), n(c.blueF()));
}

QColor parseColor(const QString &t)
{
    const QStringList p = t.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (p.size() < 3)
        return QColor();
    bool ok1, ok2, ok3;
    const double r = p[0].toDouble(&ok1), g = p[1].toDouble(&ok2), b = p[2].toDouble(&ok3);
    if (!ok1 || !ok2 || !ok3)
        return QColor();
    return QColor::fromRgbF(float(qBound(0.0, r, 1.0)), float(qBound(0.0, g, 1.0)), float(qBound(0.0, b, 1.0)));
}

// Names of the eight icon places in the file (0.21).
const char *const kIconPosNames[kIconPositions] = {"TopLeft", "Top", "TopRight", "Right", "BottomRight", "Bottom", "BottomLeft", "Left"};

int iconPosFromName(const QString &name)
{
    for (int i = 0; i < kIconPositions; ++i)
        if (name.compare(QLatin1String(kIconPosNames[i]), Qt::CaseInsensitive) == 0)
            return i;
    bool ok = false;
    const int n = name.toInt(&ok);
    return ok && n >= 0 && n < kIconPositions ? n : -1;
}

QString num(double v)
{
    QString s = QString::number(v, 'f', 1);
    return s;
}

QString idList(const QSet<int> &ids)
{
    QList<int> l = ids.values();
    std::sort(l.begin(), l.end());
    QStringList parts;
    int i = 0;
    while (i < l.size()) {
        int j = i;
        while (j + 1 < l.size() && l[j + 1] == l[j] + 1)
            ++j;
        parts << (j > i ? QStringLiteral("%1-%2").arg(l[i]).arg(l[j]) : QString::number(l[i]));
        i = j + 1;
    }
    return parts.join(QStringLiteral(", "));
}

QSet<int> parseIds(const QString &t)
{
    QSet<int> out;
    for (const QString &part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString p = part.trimmed();
        const int dash = p.indexOf(QLatin1Char('-'), 1);
        if (dash > 0) {
            bool a, b;
            const int s = p.left(dash).trimmed().toInt(&a), e = p.mid(dash + 1).trimmed().toInt(&b);
            // in 64 bits: "-2147483648-2147483647" must not overflow into a small range (0.20)
            if (a && b && qint64(e) >= qint64(s) && qint64(e) - qint64(s) < 100000)
                for (qint64 k = s; k <= e; ++k)
                    out.insert(int(k));
        } else {
            bool ok;
            const int v = p.toInt(&ok);
            if (ok)
                out.insert(v);
        }
    }
    return out;
}

QString yes(bool b) { return b ? QStringLiteral("Yes") : QStringLiteral("No"); }
bool isYes(const QString &s) { return s.trimmed().compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0; }

QString alignText(Align a)
{
    switch (a) {
    case Align::Center: return QStringLiteral("Center");
    case Align::Right: return QStringLiteral("Right");
    case Align::Justify: return QStringLiteral("Justified");
    default: return QStringLiteral("Left");
    }
}

Align parseAlign(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t == QLatin1String("center") || t == QLatin1String("centre"))
        return Align::Center;
    if (t == QLatin1String("right"))
        return Align::Right;
    if (t.startsWith(QLatin1String("justif")))
        return Align::Justify;
    return Align::Left;
}

QString borderText(BorderStyle s)
{
    switch (s) {
    case BorderStyle::Square: return QStringLiteral("Square");
    case BorderStyle::Jagged: return QStringLiteral("Jagged");
    case BorderStyle::Cloud: return QStringLiteral("Cloud");
    default: return QStringLiteral("Rounded");
    }
}

BorderStyle parseBorder(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t == QLatin1String("square"))
        return BorderStyle::Square;
    if (t == QLatin1String("jagged"))
        return BorderStyle::Jagged;
    if (t == QLatin1String("cloud"))
        return BorderStyle::Cloud;
    return BorderStyle::Rounded;
}

QString imageText(const QImage &img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

QImage parseImage(const QString &t)
{
    QImage img;
    img.loadFromData(QByteArray::fromBase64(t.trimmed().toLatin1()));
    return img;
}

void writeAppearance(QDomDocument &d, QDomElement parent, const Appearance &a, bool withText, Align align,
                     bool bold = false, bool italic = false, bool underline = false, bool strike = false)
{
    QDomElement ap = d.createElement(QStringLiteral("Appearance"));
    auto child = [&](const QString &name, const QString &text) {
        QDomElement e = d.createElement(name);
        e.appendChild(d.createTextNode(text));
        ap.appendChild(e);
        return e;
    };
    if (withText)
        child(QStringLiteral("Alignment"), alignText(align));
    if (a.borderWeight > 0) {
        QDomElement b = child(QStringLiteral("Border"), a.borderColor.isValid() ? colorText(a.borderColor) : QString());
        b.setAttribute(QStringLiteral("Weight"), a.borderWeight);
        b.setAttribute(QStringLiteral("Style"), borderText(a.borderStyle));
    } else if (a.borderStyle != BorderStyle::Rounded) {
        // the shape of the selection follows the style even without a border
        QDomElement b = child(QStringLiteral("Border"), QString());
        b.setAttribute(QStringLiteral("Weight"), 0);
        b.setAttribute(QStringLiteral("Style"), borderText(a.borderStyle));
    }
    if (a.fill.isValid())
        child(QStringLiteral("Fill"), colorText(a.fill));
    if (withText && a.textColor.isValid())
        child(QStringLiteral("TextColor"), colorText(a.textColor));
    if (withText && bold)
        child(QStringLiteral("IsBold"), yes(true));
    if (withText && italic)
        child(QStringLiteral("IsItalic"), yes(true));
    if (withText && underline)
        child(QStringLiteral("IsUnderlined"), yes(true));
    if (withText && strike)
        child(QStringLiteral("IsStrikethrough"), yes(true));
    if (a.shadow)
        child(QStringLiteral("Shadow"), yes(true));
    if (a.faded)
        child(QStringLiteral("Faded"), yes(true));
    parent.appendChild(ap);
}

void readAppearance(const QDomElement &ap, Appearance &a, Align *align, bool *bold, bool *italic, bool *underline, bool *strike)
{
    if (ap.isNull())
        return;
    const QDomElement b = ap.firstChildElement(QStringLiteral("Border"));
    if (!b.isNull()) {
        a.borderWeight = b.hasAttribute(QStringLiteral("Weight")) ? qBound(0, int(std::lround(b.attribute(QStringLiteral("Weight")).toDouble())), 5) : 1;
        a.borderStyle = parseBorder(b.attribute(QStringLiteral("Style")));
        const QString t = b.text().trimmed();
        if (!b.hasAttribute(QStringLiteral("Style")) && !t.contains(QLatin1Char(' ')) && !t.isEmpty())
            a.borderStyle = parseBorder(t); // <Border>Square</Border>
        else
            a.borderColor = parseColor(t);
    }
    a.fill = parseColor(ap.firstChildElement(QStringLiteral("Fill")).text());
    a.textColor = parseColor(ap.firstChildElement(QStringLiteral("TextColor")).text());
    a.shadow = isYes(ap.firstChildElement(QStringLiteral("Shadow")).text());
    a.faded = isYes(ap.firstChildElement(QStringLiteral("Faded")).text());
    if (align && !ap.firstChildElement(QStringLiteral("Alignment")).isNull())
        *align = parseAlign(ap.firstChildElement(QStringLiteral("Alignment")).text());
    if (bold)
        *bold = isYes(ap.firstChildElement(QStringLiteral("IsBold")).text());
    if (italic)
        *italic = isYes(ap.firstChildElement(QStringLiteral("IsItalic")).text());
    if (underline)
        *underline = isYes(ap.firstChildElement(QStringLiteral("IsUnderlined")).text());
    if (strike)
        *strike = isYes(ap.firstChildElement(QStringLiteral("IsStrikethrough")).text());
}

IconMap readIcons(const QDomElement &item)
{
    IconMap out;
    const QDomElement is = item.firstChildElement(QStringLiteral("Icons"));
    for (QDomElement e = is.firstChildElement(QStringLiteral("Icon")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Icon"))) {
        const int pos = iconPosFromName(e.attribute(QStringLiteral("Position")));
        const QString id = e.text().trimmed();
        if (pos >= 0 && !id.isEmpty())
            out.insert(pos, id);
    }
    return out;
}

QPointF parsePoint(const QString &t, bool *ok)
{
    const QStringList p = t.split(QLatin1Char(','));
    bool a = false, b = false;
    QPointF pt;
    if (p.size() >= 2) {
        pt.setX(p[0].trimmed().toDouble(&a));
        pt.setY(p[1].trimmed().toDouble(&b));
    }
    *ok = a && b;
    return pt;
}

} // namespace

QByteArray Board::toXml() const
{
    QDomDocument d;
    d.appendChild(d.createProcessingInstruction(QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"")));
    QDomElement root = d.createElement(QStringLiteral("ScappleDocument"));
    root.setAttribute(QStringLiteral("Version"), QStringLiteral("1.2"));
    root.setAttribute(QStringLiteral("ID"), uuid);
    d.appendChild(root);
    auto text = [&](QDomElement parent, const QString &name, const QString &t) {
        QDomElement e = d.createElement(name);
        e.appendChild(d.createTextNode(t));
        parent.appendChild(e);
        return e;
    };
    // <Icons><Icon Position="TopLeft">qb-flag-red</Icon> ... </Icons> (0.21; not a Scapple element)
    auto writeIcons = [&](QDomElement parent, const IconMap &icons) {
        if (icons.isEmpty())
            return;
        QDomElement is = d.createElement(QStringLiteral("Icons"));
        for (auto it = icons.cbegin(); it != icons.cend(); ++it) {
            if (it.key() < 0 || it.key() >= kIconPositions || it.value().isEmpty())
                continue;
            QDomElement ie = text(is, QStringLiteral("Icon"), it.value());
            ie.setAttribute(QStringLiteral("Position"), QLatin1String(kIconPosNames[it.key()]));
        }
        parent.appendChild(is);
    };

    QDomElement ns = d.createElement(QStringLiteral("Notes"));
    root.appendChild(ns);
    for (const Note &n : notes) {
        QDomElement e = d.createElement(QStringLiteral("Note"));
        e.setAttribute(QStringLiteral("ID"), n.id);
        e.setAttribute(QStringLiteral("FontSize"), num(n.fontSize));
        e.setAttribute(QStringLiteral("Position"), num(n.pos.x()) + QLatin1Char(',') + num(n.pos.y()));
        e.setAttribute(QStringLiteral("Width"), num(n.width));
        if (n.isImage())
            e.setAttribute(QStringLiteral("Height"), num(n.height));
        if (n.indent > 0)
            e.setAttribute(QStringLiteral("Indent"), n.indent);
        writeAppearance(d, e, n.look, true, n.align, n.bold, n.italic, n.underline, n.strike);
        text(e, QStringLiteral("String"), n.text);
        if (!n.ranges.isEmpty()) {
            QDomElement f = d.createElement(QStringLiteral("Formatting"));
            for (const TextRange &r : n.ranges) {
                QDomElement fr = text(f, QStringLiteral("FormatRange"), QStringLiteral("%1,%2").arg(r.start).arg(r.length));
                if (r.bold)
                    fr.setAttribute(QStringLiteral("Bold"), yes(true));
                if (r.italic)
                    fr.setAttribute(QStringLiteral("Italic"), yes(true));
                if (r.underline)
                    fr.setAttribute(QStringLiteral("Underline"), yes(true));
                if (r.strike)
                    fr.setAttribute(QStringLiteral("Strikethrough"), yes(true));
                if (!r.link.isEmpty())
                    fr.setAttribute(QStringLiteral("Link"), r.link);
            }
            e.appendChild(f);
        }
        if (n.isImage())
            text(e, QStringLiteral("Image"), imageText(n.image));
        if (!n.connected.isEmpty())
            text(e, QStringLiteral("ConnectedNoteIDs"), idList(n.connected));
        if (!n.pointsTo.isEmpty())
            text(e, QStringLiteral("PointsToNoteIDs"), idList(n.pointsTo));
        writeIcons(e, n.icons);
        ns.appendChild(e);
    }
    if (!shapes.isEmpty()) {
        QDomElement bs = d.createElement(QStringLiteral("BackgroundShapes"));
        root.appendChild(bs);
        for (const Shape &s : shapes) {
            QDomElement e = d.createElement(QStringLiteral("Shape"));
            e.setAttribute(QStringLiteral("ID"), s.id);
            e.setAttribute(QStringLiteral("Position"), num(s.rect.x()) + QLatin1Char(',') + num(s.rect.y()));
            e.setAttribute(QStringLiteral("Width"), num(s.rect.width()));
            e.setAttribute(QStringLiteral("Height"), num(s.rect.height()));
            writeAppearance(d, e, s.look, false, Align::Left);
            text(e, QStringLiteral("Magnetic"), yes(s.magnetic));
            if (!s.connected.isEmpty())
                text(e, QStringLiteral("ConnectedNoteIDs"), idList(s.connected));
            if (!s.pointsTo.isEmpty())
                text(e, QStringLiteral("PointsToNoteIDs"), idList(s.pointsTo));
            writeIcons(e, s.icons);
            bs.appendChild(e);
        }
    }
    if (!lineLabels.isEmpty()) {
        QDomElement ls = d.createElement(QStringLiteral("ConnectionLabels"));
        QStringList keys = lineLabels.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys) {
            QDomElement e = text(ls, QStringLiteral("Label"), lineLabels.value(k));
            e.setAttribute(QStringLiteral("NoteIDs"), QString(k).replace(QLatin1Char('-'), QStringLiteral(", ")));
        }
        root.appendChild(ls);
    }
    // the color and middle icon of lines (0.21; not a Scapple element)
    if (!lineLooks.isEmpty()) {
        QDomElement ls = d.createElement(QStringLiteral("ConnectionLooks"));
        QStringList keys = lineLooks.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys) {
            const LineLook &look = lineLooks[k];
            QDomElement e = d.createElement(QStringLiteral("Look"));
            e.setAttribute(QStringLiteral("NoteIDs"), QString(k).replace(QLatin1Char('-'), QStringLiteral(", ")));
            if (look.color.isValid())
                e.setAttribute(QStringLiteral("Color"), colorText(look.color));
            if (!look.icon.isEmpty())
                e.setAttribute(QStringLiteral("Icon"), look.icon);
            ls.appendChild(e);
        }
        root.appendChild(ls);
    }
    QDomElement st = d.createElement(QStringLiteral("NoteStyles"));
    root.appendChild(st);
    for (const NoteStyle &s : styles) {
        QDomElement e = d.createElement(QStringLiteral("Style"));
        e.setAttribute(QStringLiteral("Name"), s.name);
        e.setAttribute(QStringLiteral("ID"), s.id);
        e.setAttribute(QStringLiteral("AffectFontStyle"), yes(s.affectsFont));
        e.setAttribute(QStringLiteral("AffectAlignment"), yes(s.affectsAlignment));
        e.setAttribute(QStringLiteral("AffectTextColor"), yes(s.affectsTextColor));
        e.setAttribute(QStringLiteral("AffectNoteBody"), yes(s.affectsBody));
        e.setAttribute(QStringLiteral("AffectSize"), yes(s.affectsSize));
        e.setAttribute(QStringLiteral("AffectFade"), yes(s.affectsFade));
        e.setAttribute(QStringLiteral("AffectSticky"), yes(s.affectsMagnetic));
        if (s.affectsFont) {
            text(e, QStringLiteral("FontSize"), num(s.fontSize));
            if (s.bold)
                text(e, QStringLiteral("IsBold"), yes(true));
            if (s.italic)
                text(e, QStringLiteral("IsItalic"), yes(true));
            if (s.underline)
                text(e, QStringLiteral("IsUnderlined"), yes(true));
            if (s.strike)
                text(e, QStringLiteral("IsStrikethrough"), yes(true));
        }
        if (s.affectsAlignment) {
            text(e, QStringLiteral("Alignment"), alignText(s.align));
            if (s.indent)
                text(e, QStringLiteral("Indent"), QString::number(s.indent));
        }
        if (s.affectsTextColor && s.look.textColor.isValid())
            text(e, QStringLiteral("TextColor"), colorText(s.look.textColor));
        if (s.affectsBody) {
            if (s.look.fill.isValid())
                text(e, QStringLiteral("FillColor"), colorText(s.look.fill));
            text(e, QStringLiteral("BorderThickness"), QString::number(s.look.borderWeight));
            if (s.look.borderColor.isValid())
                text(e, QStringLiteral("BorderColor"), colorText(s.look.borderColor));
            text(e, QStringLiteral("BorderStyle"), borderText(s.look.borderStyle));
            if (s.look.shadow)
                text(e, QStringLiteral("Shadow"), yes(true));
        }
        if (s.affectsSize) {
            text(e, QStringLiteral("Width"), num(s.width));
            if (s.height > 0)
                text(e, QStringLiteral("Height"), num(s.height));
        }
        if (s.affectsFade)
            text(e, QStringLiteral("Faded"), yes(s.look.faded));
        if (s.affectsMagnetic)
            text(e, QStringLiteral("Magnetic"), yes(s.magnetic));
        st.appendChild(e);
    }
    QDomElement ui = d.createElement(QStringLiteral("UISettings"));
    root.appendChild(ui);
    // older versions and Scapple read one color: the board's own for light themes, else the default
    const QColor oldColor = settings.ownLight.isValid()       ? settings.ownLight
                            : settings.defaultLight.isValid() ? settings.defaultLight
                                                              : settings.background;
    text(ui, QStringLiteral("BackgroundColor"), colorText(oldColor));
    {
        // the board's own colors per kind of theme (0.21.1); empty: the default
        QDomElement own = d.createElement(QStringLiteral("QuillbinderBackground"));
        own.setAttribute(QStringLiteral("Light"), settings.ownLight.isValid() ? colorText(settings.ownLight) : QString());
        own.setAttribute(QStringLiteral("Dark"), settings.ownDark.isValid() ? colorText(settings.ownDark) : QString());
        ui.appendChild(own);
    }
    if (!settings.texture.isNull())
        text(ui, QStringLiteral("BackgroundTexture"), imageText(settings.texture));
    if (settings.textColor.isValid())
        text(ui, QStringLiteral("DefaultTextColor"), colorText(settings.textColor));
    if (!settings.font.isEmpty())
        text(ui, QStringLiteral("DefaultFont"), settings.font);
    text(ui, QStringLiteral("WritingDirection"), settings.rightToLeft ? QStringLiteral("RightToLeft") : QStringLiteral("LeftToRight"));
    if (settings.locked)
        text(ui, QStringLiteral("PreventEditing"), yes(true));
    if (!settings.footer)
        text(ui, QStringLiteral("ShowFooter"), yes(false));
    if (settings.pageGuides)
        text(ui, QStringLiteral("PageGuides"), yes(true));
    return d.toByteArray(4);
}

bool Board::fromXml(const QByteArray &xml, QString *error)
{
    QDomDocument d;
    QString msg;
    int line = 0;
    if (!d.setContent(xml, &msg, &line)) {
        if (error)
            *error = QObject::tr("The mind map file cannot be read (line %1: %2).").arg(line).arg(msg);
        return false;
    }
    const QDomElement root = d.documentElement();
    if (root.tagName() != QLatin1String("ScappleDocument")) {
        if (error)
            *error = QObject::tr("This is not a mind map (Scapple) file.");
        return false;
    }
    notes.clear();
    shapes.clear();
    styles.clear();
    lineLabels.clear();
    lineLooks.clear();
    settings = Settings();
    int idShift = 0;
    uuid = root.attribute(QStringLiteral("ID"));
    if (uuid.isEmpty())
        uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    QSet<int> ids;
    const QDomElement ns = root.firstChildElement(QStringLiteral("Notes"));
    for (QDomElement e = ns.firstChildElement(QStringLiteral("Note")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Note"))) {
        Note n;
        n.id = e.attribute(QStringLiteral("ID")).toInt();
        if (ids.contains(n.id))
            continue; // a broken file: the first note of an id wins
        bool ok = false;
        n.pos = parsePoint(e.attribute(QStringLiteral("Position")), &ok);
        n.width = qMax(20.0, e.attribute(QStringLiteral("Width"), QStringLiteral("160")).toDouble());
        n.height = e.attribute(QStringLiteral("Height")).toDouble();
        n.fontSize = e.attribute(QStringLiteral("FontSize"), QStringLiteral("12")).toDouble();
        if (n.fontSize <= 0)
            n.fontSize = 12;
        n.indent = qBound(0, e.attribute(QStringLiteral("Indent")).toInt(), 20);
        readAppearance(e.firstChildElement(QStringLiteral("Appearance")), n.look, &n.align, &n.bold, &n.italic, &n.underline, &n.strike);
        n.text = e.firstChildElement(QStringLiteral("String")).text();
        const QDomElement f = e.firstChildElement(QStringLiteral("Formatting"));
        for (QDomElement r = f.firstChildElement(QStringLiteral("FormatRange")); !r.isNull(); r = r.nextSiblingElement(QStringLiteral("FormatRange"))) {
            TextRange tr;
            const QStringList p = r.text().split(QLatin1Char(','));
            if (p.size() < 2)
                continue;
            tr.start = p[0].trimmed().toInt();
            tr.length = p[1].trimmed().toInt();
            tr.bold = isYes(r.attribute(QStringLiteral("Bold")));
            tr.italic = isYes(r.attribute(QStringLiteral("Italic")));
            tr.underline = isYes(r.attribute(QStringLiteral("Underline")));
            tr.strike = isYes(r.attribute(QStringLiteral("Strikethrough")));
            tr.link = r.attribute(QStringLiteral("Link"));
            if (tr.length > 0)
                n.ranges << tr;
        }
        const QDomElement img = e.firstChildElement(QStringLiteral("Image"));
        if (!img.isNull()) {
            n.image = parseImage(img.text());
            if (!n.image.isNull() && n.height <= 0)
                n.height = n.width * n.image.height() / qMax(1, n.image.width());
        }
        n.connected = parseIds(e.firstChildElement(QStringLiteral("ConnectedNoteIDs")).text());
        n.pointsTo = parseIds(e.firstChildElement(QStringLiteral("PointsToNoteIDs")).text());
        n.icons = readIcons(e);
        ids.insert(n.id);
        notes << n;
    }
    const QDomElement bs = root.firstChildElement(QStringLiteral("BackgroundShapes"));
    for (QDomElement e = bs.firstChildElement(QStringLiteral("Shape")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Shape"))) {
        Shape s;
        s.id = e.attribute(QStringLiteral("ID")).toInt();
        if (ids.contains(s.id))
            s.id = -1; // given a free id below
        bool ok = false;
        const QPointF p = parsePoint(e.attribute(QStringLiteral("Position")), &ok);
        s.rect = QRectF(p, QSizeF(qMax(10.0, e.attribute(QStringLiteral("Width"), QStringLiteral("100")).toDouble()),
                                  qMax(10.0, e.attribute(QStringLiteral("Height"), QStringLiteral("100")).toDouble())));
        s.look.borderWeight = 1;
        readAppearance(e.firstChildElement(QStringLiteral("Appearance")), s.look, nullptr, nullptr, nullptr, nullptr, nullptr);
        const QDomElement mag = e.firstChildElement(QStringLiteral("Magnetic"));
        s.magnetic = mag.isNull() ? true : isYes(mag.text());
        s.connected = parseIds(e.firstChildElement(QStringLiteral("ConnectedNoteIDs")).text());
        s.pointsTo = parseIds(e.firstChildElement(QStringLiteral("PointsToNoteIDs")).text());
        s.icons = readIcons(e);
        shapes << s;
    }
    for (Shape &s : shapes)
        if (s.id < 0) {
            s.id = nextId();
        }
    // ids from 1 on (Scapple counts from 0): every id moves up together with the lines and labels
    {
        int lowest = 1;
        for (const Note &n : notes)
            lowest = qMin(lowest, n.id);
        for (const Shape &s : shapes)
            lowest = qMin(lowest, s.id);
        if (lowest < 1) {
            const int shift = 1 - lowest;
            auto moved = [shift](const QSet<int> &in) {
                QSet<int> out;
                for (int v : in)
                    out.insert(v + shift);
                return out;
            };
            for (Note &n : notes) {
                n.id += shift;
                n.connected = moved(n.connected);
                n.pointsTo = moved(n.pointsTo);
            }
            for (Shape &s : shapes) {
                s.id += shift;
                s.connected = moved(s.connected);
                s.pointsTo = moved(s.pointsTo);
            }
            idShift = shift;
        }
    }
    // lines must name items that exist and be listed at both ends
    for (Note &n : notes) {
        for (int other : n.connected.values())
            if (!exists(other))
                n.connected.remove(other);
        for (int other : n.pointsTo.values())
            if (!exists(other))
                n.pointsTo.remove(other);
            else
                n.connected.insert(other);
    }
    for (Shape &s : shapes) {
        for (int other : s.connected.values())
            if (!exists(other))
                s.connected.remove(other);
        for (int other : s.pointsTo.values())
            if (!exists(other))
                s.pointsTo.remove(other);
            else
                s.connected.insert(other);
    }
    QList<std::pair<int, int>> pairs;
    for (const Note &n : notes)
        for (int o : n.connected)
            pairs.append({n.id, o});
    for (const Shape &s : shapes)
        for (int o : s.connected)
            pairs.append({s.id, o});
    for (auto [a, b] : pairs)
        if (QSet<int> *c = connSet(*this, b))
            c->insert(a);

    const QDomElement ls = root.firstChildElement(QStringLiteral("ConnectionLabels"));
    for (QDomElement e = ls.firstChildElement(QStringLiteral("Label")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Label"))) {
        const QList<int> pair = parseIds(e.attribute(QStringLiteral("NoteIDs"))).values();
        if (pair.size() == 2 && isConnected(pair[0] + idShift, pair[1] + idShift))
            setLineLabel(pair[0] + idShift, pair[1] + idShift, e.text());
    }
    const QDomElement lk = root.firstChildElement(QStringLiteral("ConnectionLooks"));
    for (QDomElement e = lk.firstChildElement(QStringLiteral("Look")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Look"))) {
        const QList<int> pair = parseIds(e.attribute(QStringLiteral("NoteIDs"))).values();
        if (pair.size() != 2 || !isConnected(pair[0] + idShift, pair[1] + idShift))
            continue;
        LineLook look;
        if (e.hasAttribute(QStringLiteral("Color")))
            look.color = parseColor(e.attribute(QStringLiteral("Color")));
        look.icon = e.attribute(QStringLiteral("Icon"));
        setLineLook(pair[0] + idShift, pair[1] + idShift, look);
    }

    const QDomElement st = root.firstChildElement(QStringLiteral("NoteStyles"));
    for (QDomElement e = st.firstChildElement(QStringLiteral("Style")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Style"))) {
        NoteStyle s;
        s.name = e.attribute(QStringLiteral("Name"));
        s.id = e.attribute(QStringLiteral("ID"), QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper());
        auto has = [&](const char *n) { return !e.firstChildElement(QLatin1String(n)).isNull(); };
        auto txt = [&](const char *n) { return e.firstChildElement(QLatin1String(n)).text(); };
        auto aff = [&](const char *attr, bool fallback) {
            return e.hasAttribute(QLatin1String(attr)) ? isYes(e.attribute(QLatin1String(attr))) : fallback;
        };
        s.affectsFont = aff("AffectFontStyle", has("FontSize") || has("IsBold") || has("IsItalic"));
        s.affectsAlignment = aff("AffectAlignment", has("Alignment"));
        s.affectsTextColor = aff("AffectTextColor", has("TextColor"));
        s.affectsBody = aff("AffectNoteBody", has("FillColor") || has("BorderThickness") || has("BorderColor"));
        s.affectsSize = aff("AffectSize", has("Width"));
        s.affectsFade = aff("AffectFade", has("Faded"));
        s.affectsMagnetic = aff("AffectSticky", has("Magnetic"));
        if (has("FontSize"))
            s.fontSize = txt("FontSize").toDouble();
        s.bold = isYes(txt("IsBold"));
        s.italic = isYes(txt("IsItalic"));
        s.underline = isYes(txt("IsUnderlined"));
        s.strike = isYes(txt("IsStrikethrough"));
        if (has("Alignment"))
            s.align = parseAlign(txt("Alignment"));
        s.indent = txt("Indent").toInt();
        s.look.textColor = parseColor(txt("TextColor"));
        s.look.fill = parseColor(txt("FillColor"));
        s.look.borderColor = parseColor(txt("BorderColor"));
        s.look.borderWeight = qBound(0, txt("BorderThickness").toInt(), 5);
        if (has("BorderStyle"))
            s.look.borderStyle = parseBorder(txt("BorderStyle"));
        s.look.shadow = isYes(txt("Shadow"));
        s.look.faded = isYes(txt("Faded"));
        s.width = txt("Width").toDouble();
        s.height = txt("Height").toDouble();
        s.magnetic = isYes(txt("Magnetic"));
        if (!s.name.isEmpty())
            styles << s;
    }

    const QDomElement ui = root.firstChildElement(QStringLiteral("UISettings"));
    QString bg = ui.firstChildElement(QStringLiteral("BackgroundColor")).text();
    if (bg.isEmpty())
        bg = root.firstChildElement(QStringLiteral("BackgroundColor")).text();
    if (const QColor c = parseColor(bg); c.isValid())
        settings.background = c;
    const QDomElement own = ui.firstChildElement(QStringLiteral("QuillbinderBackground"));
    settings.legacyBackground = QColor();
    if (!own.isNull()) {
        settings.ownLight = parseColor(own.attribute(QStringLiteral("Light")));
        settings.ownDark = parseColor(own.attribute(QStringLiteral("Dark")));
    } else {
        settings.ownLight = settings.ownDark = QColor();
        settings.legacyBackground = parseColor(bg);
    }
    settings.texture = parseImage(ui.firstChildElement(QStringLiteral("BackgroundTexture")).text());
    QString tc = ui.firstChildElement(QStringLiteral("DefaultTextColor")).text();
    if (tc.isEmpty())
        tc = root.firstChildElement(QStringLiteral("DefaultTextColor")).text();
    settings.textColor = parseColor(tc);
    settings.font = ui.firstChildElement(QStringLiteral("DefaultFont")).text().trimmed();
    if (!settings.font.isEmpty() && !QFontDatabase::families().contains(settings.font, Qt::CaseInsensitive))
        ; // kept: the board names its font; Qt shows a fallback until it is installed
    settings.rightToLeft = ui.firstChildElement(QStringLiteral("WritingDirection")).text().trimmed() == QLatin1String("RightToLeft");
    settings.locked = isYes(ui.firstChildElement(QStringLiteral("PreventEditing")).text());
    settings.footer = !(ui.firstChildElement(QStringLiteral("ShowFooter")).text().trimmed() == QLatin1String("No"));
    settings.pageGuides = isYes(ui.firstChildElement(QStringLiteral("PageGuides")).text());
    return true;
}

bool Board::save(const QString &path, QString *error) const
{
    if (!worthSaving()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error)
                *error = QObject::tr("Cannot remove %1").arg(path);
            return false;
        }
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    f.write(text::xmlSafeBytes(toXml()));
    if (!f.commit()) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

bool Board::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read %1").arg(path);
        return false;
    }
    return fromXml(f.readAll(), error);
}

QStringList splitText(const QString &text, const QString &separator)
{
    QString t = text;
    t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (separator.isEmpty())
        return {t.trimmed()};
    QString sep = separator;
    sep.replace(QLatin1String("\\n"), QLatin1String("\n"));
    QStringList out;
    for (const QString &p : t.split(sep))
        if (!p.trimmed().isEmpty())
            out << p.trimmed();
    return out;
}

} // namespace mindmap
} // namespace qb
