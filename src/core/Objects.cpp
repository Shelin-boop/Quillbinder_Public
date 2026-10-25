#include "core/Objects.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace qb {
namespace objects {

// ---------------------------------------------------------------- field types

static const struct {
    FieldType type;
    const char *key;
    const char *label;
} kTypes[] = {
    {FieldType::Text, "text", "Short text"},
    {FieldType::LongText, "longtext", "Long text (with links)"},
    {FieldType::Number, "number", "Number with unit"},
    {FieldType::Choice, "choice", "Choice from a list"},
    {FieldType::YesNo, "yesno", "Yes or no"},
    {FieldType::Date, "date", "Date or time (free text)"},
    {FieldType::Picture, "picture", "Picture"},
    {FieldType::Objects, "objects", "Link to objects"},
    {FieldType::Documents, "documents", "Link to documents of the binder"},
};

QString typeKey(FieldType t)
{
    for (const auto &k : kTypes)
        if (k.type == t)
            return QLatin1String(k.key);
    return QStringLiteral("text");
}

FieldType typeFromKey(const QString &key)
{
    for (const auto &k : kTypes)
        if (key == QLatin1String(k.key))
            return k.type;
    return FieldType::Text;
}

QString typeLabel(FieldType t)
{
    for (const auto &k : kTypes)
        if (k.type == t)
            return QObject::tr(k.label);
    return QString();
}

QList<FieldType> allTypes()
{
    QList<FieldType> out;
    for (const auto &k : kTypes)
        out << k.type;
    return out;
}

static bool isIdList(FieldType t) { return t == FieldType::Objects || t == FieldType::Documents; }

// ---------------------------------------------------------------- JSON

static QJsonArray toArray(const QStringList &l)
{
    QJsonArray a;
    for (const QString &s : l)
        a.append(s);
    return a;
}
static QStringList fromArray(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray())
        if (!x.toString().isEmpty())
            out << x.toString();
    return out;
}

QJsonObject FieldDef::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("label"), label);
    o.insert(QStringLiteral("type"), typeKey(type));
    if (!unit.isEmpty())
        o.insert(QStringLiteral("unit"), unit);
    if (!choices.isEmpty())
        o.insert(QStringLiteral("choices"), toArray(choices));
    if (!targets.isEmpty())
        o.insert(QStringLiteral("targets"), toArray(targets));
    return o;
}

FieldDef FieldDef::fromJson(const QJsonObject &o)
{
    FieldDef f;
    f.id = o.value(QStringLiteral("id")).toString();
    f.label = o.value(QStringLiteral("label")).toString();
    f.type = typeFromKey(o.value(QStringLiteral("type")).toString());
    f.unit = o.value(QStringLiteral("unit")).toString();
    f.choices = fromArray(o.value(QStringLiteral("choices")));
    f.targets = fromArray(o.value(QStringLiteral("targets")));
    return f;
}

const FieldDef *Category::field(const QString &fid) const
{
    for (const FieldDef &f : fields)
        if (f.id == fid)
            return &f;
    return nullptr;
}

QJsonObject Category::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("color"), color.isValid() ? color.name() : QString());
    QJsonArray fs;
    for (const FieldDef &f : fields)
        fs.append(f.toJson());
    o.insert(QStringLiteral("fields"), fs);
    return o;
}

Category Category::fromJson(const QJsonObject &o)
{
    Category c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.name = o.value(QStringLiteral("name")).toString();
    c.color = QColor(o.value(QStringLiteral("color")).toString());
    for (const QJsonValue &v : o.value(QStringLiteral("fields")).toArray()) {
        FieldDef f = FieldDef::fromJson(v.toObject());
        if (!f.id.isEmpty() && !c.field(f.id))
            c.fields << f;
    }
    return c;
}

QString Object::text(const QString &fieldId) const { return values.value(fieldId).toString(); }

QStringList Object::ids(const QString &fieldId) const
{
    const QVariant v = values.value(fieldId);
    QStringList out = v.toStringList();
    out.removeAll(QString());
    return out;
}

