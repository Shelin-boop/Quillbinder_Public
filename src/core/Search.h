#pragma once
#include "core/ProjectTypes.h"

#include <QList>
#include <QRegularExpression>
#include <QVariantMap>

namespace qb {

class Project;
class BinderItem;

QList<BinderItem *> searchProject(Project *p, const SearchQuery &q);
// quick search in titles and synopses (toolbar field)
QList<BinderItem *> quickSearch(Project *p, const QString &text, int max = 20);

// The toolbar field's list: documents whose title, synopsis or text contains
// the words, the best `perKind` of each kind. A document is listed once, under
// the first kind it matches.
struct QuickHit {
    enum Kind { Title = 0, Synopsis = 1, Text = 2 };
    BinderItem *item = nullptr;
    Kind kind = Title;
    QString excerpt;   // synopsis and text hits: the words around the match
};
QList<QuickHit> quickSearchGrouped(Project *p, const QString &text, int perKind = 10);

namespace search {
// One pattern that finds every term of the query (to highlight the matches in
// a text and to walk through them with Find Next). Invalid for an empty query
// or a regular expression with a mistake in it.
QRegularExpression pattern(const SearchQuery &q);
// Mistake in a regular expression search ("" = fine).
QString patternError(const SearchQuery &q);
// The pattern of a plain "find this text" (find bar, project replace).
QRegularExpression pattern(const QString &text, bool caseSensitive, bool wholeWord, bool regex);
// The words around the first match, on one line.
QString excerpt(const QString &text, const QRegularExpression &re, int context = 30);

// Queries as plain data (kept with the project so that "Search Results"
// can run the last search again).
QVariantMap toMap(const SearchQuery &q);
SearchQuery fromMap(const QVariantMap &m);

// Filter bar of corkboard and outliner.
bool matches(Project *p, BinderItem *item, const ItemFilter &f);
} // namespace search

} // namespace qb
