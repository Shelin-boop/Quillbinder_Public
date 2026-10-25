#include "ui/ViewMemory.h"
#include "core/Project.h"

#include <QCryptographicHash>
#include <QStringList>
#include <algorithm>

namespace qb {
namespace viewmem {

namespace {
const QString kText = QStringLiteral("textPlaces");
const QString kBoards = QStringLiteral("mindmapViews");
const QString kMedia = QStringLiteral("mediaPlaces");
const QString kInspector = QStringLiteral("inspectorState");
}

QString groupKey(const QStringList &ids)
{
    if (ids.size() == 1)
        return ids.first();
    // a folder in combined text: the documents shown, as a short key
    const QByteArray h = QCryptographicHash::hash(ids.join(QLatin1Char(',')).toUtf8(), QCryptographicHash::Sha1);
    return QStringLiteral("group:") + QString::fromLatin1(h.toHex().left(16));
}

TextPlace textPlace(Project *p, const QString &key)
{
    TextPlace t;
    if (!p || key.isEmpty())
        return t;
    const QVariantMap m = p->uiState.value(kText).toMap().value(key).toMap();
    if (m.isEmpty())
        return t;
    t.cursor = m.value(QStringLiteral("c"), -1).toInt();
    t.top = m.value(QStringLiteral("t"), -1).toInt();
    t.topItem = m.value(QStringLiteral("ti")).toString();
    t.cursorItem = m.value(QStringLiteral("ci")).toString();
    return t;
}

void setTextPlace(Project *p, const QString &key, const TextPlace &place)
{
    if (!p || key.isEmpty() || !place.valid())
        return;
    QVariantMap all = p->uiState.value(kText).toMap();
    const int seq = p->uiState.value(QStringLiteral("textPlacesSeq"), 0).toInt() + 1;
    p->uiState[QStringLiteral("textPlacesSeq")] = seq;
    QVariantMap m;
    m[QStringLiteral("c")] = place.cursor;
    m[QStringLiteral("t")] = place.top;
    if (!place.topItem.isEmpty())
        m[QStringLiteral("ti")] = place.topItem;
    if (!place.cursorItem.isEmpty())
        m[QStringLiteral("ci")] = place.cursorItem;
    m[QStringLiteral("n")] = seq;
    all[key] = m;
    if (all.size() > kMaxTextPlaces) {
        // the places used longest ago go
        QList<QPair<int, QString>> order;
        for (auto it = all.cbegin(); it != all.cend(); ++it)
            order.append({it.value().toMap().value(QStringLiteral("n")).toInt(), it.key()});
        std::sort(order.begin(), order.end());
        for (int i = 0; i < order.size() - kMaxTextPlaces; ++i)
            all.remove(order[i].second);
    }
    p->uiState[kText] = all;
}

BoardView boardView(Project *p, const QString &itemId, const QString &kind)
{
    BoardView v;
    if (!p || itemId.isEmpty())
        return v;
    const QVariantMap m = p->uiState.value(kBoards).toMap().value(itemId).toMap().value(kind).toMap();
    v.zoom = m.value(QStringLiteral("z"), 0.0).toDouble();
    v.center = QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble());
    return v;
}

void setBoardView(Project *p, const QString &itemId, const QString &kind, const BoardView &v)
{
    if (!p || itemId.isEmpty() || !v.valid())
        return;
    QVariantMap all = p->uiState.value(kBoards).toMap();
    QVariantMap board = all.value(itemId).toMap();
    board[kind] = QVariantMap{{QStringLiteral("z"), v.zoom}, {QStringLiteral("x"), v.center.x()}, {QStringLiteral("y"), v.center.y()}};
    all[itemId] = board;
    p->uiState[kBoards] = all;
}

MediaPlace mediaPlace(Project *p, const QString &itemId)
{
    MediaPlace m;
    if (!p || itemId.isEmpty())
        return m;
    const QVariantMap v = p->uiState.value(kMedia).toMap().value(itemId).toMap();
    if (v.isEmpty())
        return m;
    m.zoom = v.value(QStringLiteral("z"), -1.0).toDouble();
    m.page = v.value(QStringLiteral("p"), -1).toInt();
    m.pdfFitWidth = v.value(QStringLiteral("fw"), false).toBool();
    return m;
}

void setMediaPlace(Project *p, const QString &itemId, const MediaPlace &m)
{
    if (!p || itemId.isEmpty())
        return;
    QVariantMap all = p->uiState.value(kMedia).toMap();
    QVariantMap v;
    v[QStringLiteral("z")] = m.zoom;
    if (m.page >= 0)
        v[QStringLiteral("p")] = m.page;
    if (m.pdfFitWidth)
        v[QStringLiteral("fw")] = true;
    all[itemId] = v;
    p->uiState[kMedia] = all;
}

QVariantMap inspectorState(Project *p) { return p ? p->uiState.value(kInspector).toMap() : QVariantMap(); }

void setInspectorState(Project *p, const QVariantMap &state)
{
    if (p)
        p->uiState[kInspector] = state;
}

} // namespace viewmem
} // namespace qb