QJsonObject Object::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), 1);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("category"), category);
    o.insert(QStringLiteral("name"), name);
    if (!folder.isEmpty())
        o.insert(QStringLiteral("folder"), folder);
    if (color.isValid())
        o.insert(QStringLiteral("color"), color.name());
    o.insert(QStringLiteral("created"), created.toString(Qt::ISODate));
    o.insert(QStringLiteral("modified"), modified.toString(Qt::ISODate));
    o.insert(QStringLiteral("fields"), QJsonObject::fromVariantMap(values));
    return o;
}

Object Object::fromJson(const QJsonObject &o)
{
    Object x;
    x.id = o.value(QStringLiteral("id")).toString();
    x.category = o.value(QStringLiteral("category")).toString();
    x.name = o.value(QStringLiteral("name")).toString();
    x.folder = o.value(QStringLiteral("folder")).toString();
    const QString col = o.value(QStringLiteral("color")).toString();
    if (!col.isEmpty())
        x.color = QColor(col);
    x.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    x.modified = QDateTime::fromString(o.value(QStringLiteral("modified")).toString(), Qt::ISODate);
    x.values = o.value(QStringLiteral("fields")).toObject().toVariantMap();
    // lists come back as QVariantList: make them string lists
    for (auto it = x.values.begin(); it != x.values.end(); ++it)
        if (it->typeId() == QMetaType::QVariantList)
            *it = it->toStringList();
    return x;
}

const Category *Schema::category(const QString &id) const
{
    for (const Category &c : categories)
        if (c.id == id)
            return &c;
    return nullptr;
}

Category *Schema::category(const QString &id)
{
    for (Category &c : categories)
        if (c.id == id)
            return &c;
    return nullptr;
}

QJsonObject Schema::toJson(bool withFolders) const
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), 1);
    QJsonArray cs;
    for (const Category &c : categories)
        cs.append(c.toJson());
    o.insert(QStringLiteral("categories"), cs);
    if (withFolders) {
        QJsonArray fs;
        for (const Folder &f : folders) {
            QJsonObject x;
            x.insert(QStringLiteral("id"), f.id);
            x.insert(QStringLiteral("category"), f.category);
            if (!f.parent.isEmpty())
                x.insert(QStringLiteral("parent"), f.parent);
            x.insert(QStringLiteral("name"), f.name);
            fs.append(x);
        }
        o.insert(QStringLiteral("folders"), fs);
    }
    return o;
}

Schema Schema::fromJson(const QJsonObject &o)
{
    Schema s;
    for (const QJsonValue &v : o.value(QStringLiteral("categories")).toArray()) {
        Category c = Category::fromJson(v.toObject());
        if (!c.id.isEmpty() && !s.category(c.id))
            s.categories << c;
    }
    for (const QJsonValue &v : o.value(QStringLiteral("folders")).toArray()) {
        const QJsonObject x = v.toObject();
        Folder f{x.value(QStringLiteral("id")).toString(), x.value(QStringLiteral("category")).toString(),
                 x.value(QStringLiteral("parent")).toString(), x.value(QStringLiteral("name")).toString()};
        if (!f.id.isEmpty() && s.category(f.category))
            s.folders << f;
    }
    return s;
}

// ---------------------------------------------------------------- the built-in templates

namespace {

FieldDef fd(const char *id, const char *label, FieldType t, const char *unit = "", QStringList extra = {})
{
    FieldDef f;
    f.id = QLatin1String(id);
    f.label = QObject::tr(label);
    f.type = t;
    f.unit = QString::fromUtf8(unit);
    if (t == FieldType::Choice)
        f.choices = extra;
    else if (t == FieldType::Objects)
        f.targets = extra;
    return f;
}

QStringList list(std::initializer_list<const char *> l)
{
    QStringList out;
    for (const char *s : l)
        out << QObject::tr(s);
    return out;
}

} // namespace

