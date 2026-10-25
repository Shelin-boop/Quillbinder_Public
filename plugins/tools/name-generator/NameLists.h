#pragma once
// Name lists of the name generator and the generator itself (no widgets).
//
// A list is a JSON file (names/<id>.json in the add-on, or an own list in
// the tool's data folder):
//   { "id", "name", "source",
//     "male":    { "names": [...], "weights": [...] },   weights optional, then ordered by frequency
//     "female":  { ... },
//     "surnames": { ... },
//     "surnamesFemale": { ... },   optional: the women's forms (Polish, Czech, Russian ...)
//     "unisex": true,              optional: given names not split by gender
//     "surnameFirst": true }       optional: family name first (Japanese, Chinese, Korean, Hungarian, Vietnamese)

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class QRandomGenerator;

namespace namegen {

struct NameSet {
    QStringList names;          // most frequent first when weights exist
    QVector<double> weights;    // empty: no frequencies
    bool isEmpty() const { return names.isEmpty(); }
};

struct NameList {
    QString id, name, source, file;
    NameSet male, female, surnames, surnamesFemale;
    bool unisex = false;
    bool surnameFirst = false;
    bool own = false;           // the writer's own list (can be removed)
    bool hasFrequencies() const { return !male.weights.isEmpty() || !surnames.weights.isEmpty(); }
};

// Every list in the folders (*.json); the first list with a given id wins.
QList<NameList> loadLists(const QStringList &dirs, const QString &ownDir);
bool readList(const QString &file, NameList *out, QString *error);
bool writeList(const QString &file, const NameList &list, QString *error);

enum class Gender { Male, Female, Both };
enum class Show { Full, ForenameOnly, SurnameOnly, InitialAndSurname };

struct Options {
    Gender gender = Gender::Both;
    QStringList forenameLists;   // ids
    QStringList surnameLists;    // ids; empty or sameOrigin: the list of the forename
    bool sameOrigin = true;
    int count = 25;
    int forenameObscurity = 30;  // 0 = only common names .. 100 = only rare ones (lists with frequencies)
    int surnameObscurity = 30;
    int alliteration = 0;        // percent of names whose surname starts like the forename
    int doubleBarrelled = 0;     // percent with two surnames joined by a hyphen
    bool middleName = false;
    Show show = Show::Full;
    QString forenameStarts, surnameStarts;
};

struct Name {
    QString forename, middle, surname;
    bool female = false;
    bool surnameFirst = false;
    QString listName;
    QString text(Show show) const;
};

QList<Name> generate(const QList<NameList> &lists, const Options &o, QRandomGenerator *rng, QString *problem);

// The band of a ranked list that a given obscurity picks from (exposed for tests).
void band(int size, int obscurity, int *from, int *to);

} // namespace namegen
