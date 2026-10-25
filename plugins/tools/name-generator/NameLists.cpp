#include "NameLists.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>

#include <cmath>

namespace namegen {

static NameSet readSet(const QJsonObject &o)
{
    NameSet s;
    for (const QJsonValue &v : o.value(QStringLiteral("names")).toArray())
        s.names << v.toString();
    const QJsonArray w = o.value(QStringLiteral("weights")).toArray();
    if (w.size() == s.names.size())
        for (const QJsonValue &v : w)
            s.weights << v.toDouble();
    return s;
}

static QJsonObject writeSet(const NameSet &s)
{
    QJsonObject o{{QStringLiteral("names"), QJsonArray::fromStringList(s.names)}};
    if (!s.weights.isEmpty()) {
        QJsonArray w;
        for (double d : s.weights)
            w.append(d);
        o.insert(QStringLiteral("weights"), w);
    }
    return o;
}

bool readList(const QString &file, NameList *out, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    NameList l;
    l.file = file;
    l.id = o.value(QStringLiteral("id")).toString(QFileInfo(file).completeBaseName());
    l.name = o.value(QStringLiteral("name")).toString(l.id);
    l.source = o.value(QStringLiteral("source")).toString();
    l.male = readSet(o.value(QStringLiteral("male")).toObject());
    l.female = readSet(o.value(QStringLiteral("female")).toObject());
    l.surnames = readSet(o.value(QStringLiteral("surnames")).toObject());
    l.surnamesFemale = readSet(o.value(QStringLiteral("surnamesFemale")).toObject());
    l.unisex = o.value(QStringLiteral("unisex")).toBool();
    l.surnameFirst = o.value(QStringLiteral("surnameFirst")).toBool();
    if (l.male.isEmpty() && l.female.isEmpty() && l.surnames.isEmpty()) {
        if (error)
            *error = QObject::tr("The file has no names.");
        return false;
    }
    *out = l;
    return true;
}

bool writeList(const QString &file, const NameList &l, QString *error)
{
    QJsonObject o{{QStringLiteral("id"), l.id}, {QStringLiteral("name"), l.name}, {QStringLiteral("source"), l.source}};
    if (!l.male.isEmpty())
        o.insert(QStringLiteral("male"), writeSet(l.male));
    if (!l.female.isEmpty())
        o.insert(QStringLiteral("female"), writeSet(l.female));
    if (!l.surnames.isEmpty())
        o.insert(QStringLiteral("surnames"), writeSet(l.surnames));
    if (!l.surnamesFemale.isEmpty())
        o.insert(QStringLiteral("surnamesFemale"), writeSet(l.surnamesFemale));
    if (l.unisex)
        o.insert(QStringLiteral("unisex"), true);
    if (l.surnameFirst)
        o.insert(QStringLiteral("surnameFirst"), true);
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return true;
}

QList<NameList> loadLists(const QStringList &dirs, const QString &ownDir)
{
    QList<NameList> out;
    QSet<QString> seen;
    auto scan = [&](const QString &dir, bool own) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            NameList l;
            if (!readList(fi.absoluteFilePath(), &l, nullptr) || seen.contains(l.id))
                continue;
            l.own = own;
            seen.insert(l.id);
            out << l;
        }
    };
    if (!ownDir.isEmpty())
        scan(ownDir, true);
    for (const QString &d : dirs)
        scan(d, false);
    std::sort(out.begin(), out.end(), [](const NameList &a, const NameList &b) {
        if (a.own != b.own)
            return b.own; // own lists at the end
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

void band(int size, int obscurity, int *from, int *to)
{
    // 0: the most common tenth; 50: the whole list; 100: the rarer half
    const double o = qBound(0, obscurity, 100) / 100.0;
    double lo = o > 0.5 ? (o - 0.5) : 0.0;           // 0 .. 0.5
    double hi = qMin(1.0, 0.1 + o * 1.8);            // 0.1 .. 1
    *from = qBound(0, int(std::floor(lo * size)), qMax(0, size - 1));
    *to = qBound(*from + 1, int(std::ceil(hi * size)), size);
}

// Picks a name; with frequencies by obscurity (rank band), without them from the whole list.
static QString pickFrom(const NameSet &set, int obscurity, const QString &starts, const QSet<QString> &avoid,
                        QRandomGenerator *rng)
{
    if (set.isEmpty())
        return QString();
    int from = 0, to = int(set.names.size());
    if (!set.weights.isEmpty())
        band(int(set.names.size()), obscurity, &from, &to);
    QList<int> pool;
    pool.reserve(to - from);
    for (int i = from; i < to; ++i) {
        const QString &n = set.names.at(i);
        if (!starts.isEmpty() && !n.startsWith(starts, Qt::CaseInsensitive))
            continue;
        if (avoid.contains(n))
            continue;
        pool << i;
    }
    if (pool.isEmpty() && !starts.isEmpty()) {
        // the band has no name with these letters: look in the whole list
        for (int i = 0; i < set.names.size(); ++i)
            if (set.names.at(i).startsWith(starts, Qt::CaseInsensitive) && !avoid.contains(set.names.at(i)))
                pool << i;
    }
    if (pool.isEmpty())
        return QString();
    if (!set.weights.isEmpty() && obscurity < 50) {
        // common names: by their frequency inside the band
        double total = 0;
        for (int i : pool)
            total += set.weights.at(i);
        double r = rng->generateDouble() * total;
        for (int i : pool) {
            r -= set.weights.at(i);
            if (r <= 0)
                return set.names.at(i);
        }
        return set.names.at(pool.constLast());
    }
    return set.names.at(pool.at(rng->bounded(int(pool.size()))));
}

QString Name::text(Show show) const
{
    QString given = forename;
    if (!middle.isEmpty())
        given += QLatin1Char(' ') + middle;
    switch (show) {
    case Show::ForenameOnly:
        return given;
    case Show::SurnameOnly:
        return surname;
    case Show::InitialAndSurname: {
        QString initials;
        for (const QString &part : given.split(QLatin1Char(' '), Qt::SkipEmptyParts))
            initials += part.left(1) + QStringLiteral(". ");
        return surnameFirst ? surname + QLatin1Char(' ') + initials.trimmed() : initials + surname;
    }
    case Show::Full:
        break;
    }
    if (surname.isEmpty())
        return given;
    if (given.isEmpty())
        return surname;
    return surnameFirst ? surname + QLatin1Char(' ') + given : given + QLatin1Char(' ') + surname;
}

QList<Name> generate(const QList<NameList> &lists, const Options &o, QRandomGenerator *rng, QString *problem)
{
    QList<const NameList *> fore, sur;
    for (const NameList &l : lists) {
        if (o.forenameLists.contains(l.id) && (!l.male.isEmpty() || !l.female.isEmpty()))
            fore << &l;
        if (o.surnameLists.contains(l.id) && !l.surnames.isEmpty())
            sur << &l;
    }
    const bool needFore = o.show != Show::SurnameOnly;
    const bool needSur = o.show != Show::ForenameOnly;
    if (needFore && fore.isEmpty()) {
        if (problem)
            *problem = QObject::tr("Choose at least one list of forenames.");
        return {};
    }
    if (needSur && fore.isEmpty() && sur.isEmpty()) {
        if (problem)
            *problem = QObject::tr("Choose at least one list of surnames.");
        return {};
    }
    QList<Name> out;
    QSet<QString> made;
    const int wanted = qBound(1, o.count, 1000);
    int attempts = 0;
    while (out.size() < wanted && attempts < wanted * 30) {
        ++attempts;
        Name n;
        n.female = o.gender == Gender::Female || (o.gender == Gender::Both && rng->bounded(2) == 1);
        const NameList *fl = fore.isEmpty() ? nullptr : fore.at(rng->bounded(int(fore.size())));
        if (fl) {
            const NameSet &set = n.female ? (fl->female.isEmpty() ? fl->male : fl->female)
                                          : (fl->male.isEmpty() ? fl->female : fl->male);
            if (needFore) {
                n.forename = pickFrom(set, o.forenameObscurity, o.forenameStarts, {}, rng);
                if (n.forename.isEmpty())
                    continue;
                if (o.middleName)
                    n.middle = pickFrom(set, o.forenameObscurity, QString(), {n.forename}, rng);
            }
        }
        if (needSur) {
            const NameList *sl = nullptr;
            if ((o.sameOrigin || sur.isEmpty()) && fl && !fl->surnames.isEmpty())
                sl = fl;
            else if (!sur.isEmpty())
                sl = sur.at(rng->bounded(int(sur.size())));
            if (!sl)
                continue;
            const NameSet &set = (n.female && !sl->surnamesFemale.isEmpty()) ? sl->surnamesFemale : sl->surnames;
            QString starts = o.surnameStarts;
            if (starts.isEmpty() && !n.forename.isEmpty() && rng->bounded(100) < o.alliteration)
                starts = n.forename.left(1);
            n.surname = pickFrom(set, o.surnameObscurity, starts, {}, rng);
            if (n.surname.isEmpty() && starts != o.surnameStarts) // no surname with that letter: no alliteration then
                n.surname = pickFrom(set, o.surnameObscurity, o.surnameStarts, {}, rng);
            if (n.surname.isEmpty())
                continue;
            if (rng->bounded(100) < o.doubleBarrelled) {
                const QString second = pickFrom(set, o.surnameObscurity, QString(), {n.surname}, rng);
                if (!second.isEmpty())
                    n.surname += QLatin1Char('-') + second;
            }
            n.surnameFirst = sl->surnameFirst;
            n.listName = sl == fl || !fl ? sl->name : fl->name + QStringLiteral(" / ") + sl->name;
        } else if (fl) {
            n.listName = fl->name;
        }
        const QString key = n.text(o.show);
        if (made.contains(key) && attempts < wanted * 20)
            continue;
        made.insert(key);
        out << n;
    }
    if (out.size() < wanted && problem)
        *problem = QObject::tr("Only %1 different names could be made with these settings.").arg(out.size());
    return out;
}

} // namespace namegen