Schema builtInSchema()
{
    using T = FieldType;
    const QStringList chars{QStringLiteral("characters")}, locs{QStringLiteral("locations")},
        envs{QStringLiteral("environments")}, pois{QStringLiteral("pois")}, wx{QStringLiteral("weather")};
    Schema s;
    Category c;
    c.id = QStringLiteral("characters");
    c.name = QObject::tr("Characters");
    c.color = QColor(QStringLiteral("#9c2a9c"));
    c.fields = {fd("fullName", "Full name", T::Text),
                fd("aliases", "Also known as", T::Text),
                fd("role", "Role in the story", T::Choice, "", list({"Protagonist", "Antagonist", "Supporting", "Minor", "Mentioned only"})),
                fd("race", "Race / species", T::Text),
                fd("gender", "Gender", T::Text),
                fd("age", "Age", T::Number, "years"),
                fd("born", "Born", T::Date),
                fd("height", "Height", T::Number, "cm"),
                fd("build", "Build", T::Text),
                fd("eyes", "Eye color", T::Text),
                fd("hair", "Hair", T::Text),
                fd("features", "Distinguishing features", T::LongText),
                fd("attributes", "Attributes", T::LongText),
                fd("traits", "Traits", T::LongText),
                fd("occupation", "Occupation", T::Text),
                fd("livesIn", "Lives in", T::Objects, "", locs),
                fd("origin", "Comes from", T::Objects, "", locs),
                fd("relations", "Family and relationships", T::Objects, "", chars),
                fd("goals", "Goals and motives", T::LongText),
                fd("fears", "Fears and weaknesses", T::LongText),
                fd("background", "Background story", T::LongText),
                fd("scenes", "Appears in", T::Documents),
                fd("picture", "Picture", T::Picture),
                fd("other", "Other", T::LongText)};
    s.categories << c;
    c = Category();
    c.id = QStringLiteral("locations");
    c.name = QObject::tr("Locations");
    c.color = QColor(QStringLiteral("#1a9fd9"));
    c.fields = {fd("kind", "Kind", T::Choice, "",
                   list({"Continent", "Country", "Region", "City", "Town", "Village", "District", "Building", "Room", "Other"})),
                fd("partOf", "Part of", T::Objects, "", locs),
                fd("environment", "Environment", T::Objects, "", envs),
                fd("weather", "Climate and weather", T::Objects, "", wx),
                fd("population", "Population", T::Number, "inhabitants"),
                fd("rule", "Ruled by", T::Text),
                fd("inhabitants", "Notable inhabitants", T::Objects, "", chars),
                fd("places", "Points of interest", T::Objects, "", pois),
                fd("description", "Description", T::LongText),
                fd("history", "History", T::LongText),
                fd("scenes", "Scenes set here", T::Documents),
                fd("picture", "Map or picture", T::Picture),
                fd("other", "Other", T::LongText)};
    s.categories << c;
    c = Category();
    c.id = QStringLiteral("environments");
    c.name = QObject::tr("Environments");
    c.color = QColor(QStringLiteral("#4ea72e"));
    c.fields = {fd("kind", "Kind of landscape", T::Choice, "",
                   list({"Forest", "Mountains", "Hills", "Plains", "Desert", "Coast", "Sea", "River", "Lake", "Swamp", "Tundra",
                         "Jungle", "Underground", "City", "Other"})),
                fd("terrain", "Terrain", T::LongText),
                fd("flora", "Plants", T::LongText),
                fd("fauna", "Animals", T::LongText),
                fd("resources", "Resources", T::Text),
                fd("dangers", "Dangers", T::LongText),
                fd("weather", "Typical weather", T::Objects, "", wx),
                fd("senses", "Sounds, smells and light", T::LongText),
                fd("locations", "Places with this environment", T::Objects, "", locs),
                fd("description", "Description", T::LongText),
                fd("picture", "Picture", T::Picture),
                fd("other", "Other", T::LongText)};
    s.categories << c;
    c = Category();
    c.id = QStringLiteral("pois");
    c.name = QObject::tr("Points of Interest");
    c.color = QColor(QStringLiteral("#e8722e"));
    c.fields = {fd("kind", "Kind", T::Choice, "",
                   list({"Building", "Landmark", "Ruin", "Shop", "Inn or tavern", "Temple", "Castle", "Natural feature", "Other"})),
                fd("locatedIn", "Located in", T::Objects, "", locs),
                fd("keeper", "Owner or keeper", T::Objects, "", chars),
                fd("significance", "Significance for the story", T::LongText),
                fd("access", "How to get there", T::LongText),
                fd("secrets", "Secrets", T::LongText),
                fd("description", "Description", T::LongText),
                fd("scenes", "Scenes set here", T::Documents),
                fd("picture", "Picture", T::Picture),
                fd("other", "Other", T::LongText)};
    s.categories << c;
    c = Category();
    c.id = QStringLiteral("weather");
    c.name = QObject::tr("Weather");
    c.color = QColor(QStringLiteral("#5b7f95"));
    c.fields = {fd("season", "Season", T::Choice, "",
                   list({"Spring", "Summer", "Autumn", "Winter", "Rainy season", "Dry season", "All year"})),
                fd("temperature", "Temperature", T::Number, "°C"),
                fd("precipitation", "Precipitation", T::Choice, "", list({"None", "Drizzle", "Rain", "Heavy rain", "Snow", "Hail", "Fog"})),
                fd("wind", "Wind", T::Text),
                fd("sky", "Sky and light", T::Text),
                fd("phenomena", "Special phenomena", T::LongText),
                fd("effects", "Effects on people and travel", T::LongText),
                fd("where", "Where it occurs", T::Objects, "", locs + envs),
                fd("description", "Description", T::LongText),
                fd("other", "Other", T::LongText)};
    s.categories << c;
    return s;
}

