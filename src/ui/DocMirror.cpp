#include "ui/DocMirror.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QUrl>
#include <QVariant>

namespace qb {

namespace {

// The character formats of a span as runs: [start, end) with their properties.
struct Run {
    int start, end;
    QMap<int, QVariant> props;
};

QList<Run> runsOf(QTextDocument *d, int from, int to)
{
    QList<Run> runs;
    for (QTextBlock b = d->findBlock(from); b.isValid() && b.position() <= to; b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const int s = qMax(from, f.position()), e = qMin(to, f.position() + f.length());
            if (s >= e)
                continue;
            const QMap<int, QVariant> p = f.charFormat().properties();
            if (!runs.isEmpty() && runs.last().end == s && runs.last().props == p)
                runs.last().end = e;
            else
                runs.append({s, e, p});
        }
    }
    return runs;
}

// A paragraph's format without the number of its list: the list objects of two
// documents have numbers of their own (the list is compared by its format).
QMap<int, QVariant> paraProps(const QTextBlock &b)
{
    QMap<int, QVariant> p = b.blockFormat().properties();
    p.remove(QTextFormat::ObjectIndex);
    return p;
}

QTextBlockFormat withoutList(QTextBlockFormat f)
{
    f.clearProperty(QTextFormat::ObjectIndex);
    return f;
}

QTextListFormat listFormat(const QTextList *l)
{
    QTextListFormat f = l->format();
    f.clearProperty(QTextFormat::ObjectIndex);
    return f;
}

bool sameBlockFormats(QTextDocument *a, QTextDocument *b, int from, int to)
{
    QTextBlock x = a->findBlock(from), y = b->findBlock(from);
    for (; x.isValid() && x.position() <= to; x = x.next(), y = y.next()) {
        if (!y.isValid() || x.position() != y.position())
            return false;
        if (paraProps(x) != paraProps(y) || x.charFormat().properties() != y.charFormat().properties())
            return false;
        if (bool(x.textList()) != bool(y.textList()))
            return false;
        if (x.textList() && listFormat(x.textList()).properties() != listFormat(y.textList()).properties())
            return false;
    }
    return true;
}

} // namespace

bool sameContents(QTextDocument *a, QTextDocument *b, int from, int to)
{
    if (!a || !b)
        return false;
    const int lenA = a->characterCount(), lenB = b->characterCount();
    if (to < 0) {
        if (lenA != lenB)
            return false;
        to = lenA - 1;
    }
    from = qMax(0, from);
    to = qMin(to, qMin(lenA, lenB) - 1);
    if (to < from)
        return lenA == lenB;
    QTextCursor ca(a), cb(b);
    ca.setPosition(from);
    ca.setPosition(to, QTextCursor::KeepAnchor);
    cb.setPosition(from);
    cb.setPosition(to, QTextCursor::KeepAnchor);
    if (ca.selectedText() != cb.selectedText())
        return false;
    const QList<Run> ra = runsOf(a, from, to), rb = runsOf(b, from, to);
    if (ra.size() != rb.size())
        return false;
    for (int i = 0; i < ra.size(); ++i)
        if (ra[i].start != rb[i].start || ra[i].end != rb[i].end || ra[i].props != rb[i].props)
            return false;
    return sameBlockFormats(a, b, from, to);
}

DocMirror::DocMirror(QTextDocument *source, QTextDocument *copy, QObject *parent)
    : QObject(parent), m_source(source), m_copy(copy)
{
    connect(source, &QTextDocument::contentsChange, this,
            [this](int pos, int removed, int added) { changed(m_source, pos, removed, added); });
    connect(copy, &QTextDocument::contentsChange, this,
            [this](int pos, int removed, int added) { changed(m_copy, pos, removed, added); });
}

DocMirror::~DocMirror() = default;

