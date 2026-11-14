#include "ui/BarPlaces.h"

#include <QtGlobal>

namespace qb {
namespace barplaces {

Places fromRows(const QList<QStringList> &topRows, const QList<QStringList> &bottomRows)
{
    Places p;
    auto fill = [](const QList<QStringList> &from, QList<Row> *to) {
        for (const QStringList &names : from) {
            Row row;
            for (const QString &n : names)
                row << Slot{n, 0};
            if (!row.isEmpty())
                *to << row;
        }
    };
    fill(topRows, &p.top);
    fill(bottomRows, &p.bottom);
    return p;
}

QStringList toText(const Places &p)
{
    QStringList lines;
    auto write = [&lines](const QList<Row> &rows, const QString &edge) {
        for (const Row &row : rows) {
            QStringList parts;
            for (const Slot &s : row)
                parts << s.name + QLatin1Char(':') + QString::number(s.lead);
            lines << edge + QLatin1Char('|') + parts.join(QLatin1Char(' '));
        }
    };
    write(p.top, QStringLiteral("top"));
    write(p.bottom, QStringLiteral("bottom"));
    return lines;
}

Places fromText(const QStringList &lines)
{
    Places p;
    for (const QString &line : lines) {
        const int bar = line.indexOf(QLatin1Char('|'));
        if (bar < 0)
            continue;
        const QString edge = line.left(bar).trimmed();
        if (edge != QLatin1String("top") && edge != QLatin1String("bottom"))
            continue;
        Row row;
        // (toolbars are set apart by spaces; commas are read too)
        QString rest = line.mid(bar + 1);
        rest.replace(QLatin1Char(','), QLatin1Char(' '));
        const QStringList parts = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const int colon = part.lastIndexOf(QLatin1Char(':'));
            Slot s;
            s.name = (colon < 0 ? part : part.left(colon)).trimmed();
            s.lead = colon < 0 ? 0 : part.mid(colon + 1).trimmed().toInt();
            if (!s.name.isEmpty())
                row << s;
        }
        (edge == QLatin1String("top") ? p.top : p.bottom) << row; // (an empty row goes in normalized())
    }
    return p;
}

QString describe(const Places &p)
{
    auto rows = [](const QList<Row> &list) {
        QStringList out;
        for (const Row &row : list) {
            QStringList parts;
            for (const Slot &s : row) {
                if (s.lead > 0)
                    parts << QLatin1Char('+') + QString::number(s.lead);
                parts << s.name;
            }
            out << parts.join(QLatin1Char(' '));
        }
        return out.join(QLatin1String(" / "));
    };
    QString s = QLatin1String("top: ") + rows(p.top);
    if (!p.bottom.isEmpty())
        s += QLatin1String("; bottom: ") + rows(p.bottom);
    return s;
}

Where find(const Places &p, const QString &name)
{
    for (int edge = 0; edge < 2; ++edge) {
        const QList<Row> &rows = edge ? p.bottom : p.top;
        for (int r = 0; r < rows.size(); ++r)
            for (int i = 0; i < rows[r].size(); ++i)
                if (rows[r][i].name == name)
                    return {true, edge == 1, r, i};
    }
    return {};
}

namespace {

// no empty rows, and no free space in front of a row's first toolbar
void tidy(Places *p)
{
    for (QList<Row> *rows : {&p->top, &p->bottom}) {
        for (int r = rows->size() - 1; r >= 0; --r) {
            if ((*rows)[r].isEmpty())
                rows->removeAt(r);
        }
        for (Row &row : *rows) {
            for (Slot &s : row)
                s.lead = qBound(0, s.lead, kMaxLead);
            row[0].lead = 0;
        }
    }
}

} // namespace

Places normalized(const Places &p, const Places &standard)
{
    QStringList known; // in the standard order
    for (const QList<Row> *rows : {&standard.top, &standard.bottom})
        for (const Row &row : *rows)
            for (const Slot &s : row)
                known << s.name;
    Places out;
    QSet<QString> seen;
    for (int edge = 0; edge < 2; ++edge) {
        const QList<Row> &from = edge ? p.bottom : p.top;
        QList<Row> &to = edge ? out.bottom : out.top;
        for (const Row &row : from) {
            Row kept;
            for (const Slot &s : row)
                if (known.contains(s.name) && !seen.contains(s.name)) {
                    seen.insert(s.name);
                    kept << s;
                }
            to << kept;
        }
    }
    // a toolbar that is missing: behind the one before it in the standard places; the first of a standard row that
    // has no neighbor yet opens a row of its own at that edge
    for (int edge = 0; edge < 2; ++edge) {
        const QList<Row> &rows = edge ? standard.bottom : standard.top;
        for (const Row &row : rows) {
            for (int i = 0; i < row.size(); ++i) {
                const QString &name = row[i].name;
                if (seen.contains(name))
                    continue;
                seen.insert(name);
                bool placed = false;
                // the nearest standard neighbor that has a place: the ones before it first, then the ones after it
                for (int k = i - 1; k >= 0 && !placed; --k) {
                    const Where w = find(out, row[k].name);
                    if (w.found) {
                        (w.bottom ? out.bottom : out.top)[w.row].insert(w.index + 1, Slot{name, 0});
                        placed = true;
                    }
                }
                for (int k = i + 1; k < row.size() && !placed; ++k) {
                    const Where w = find(out, row[k].name);
                    if (w.found) {
                        (w.bottom ? out.bottom : out.top)[w.row].insert(w.index, Slot{name, 0});
                        placed = true;
                    }
                }
                if (!placed)
                    (edge ? out.bottom : out.top) << Row{Slot{name, 0}};
            }
        }
    }
    tidy(&out);
    return out;
}

Places moved(const Places &p, const QString &name, const Target &to, const QSet<QString> &hidden)
{
    const Where from = find(p, name);
    if (!from.found || !to.valid)
        return p;
    Places out = p;
    // mark the old slot (it is taken out after the toolbar has its new place, so the rows keep their numbers)
    Row &old = (from.bottom ? out.bottom : out.top)[from.row];
    const int freed = old[from.index].lead;
    old[from.index].name.clear();
    // free space that was in front of it stays: the next toolbar of the row that shows takes it over
    for (int i = from.index + 1; i < old.size(); ++i)
        if (!hidden.contains(old[i].name)) {
            old[i].lead += freed;
            break;
        }
    QList<Row> &rows = to.bottom ? out.bottom : out.top;
    if (to.newRow) {
        rows.insert(qBound(0, to.row, int(rows.size())), Row{Slot{name, 0}});
    } else if (rows.isEmpty()) {
        rows << Row{Slot{name, 0}};
    } else {
        Row &row = rows[qBound(0, to.row, int(rows.size()) - 1)];
        int at = row.size();
        for (int i = 0; i < row.size(); ++i)
            if (!to.before.isEmpty() && row[i].name == to.before) {
                at = i;
                break;
            }
        row.insert(at, Slot{name, 0});
    }
    for (QList<Row> *list : {&out.top, &out.bottom})
        for (Row &row : *list)
            for (int i = row.size() - 1; i >= 0; --i)
                if (row[i].name.isEmpty())
                    row.removeAt(i);
    tidy(&out);
    return out;
}

QList<int> lefts(const QList<Shown> &row)
{
    QList<int> x;
    int at = 0;
    for (int i = 0; i < row.size(); ++i) {
        at += i == 0 ? 0 : row[i].lead;
        x << at;
        at += row[i].width;
    }
    return x;
}

QList<int> fitted(const QList<Shown> &row, int rowWidth, int minLead)
{
    QList<int> leads;
    qint64 widths = 0, wanted = 0;
    for (int i = 0; i < row.size(); ++i) {
        widths += row[i].width;
        const int lead = i == 0 ? 0 : qBound(0, row[i].lead, kMaxLead);
        leads << lead;
        wanted += lead;
    }
    const qint64 room = qMax<qint64>(0, rowWidth - widths);
    if (wanted > room)
        for (int &lead : leads)
            lead = int(lead * room / wanted);
    for (int &lead : leads)
        if (lead <= minLead)
            lead = 0;
    return leads;
}

QList<int> shifted(const QList<Shown> &row, int rowWidth, int index, int left, int minLead)
{
    QList<int> leads;
    for (int i = 0; i < row.size(); ++i)
        leads << (i == 0 ? 0 : qMax(0, row[i].lead));
    if (index <= 0 || index >= row.size())
        return leads; // the row's first toolbar stays at the edge
    const QList<int> x = lefts(row);
    const int prevEnd = x[index - 1] + row[index - 1].width;
    int after = 0; // what the toolbars behind it need when they stand together at the row's end
    for (int i = index + 1; i < row.size(); ++i)
        after += row[i].width;
    const int maxLeft = qMax(prevEnd, rowWidth - row[index].width - after);
    int at = qBound(prevEnd, left, maxLeft);
    if (at - prevEnd <= minLead)
        at = prevEnd;
    leads[index] = at - prevEnd;
    // the toolbars after it stay where they were unless it reaches them
    int end = at + row[index].width;
    for (int i = index + 1; i < row.size(); ++i) {
        int lead = qMax(x[i], end) - end;
        if (lead <= minLead)
            lead = 0;
        leads[i] = lead;
        end += lead + row[i].width;
    }
    return leads;
}

} // namespace barplaces
} // namespace qb