static QString newProjectsFile() { return dataDir() + QLatin1String("/object-templates.json"); }

Schema schemaForNewProjects()
{
    QFile f(newProjectsFile());
    if (f.open(QIODevice::ReadOnly)) {
        const Schema s = Schema::fromJson(QJsonDocument::fromJson(f.readAll()).object());
        if (!s.categories.isEmpty())
            return s;
    }
    return builtInSchema();
}

bool saveSchemaForNewProjects(const Schema &s, QString *error)
{
    QDir().mkpath(dataDir());
    QSaveFile f(newProjectsFile());
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(s.toJson(false)).toJson()) < 0 || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

void resetSchemaForNewProjects() { QFile::remove(newProjectsFile()); }
bool hasOwnSchemaForNewProjects() { return QFile::exists(newProjectsFile()); }

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString linkTo(const QString &objectId) { return QStringLiteral("qbobj://") + objectId; }

QString idFromLink(const QString &href)
{
    return href.startsWith(QLatin1String("qbobj://")) ? href.mid(8) : QString();
}

QStringList textLinks(const Object &o, const Category *c)
{
    static const QRegularExpression re(QStringLiteral("qbobj://([A-Za-z0-9-]+)"));
    QStringList out;
    if (!c)
        return out;
    for (const FieldDef &f : c->fields) {
        if (f.type != FieldType::LongText)
            continue;
        auto it = re.globalMatch(o.text(f.id));
        while (it.hasNext()) {
            const QString id = it.next().captured(1);
            if (!out.contains(id))
                out << id;
        }
    }
    return out;
}

// ---------------------------------------------------------------- store

Store::Store(const QString &projectFolder, QObject *parent) : QObject(parent), m_dir(projectFolder + QLatin1String("/objects"))
{
    m_schema = schemaForNewProjects();
}

