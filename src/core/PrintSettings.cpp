#include "core/PrintSettings.h"

#include "core/DocumentStore.h"
#include "core/Project.h"

namespace qb {

namespace {
QVariantMap *g_standard = nullptr;
const QString kKey = QStringLiteral("printSettings");
} // namespace

QList<PrintSettings::CardSize> PrintSettings::cardSizes()
{
    return {{QStringLiteral("3 x 5 in"), 127, 76.2},
            {QStringLiteral("4 x 6 in"), 152.4, 101.6},
            {QStringLiteral("5 x 8 in"), 203.2, 127},
            {QStringLiteral("A7"), 105, 74},
            {QStringLiteral("A6"), 148, 105}};
}

QVariantMap PrintSettings::toVariant() const
{
    QVariantMap m;
    m[QStringLiteral("page")] = page.toVariant();
    m[QStringLiteral("headerLeft")] = headerLeft;
    m[QStringLiteral("headerCenter")] = headerCenter;
    m[QStringLiteral("headerRight")] = headerRight;
    m[QStringLiteral("footerLeft")] = footerLeft;
    m[QStringLiteral("footerCenter")] = footerCenter;
    m[QStringLiteral("footerRight")] = footerRight;
    m[QStringLiteral("headerOnFirstPage")] = headerOnFirstPage;
    m[QStringLiteral("titles")] = titles;
    m[QStringLiteral("synopses")] = synopses;
    m[QStringLiteral("notes")] = notes;
    m[QStringLiteral("comments")] = comments;
    m[QStringLiteral("endnotes")] = endnotes;
    m[QStringLiteral("highlights")] = highlights;
    m[QStringLiteral("editorFont")] = editorFont;
    m[QStringLiteral("fontFamily")] = fontFamily;
    m[QStringLiteral("fontSize")] = fontSize;
    m[QStringLiteral("lineSpacing")] = lineSpacing;
    m[QStringLiteral("cardSize")] = cardSize;
    m[QStringLiteral("cardWidth")] = cardWidth;
    m[QStringLiteral("cardHeight")] = cardHeight;
    m[QStringLiteral("cardPerPage")] = cardPerPage;
    m[QStringLiteral("cardTitle")] = cardTitle;
    m[QStringLiteral("cardSynopsis")] = cardSynopsis;
    m[QStringLiteral("cardTextIfEmpty")] = cardTextIfEmpty;
    m[QStringLiteral("cardLabel")] = cardLabel;
    m[QStringLiteral("cardStatus")] = cardStatus;
    m[QStringLiteral("cardKeywords")] = cardKeywords;
    m[QStringLiteral("cardPictures")] = cardPictures;
    m[QStringLiteral("cardRuled")] = cardRuled;
    m[QStringLiteral("cardBorder")] = cardBorder;
    m[QStringLiteral("cardFont")] = cardFont;
    m[QStringLiteral("cardFontSize")] = cardFontSize;
    m[QStringLiteral("outlinerSynopses")] = outlinerSynopses;
    m[QStringLiteral("outlinerAllRows")] = outlinerAllRows;
    m[QStringLiteral("outlinerHeader")] = outlinerHeader;
    m[QStringLiteral("outlinerLines")] = outlinerLines;
    m[QStringLiteral("outlinerLabels")] = outlinerLabels;
    m[QStringLiteral("outlinerFont")] = outlinerFont;
    m[QStringLiteral("outlinerFontSize")] = outlinerFontSize;
    return m;
}

PrintSettings PrintSettings::fromVariant(const QVariantMap &m, const PrintSettings &fallback)
{
    PrintSettings s = fallback;
    if (m.isEmpty())
        return s;
    auto str = [&m](const char *k, QString &v) {
        const auto it = m.constFind(QLatin1String(k));
        if (it != m.constEnd())
            v = it.value().toString();
    };
    auto flag = [&m](const char *k, bool &v) {
        const auto it = m.constFind(QLatin1String(k));
        if (it != m.constEnd())
            v = it.value().toBool();
    };
    auto num = [&m](const char *k, double &v, double lo, double hi) {
        const auto it = m.constFind(QLatin1String(k));
        if (it != m.constEnd())
            v = qBound(lo, it.value().toDouble(), hi);
    };
    s.page = PageSetup::fromVariant(m.value(QStringLiteral("page")).toMap(), fallback.page);
    str("headerLeft", s.headerLeft);
    str("headerCenter", s.headerCenter);
    str("headerRight", s.headerRight);
    str("footerLeft", s.footerLeft);
    str("footerCenter", s.footerCenter);
    str("footerRight", s.footerRight);
    flag("headerOnFirstPage", s.headerOnFirstPage);
    flag("titles", s.titles);
    flag("synopses", s.synopses);
    flag("notes", s.notes);
    flag("comments", s.comments);
    flag("endnotes", s.endnotes);
    flag("highlights", s.highlights);
    flag("editorFont", s.editorFont);
    str("fontFamily", s.fontFamily);
    num("fontSize", s.fontSize, 4, 72);
    num("lineSpacing", s.lineSpacing, 0, 3);
    str("cardSize", s.cardSize);
    num("cardWidth", s.cardWidth, 30, 400);
    num("cardHeight", s.cardHeight, 20, 400);
    flag("cardPerPage", s.cardPerPage);
    flag("cardTitle", s.cardTitle);
    flag("cardSynopsis", s.cardSynopsis);
    flag("cardTextIfEmpty", s.cardTextIfEmpty);
    flag("cardLabel", s.cardLabel);
    flag("cardStatus", s.cardStatus);
    flag("cardKeywords", s.cardKeywords);
    flag("cardPictures", s.cardPictures);
    flag("cardRuled", s.cardRuled);
    flag("cardBorder", s.cardBorder);
    str("cardFont", s.cardFont);
    num("cardFontSize", s.cardFontSize, 4, 48);
    if (m.contains(QStringLiteral("outlinerSynopses")))
        s.outlinerSynopses = qBound(0, m.value(QStringLiteral("outlinerSynopses")).toInt(), 2);
    flag("outlinerAllRows", s.outlinerAllRows);
    flag("outlinerHeader", s.outlinerHeader);
    flag("outlinerLines", s.outlinerLines);
    flag("outlinerLabels", s.outlinerLabels);
    str("outlinerFont", s.outlinerFont);
    num("outlinerFontSize", s.outlinerFontSize, 4, 36);
    // a preset keeps its size
    for (const CardSize &c : cardSizes())
        if (c.name == s.cardSize) {
            s.cardWidth = c.w;
            s.cardHeight = c.h;
        }
    return s;
}

QVariantMap PrintSettings::standardMap() { return g_standard ? *g_standard : QVariantMap(); }

void PrintSettings::setStandardMap(const QVariantMap &m)
{
    if (!g_standard)
        g_standard = new QVariantMap;
    *g_standard = m;
}

PrintSettings PrintSettings::ofProject(const Project *p)
{
    PrintSettings base;
    if (p && p->docs())
        base.page = p->docs()->pageSetup();
    base = fromVariant(standardMap(), base);
    if (!p)
        return base;
    return fromVariant(p->uiState.value(kKey).toMap(), base);
}

void PrintSettings::storeInProject(Project *p, const PrintSettings &s)
{
    if (!p)
        return;
    p->uiState[kKey] = s.toVariant();
    p->markIndexDirty();
}

} // namespace qb