void DocMirror::changed(QTextDocument *from, int pos, int removed, int added)
{
    // (what was taken out is worked out from the lengths: Qt sometimes counts the last paragraph mark in)
    if (m_busy || !m_source || !m_copy)
        return;
    QTextDocument *to = from == m_source.data() ? m_copy.data() : m_source.data();
    const int fromLen = from->characterCount(), toLen = to->characterCount();
    // A document that gets its layout (an editor takes it) announces all its text as
    // added, without any change of length: no edit (copying then would even run inside
    // the layout of the other document).
    if (fromLen == toLen && ((removed == 0 && added > 0) || (added == 0 && removed > 0)))
        return;
    if (pos < 0 || pos >= fromLen || pos >= toLen) {
        rebuildCopy();
        return;
    }
    added = qBound(0, added, fromLen - 1 - pos);
    // `to` still holds what `from` held before: what was taken out follows from the lengths
    const int taken = toLen - fromLen + added;
    if (taken < 0 || pos + taken > toLen - 1) {
        rebuildCopy();
        return;
    }
    if (taken == added && sameContents(from, to, pos, pos + added))
        return; // nothing changed that is stored (highlighting, layout)
    if (qEnvironmentVariableIsSet("QB_MIRROR_DEBUG"))
        qInfo("mirror %s pos=%d removed=%d added=%d fromLen=%d toLen=%d taken=%d", from == m_source.data() ? "doc->copy" : "copy->doc", pos,
              removed, added, fromLen, toLen, taken);
    m_busy = true;
    QTextCursor tc(to);
    tc.beginEditBlock();
    if (taken > 0) {
        tc.setPosition(pos);
        tc.setPosition(pos + taken, QTextCursor::KeepAnchor);
        tc.removeSelectedText();
    }
    // The changed span of `from`, written into `to` piece by piece: the text of each
    // fragment with its character format, a new paragraph for each paragraph mark.
    // (QTextCursor::insertFragment sometimes adds a paragraph of its own when the
    // piece starts with a list item; seen in the checks.)
    const int end = pos + added;
    int at = pos;
    for (QTextBlock fb = from->findBlock(pos); fb.isValid() && fb.position() <= end && added > 0; fb = fb.next()) {
        for (auto it = fb.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const int s = qMax(pos, fr.position()), e = qMin(end, fr.position() + fr.length());
            if (s >= e)
                continue;
            const QString piece = fr.text().mid(s - fr.position(), e - s);
            tc.setPosition(at);
            tc.insertText(piece, fr.charFormat());
            at += int(piece.size());
        }
        const int mark = fb.position() + fb.length() - 1; // the paragraph mark of this block
        if (mark >= pos && mark < end) {
            const QTextBlock next = fb.next();
            tc.setPosition(at);
            tc.insertBlock(next.isValid() ? withoutList(next.blockFormat()) : QTextBlockFormat(),
                           next.isValid() ? next.charFormat() : QTextCharFormat());
            at += 1;
        }
    }
    // The paragraphs of the span get the formatting and the lists they have in `from`.
    if (to->characterCount() == fromLen) {
        QTextBlock fb = from->findBlock(pos);
        const int end = pos + added;
        for (; fb.isValid() && fb.position() <= end; fb = fb.next()) {
            QTextBlock tb = to->findBlock(fb.position());
            if (!tb.isValid() || tb.position() != fb.position())
                break;
            QTextCursor bc(tb);
            if (tb.charFormat().properties() != fb.charFormat().properties())
                bc.setBlockCharFormat(fb.charFormat());
            // the list first (taking a paragraph out of a list adds the list's indent
            // to the paragraph: the paragraph format is set afterwards)
            QTextList *fl = fb.textList();
            QTextList *tl = tb.textList();
            const QTextBlock prevT = tb.previous(), prevF = fb.previous();
            const bool sameAsBefore = prevT.isValid() && prevT.textList() && prevF.isValid() && prevF.textList() == fl;
            if (fl && !tl) {
                // join the list of the paragraph before when it is the same list there
                if (sameAsBefore)
                    prevT.textList()->add(tb);
                else
                    bc.createList(listFormat(fl));
            } else if (fl && tl && sameAsBefore && prevT.textList() != tl) {
                // the piece came with a list of its own: one list, as in `from`
                tl->remove(tb);
                prevT.textList()->add(tb);
            } else if (fl && tl && listFormat(tl).properties() != listFormat(fl).properties()) {
                tl->setFormat(listFormat(fl));
            } else if (!fl && tl) {
                tl->remove(tb);
            }
            tb = to->findBlock(fb.position());
            if (paraProps(tb) != paraProps(fb)) {
                // the format of `from`, in the list `tb` is in now
                QTextBlockFormat want = withoutList(fb.blockFormat());
                if (tb.textList())
                    want.setObjectIndex(tb.blockFormat().objectIndex());
                QTextCursor(tb).setBlockFormat(want);
            }
            // pictures: the copy must know them as well
            for (auto it = fb.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat cf = it.fragment().charFormat();
                if (!cf.isImageFormat())
                    continue;
                const QUrl name(cf.toImageFormat().name());
                if (!to->resource(QTextDocument::ImageResource, name).isValid()) {
                    const QVariant img = from->resource(QTextDocument::ImageResource, name);
                    if (img.isValid())
                        to->addResource(QTextDocument::ImageResource, name, img);
                }
            }
        }
    }
    tc.endEditBlock();
    m_busy = false;
    if (to == m_source.data())
        ++m_toSource;
    else
        ++m_toCopy;
    if (to->characterCount() != from->characterCount() || !sameContents(from, to, qMax(0, pos - 1), qMin(fromLen - 1, end + 1))) {
        if (qEnvironmentVariableIsSet("QB_MIRROR_DEBUG")) {
            qInfo("mirror: not the same after copying (%d/%d), the copy is made anew", from->characterCount(), to->characterCount());
            auto dump = [](QTextDocument *d, int a, int z) {
                for (QTextBlock b = d->findBlock(a); b.isValid() && b.position() <= z; b = b.next()) {
                    QStringList keys;
                    const auto pp = paraProps(b);
                    for (auto k = pp.cbegin(); k != pp.cend(); ++k)
                        keys << QStringLiteral("%1=%2").arg(k.key(), 0, 16).arg(k.value().toString());
                    qInfo("  block %d len %d list=%d bf=%s bcf=%d", b.position(), b.length(), b.textList() ? 1 : 0, qPrintable(keys.join(QLatin1Char(' '))),
                          int(b.charFormat().properties().size()));
                    for (auto it = b.begin(); !it.atEnd(); ++it)
                        qInfo("    frag %d '%s' props=%d size=%.1f", it.fragment().position(), qPrintable(it.fragment().text().left(20)),
                              int(it.fragment().charFormat().properties().size()), it.fragment().charFormat().fontPointSize());
                }
            };
            qInfo(" from:");
            dump(from, qMax(0, pos - 1), qMin(fromLen - 1, end + 1));
            qInfo(" to:");
            dump(to, qMax(0, pos - 1), qMin(fromLen - 1, end + 1));
        }
        rebuildCopy();
    }
}

void DocMirror::rebuildCopy()
{
    if (!m_source || !m_copy)
        return;
    QTextDocument *old = m_copy;
    QTextDocument *fresh = m_source->clone(old->parent());
    fresh->setDefaultFont(old->defaultFont());
    fresh->setProperty("qbZoom", old->property("qbZoom"));
    disconnect(old, nullptr, this, nullptr);
    m_copy = fresh;
    connect(fresh, &QTextDocument::contentsChange, this,
            [this](int pos, int removed, int added) { changed(m_copy, pos, removed, added); });
    emit copyReplaced(fresh);
}

} // namespace qb