bool Store::load(QString *error)
{
    m_warnings.clear();
    m_objects.clear();
    QFile sf(m_dir + QLatin1String("/schema.json"));
    if (sf.exists()) {
        QJsonParseError pe;
        const QJsonDocument d = sf.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(sf.readAll(), &pe) : QJsonDocument();
        Schema s = Schema::fromJson(d.object());
        if (s.categories.isEmpty()) {
            m_warnings << tr("The templates of the objects (%1) could not be read; the standard templates are used. "
                             "The file stays as it is until the templates are changed.")
                              .arg(sf.fileName());
        } else {
            m_schema = s;
            m_schemaLoaded = true;
        }
    }
    const QFileInfoList dirs = QDir(m_dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &di : dirs) {
        if (di.fileName() == QLatin1String("images"))
            continue;
        for (const QFileInfo &fi : QDir(di.absoluteFilePath()).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
            QFile f(fi.absoluteFilePath());
            QJsonParseError pe;
            const QJsonDocument d = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &pe) : QJsonDocument();
            Object o = Object::fromJson(d.object());
            if (o.id.isEmpty() || !d.isObject()) {
                m_warnings << tr("The object file %1 could not be read; it is left as it is.").arg(fi.absoluteFilePath());
                continue;
            }
            if (o.category.isEmpty())
                o.category = di.fileName();
            if (!m_schema.category(o.category)) {
                // an object of a category the templates do not have: give it one, so it is not lost
                Category c;
                c.id = o.category;
                c.name = o.category;
                c.color = QColor(Qt::gray);
                m_schema.categories << c;
                m_warnings << tr("Objects of the unknown category \"%1\" were found; the category was added.").arg(o.category);
            }
            m_objects.insert(o.id, o);
        }
    }
    if (error && !m_warnings.isEmpty())
        *error = m_warnings.join(QLatin1Char('\n'));
    return true;
}

bool Store::isDirty() const { return m_schemaDirty || !m_dirty.isEmpty() || !m_removedFiles.isEmpty() || !m_orphanPictures.isEmpty(); }

QString Store::fileOf(const Object &o) const { return QStringLiteral("%1/%2/%3.json").arg(m_dir, o.category, o.id); }

static bool writeJson(const QString &path, const QJsonObject &o, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    const QByteArray data = QJsonDocument(o).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

bool Store::save(QString *error)
{
    bool ok = true;
    if (m_schemaDirty || (!m_schemaLoaded && (!m_objects.isEmpty() || !m_removedFiles.isEmpty()))) {
        // a damaged schema file is kept aside before it is replaced
        const QString sf = m_dir + QLatin1String("/schema.json");
        if (!m_schemaLoaded && QFile::exists(sf))
            QFile::rename(sf, sf + QStringLiteral(".damaged-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
        if (writeJson(sf, m_schema.toJson(), error)) {
            m_schemaDirty = false;
            m_schemaLoaded = true;
        } else {
            ok = false;
        }
    }
    for (const QString &id : QSet<QString>(m_dirty)) {
        auto it = m_objects.constFind(id);
        if (it == m_objects.constEnd()) {
            m_dirty.remove(id);
            continue;
        }
        if (writeJson(fileOf(*it), it->toJson(), error))
            m_dirty.remove(id);
        else
            ok = false;
    }
    for (auto it = m_removedFiles.begin(); it != m_removedFiles.end();) {
        if (!QFile::exists(it.value()) || QFile::remove(it.value()))
            it = m_removedFiles.erase(it);
        else
            ++it;
    }
    for (const QString &p : std::as_const(m_orphanPictures))
        QFile::remove(picturePath(p));
    m_orphanPictures.clear();
    return ok;
}

void Store::touchSchema()
{
    m_schemaDirty = true;
    emit dirtied();
}

void Store::setSchema(const Schema &s)
{
    const QList<Folder> folders = m_schema.folders;
    m_schema = s;
    // folders of categories that are gone go as well
    m_schema.folders.clear();
    for (const Folder &f : (s.folders.isEmpty() ? folders : s.folders))
        if (m_schema.category(f.category))
            m_schema.folders << f;
    touchSchema();
    emit schemaChanged();
}

QColor Store::colorOf(const Object &o) const
{
    if (o.color.isValid())
        return o.color;
    const Category *c = m_schema.category(o.category);
    return c && c->color.isValid() ? c->color : QColor(Qt::gray);
}

QList<const Object *> Store::objects(const QString &category) const
{
    QList<const Object *> out;
    for (const Object &o : m_objects)
        if (category.isEmpty() || o.category == category)
            out << &o;
    std::sort(out.begin(), out.end(), [](const Object *a, const Object *b) {
        const int c = QString::localeAwareCompare(a->name, b->name);
        return c != 0 ? c < 0 : a->id < b->id;
    });
    return out;
}

const Object *Store::object(const QString &id) const
{
    auto it = m_objects.constFind(id);
    return it == m_objects.constEnd() ? nullptr : &*it;
}

QString Store::create(const QString &category, const QString &folder, const QString &name)
{
    const Category *c = m_schema.category(category);
    if (!c)
        return QString();
    Object o;
    o.id = newId();
    o.category = category;
    o.folder = folder;
    o.name = name.trimmed().isEmpty() ? tr("Unnamed") : name.trimmed();
    o.created = o.modified = QDateTime::currentDateTime();
    m_objects.insert(o.id, o);
    m_dirty.insert(o.id);
    emit dirtied();
    emit objectAdded(o.id);
    return o.id;
}

void Store::update(const Object &o, QObject *source)
{
    auto it = m_objects.find(o.id);
    if (it == m_objects.end())
        return;
    Object n = o;
    n.modified = QDateTime::currentDateTime();
    // pictures replaced or removed: their files go at the next save
    if (const Category *c = m_schema.category(n.category))
        for (const FieldDef &f : c->fields)
            if (f.type == FieldType::Picture) {
                const QString was = it->text(f.id), now = n.text(f.id);
                if (!was.isEmpty() && was != now)
                    m_orphanPictures << was;
            }
    *it = n;
    m_dirty.insert(n.id);
    emit dirtied();
    emit objectChanged(n.id, source);
}

void Store::remove(const QString &id)
{
    auto it = m_objects.find(id);
    if (it == m_objects.end())
        return;
    m_removedFiles.insert(id, fileOf(*it));
    if (const Category *c = m_schema.category(it->category))
        for (const FieldDef &f : c->fields)
            if (f.type == FieldType::Picture && !it->text(f.id).isEmpty())
                m_orphanPictures << it->text(f.id);
    m_objects.erase(it);
    m_dirty.remove(id);
    // links to it in the fields of other objects
    QStringList touched;
    for (Object &o : m_objects) {
        const Category *c = m_schema.category(o.category);
        if (!c)
            continue;
        bool changed = false;
        // links in texts become plain words: [Ashvale](qbobj://<id>) -> Ashvale
        const QRegularExpression textLink(QStringLiteral("\\[([^\\]]*)\\]\\(%1\\)").arg(QRegularExpression::escape(linkTo(id))));
        for (const FieldDef &f : c->fields)
            if (f.type == FieldType::Objects) {
                QStringList l = o.ids(f.id);
                if (l.removeAll(id) > 0) {
                    o.values.insert(f.id, l);
                    changed = true;
                }
            } else if (f.type == FieldType::LongText && o.text(f.id).contains(linkTo(id))) {
                QString t = o.text(f.id);
                t.replace(textLink, QStringLiteral("\\1"));
                o.values.insert(f.id, t);
                changed = true;
            }
        if (changed) {
            m_dirty.insert(o.id);
            touched << o.id;
        }
    }
    emit dirtied();
    emit objectRemoved(id);
    for (const QString &t : touched)
        emit objectChanged(t, nullptr);
}

QString Store::duplicate(const QString &id)
{
    const Object *src = object(id);
    if (!src)
        return QString();
    Object o = *src;
    o.id = newId();
    o.name = tr("%1 (copy)").arg(src->name);
    o.created = o.modified = QDateTime::currentDateTime();
    // pictures are files of their own
    if (const Category *c = m_schema.category(o.category))
        for (const FieldDef &f : c->fields)
            if (f.type == FieldType::Picture && !o.text(f.id).isEmpty()) {
                const QString copy = importPicture(o.id, f.id, picturePath(o.text(f.id)));
                o.values.insert(f.id, copy);
            }
    m_objects.insert(o.id, o);
    m_dirty.insert(o.id);
    emit dirtied();
    emit objectAdded(o.id);
    return o.id;
}

QList<Folder> Store::folders(const QString &category) const
{
    QList<Folder> out;
    for (const Folder &f : m_schema.folders)
        if (f.category == category)
            out << f;
    std::sort(out.begin(), out.end(), [](const Folder &a, const Folder &b) { return QString::localeAwareCompare(a.name, b.name) < 0; });
    return out;
}

QString Store::addFolder(const QString &category, const QString &parent, const QString &name)
{
    if (!m_schema.category(category))
        return QString();
    Folder f{newId(), category, parent, name.trimmed().isEmpty() ? tr("New Folder") : name.trimmed()};
    m_schema.folders << f;
    touchSchema();
    emit foldersChanged(category);
    return f.id;
}

void Store::renameFolder(const QString &id, const QString &name)
{
    for (Folder &f : m_schema.folders)
        if (f.id == id && !name.trimmed().isEmpty()) {
            f.name = name.trimmed();
            touchSchema();
            emit foldersChanged(f.category);
        }
}

void Store::removeFolder(const QString &id)
{
    QString parent, category;
    for (const Folder &f : m_schema.folders)
        if (f.id == id) {
            parent = f.parent;
            category = f.category;
        }
    if (category.isEmpty())
        return;
    for (Folder &f : m_schema.folders)
        if (f.parent == id)
            f.parent = parent;
    m_schema.folders.erase(std::remove_if(m_schema.folders.begin(), m_schema.folders.end(), [&](const Folder &f) { return f.id == id; }),
                           m_schema.folders.end());
    for (Object &o : m_objects)
        if (o.folder == id) {
            o.folder = parent;
            m_dirty.insert(o.id);
        }
    touchSchema();
    emit foldersChanged(category);
}

void Store::moveFolder(const QString &id, const QString &parent)
{
    // never into itself or one of its own folders
    for (QString p = parent; !p.isEmpty();) {
        if (p == id)
            return;
        QString next;
        for (const Folder &f : m_schema.folders)
            if (f.id == p)
                next = f.parent;
        p = next;
    }
    for (Folder &f : m_schema.folders)
        if (f.id == id) {
            f.parent = parent;
            touchSchema();
            emit foldersChanged(f.category);
        }
}

void Store::moveObject(const QString &id, const QString &folder)
{
    auto it = m_objects.find(id);
    if (it == m_objects.end() || it->folder == folder)
        return;
    it->folder = folder;
    m_dirty.insert(id);
    emit dirtied();
    emit foldersChanged(it->category);
}

QList<Backlink> Store::backlinks(const QString &id) const
{
    QList<Backlink> out;
    for (const Object *o : objects()) {
        if (o->id == id)
            continue;
        const Category *c = m_schema.category(o->category);
        if (!c)
            continue;
        for (const FieldDef &f : c->fields) {
            if (f.type == FieldType::Objects && o->ids(f.id).contains(id))
                out << Backlink{o->id, f.label};
            else if (f.type == FieldType::LongText && o->text(f.id).contains(linkTo(id)))
                out << Backlink{o->id, tr("text of %1").arg(f.label)};
        }
    }
    return out;
}

QList<const Object *> Store::objectsForDocument(const QString &documentId) const
{
    QList<const Object *> out;
    for (const Object *o : objects()) {
        const Category *c = m_schema.category(o->category);
        if (!c)
            continue;
        for (const FieldDef &f : c->fields)
            if (f.type == FieldType::Documents && o->ids(f.id).contains(documentId)) {
                out << o;
                break;
            }
    }
    return out;
}

QString Store::importPicture(const QString &objectId, const QString &fieldId, const QString &file, QString *error)
{
    QImage img(file);
    if (img.isNull()) {
        if (error)
            *error = tr("%1 is not a picture that can be shown.").arg(file);
        return QString();
    }
    QDir().mkpath(pictureDir());
    QString suffix = QFileInfo(file).suffix().toLower();
    if (suffix != QLatin1String("png") && suffix != QLatin1String("jpg") && suffix != QLatin1String("jpeg"))
        suffix = QStringLiteral("png");
    const QString name = QStringLiteral("%1-%2-%3.%4").arg(objectId.left(8), fieldId, newId().left(6), suffix);
    // pictures larger than 1600 px are made smaller (the file in the project stays small)
    if (img.width() > 1600 || img.height() > 1600)
        img = img.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!img.save(picturePath(name))) {
        if (error)
            *error = tr("Cannot write %1").arg(picturePath(name));
        return QString();
    }
    return name;
}

QString Store::picturePath(const QString &name) const { return name.isEmpty() ? QString() : pictureDir() + QLatin1Char('/') + name; }

} // namespace objects
} // namespace qb
