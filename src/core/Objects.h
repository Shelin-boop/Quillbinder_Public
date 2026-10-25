#pragma once
// Objects of a project (0.20): characters, locations, environments, points of
// interest, weather and categories of the writer's own. Each category has a
// template (its list of fields) and a color; objects fill the fields, and fields of
// the kind "objects" link objects with each other (a character "lives in" a
// location ...); long texts can hold links as well. Objects belong to the project,
// not to a document; "documents" fields link them to documents of the binder.
//
// Stored in the project folder, never in the project file (.qbx), so that versions
// before 0.20, which rewrite the project file, cannot lose them:
//   objects/schema.json            categories with their templates and colors, folders
//   objects/<category>/<id>.json   one file per object
//   objects/images/                pictures of the objects
// Nothing is written until the first object or a change of the templates.
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace qb {
namespace objects {

enum class FieldType { Text, LongText, Number, Choice, YesNo, Date, Picture, Objects, Documents };
QString typeKey(FieldType t);
FieldType typeFromKey(const QString &key);
QString typeLabel(FieldType t); // "Short text", "Link to objects" ...
QList<FieldType> allTypes();

struct FieldDef {
    QString id;
    QString label;
    FieldType type = FieldType::Text;
    QString unit;        // Number: "cm", "years" ...
    QStringList choices; // Choice: the list (other values can be typed)
    QStringList targets; // Objects: categories that can be linked (empty: all)
    QJsonObject toJson() const;
    static FieldDef fromJson(const QJsonObject &o);
};

struct Category {
    QString id;
    QString name;
    QColor color;
    QList<FieldDef> fields; // the template
    const FieldDef *field(const QString &id) const;
    QJsonObject toJson() const;
    static Category fromJson(const QJsonObject &o);
};

struct Folder {
    QString id;
    QString category;
    QString parent; // "" = the category itself
    QString name;
};

struct Object {
    QString id;
    QString category;
    QString folder; // "" = the category itself
    QString name;
    QColor color;   // invalid: the category's color
    QDateTime created, modified;
    QVariantMap values; // field id -> QString (texts), bool (yes/no), QStringList (objects, documents)
    QString text(const QString &fieldId) const;
    QStringList ids(const QString &fieldId) const;
    QJsonObject toJson() const;
    static Object fromJson(const QJsonObject &o);
};

struct Schema {
    QList<Category> categories;
    QList<Folder> folders;
    const Category *category(const QString &id) const;
    Category *category(const QString &id);
    QJsonObject toJson(bool withFolders = true) const;
    static Schema fromJson(const QJsonObject &o);
};

// Characters, Locations, Environments, Points of Interest, Weather.
Schema builtInSchema();
// The writer's templates for new projects (File > ... "Use for New Projects"), else the built-in ones.
Schema schemaForNewProjects();
bool saveSchemaForNewProjects(const Schema &s, QString *error = nullptr);
void resetSchemaForNewProjects();
bool hasOwnSchemaForNewProjects();

QString newId();
// "qbobj://<id>": a link to an object (in long texts, and in the documents' text)
QString linkTo(const QString &objectId);
QString idFromLink(const QString &href); // "" when it is no object link
// The ids an object links to in its long texts.
QStringList textLinks(const Object &o, const Category *c);

struct Backlink {
    QString objectId; // the object that points here
    QString field;    // its field (label), or "text of <field>"
};

class Store : public QObject
{
    Q_OBJECT
public:
    explicit Store(const QString &projectFolder, QObject *parent = nullptr);
    QString dir() const { return m_dir; }
    QString pictureDir() const { return m_dir + QLatin1String("/images"); }
    bool load(QString *error = nullptr);
    // files written since the last save: changed objects, removed ones, the schema
    bool isDirty() const;
    bool save(QString *error = nullptr);
    // what could not be read (damaged files are kept as they are)
    QStringList warnings() const { return m_warnings; }

    const Schema &schema() const { return m_schema; }
    void setSchema(const Schema &s);
    const Category *category(const QString &id) const { return m_schema.category(id); }
    QColor colorOf(const Object &o) const;

    QList<const Object *> objects(const QString &category = QString()) const; // sorted by name
    const Object *object(const QString &id) const;
    int count() const { return m_objects.size(); }
    // a new object (name may be empty: "New <category>")
    QString create(const QString &category, const QString &folder = QString(), const QString &name = QString());
    // replaces the object of that id; source: the widget that changed it (gets no echo)
    void update(const Object &o, QObject *source = nullptr);
    // removes the object and every link to it in other objects' fields
    void remove(const QString &id);
    QString duplicate(const QString &id);

    QList<Folder> folders(const QString &category) const;
    QString addFolder(const QString &category, const QString &parent, const QString &name);
    void renameFolder(const QString &id, const QString &name);
    void removeFolder(const QString &id); // its objects and folders go one level up
    void moveFolder(const QString &id, const QString &parent);
    void moveObject(const QString &id, const QString &folder);

    QList<Backlink> backlinks(const QString &id) const;
    QList<const Object *> objectsForDocument(const QString &documentId) const;
    // copies a picture into objects/images; returns the file name to store
    QString importPicture(const QString &objectId, const QString &fieldId, const QString &file, QString *error = nullptr);
    QString picturePath(const QString &name) const;

signals:
    void schemaChanged();
    void objectChanged(const QString &id, QObject *source);
    void objectAdded(const QString &id);
    void objectRemoved(const QString &id);
    void foldersChanged(const QString &category);
    void dirtied();

private:
    void touchSchema();
    QString fileOf(const Object &o) const;
    QString m_dir;
    Schema m_schema;
    bool m_schemaLoaded = false; // schema.json was there (or was written)
    QHash<QString, Object> m_objects;
    QSet<QString> m_dirty;
    QHash<QString, QString> m_removedFiles; // id -> file to delete at the next save
    QStringList m_orphanPictures;
    bool m_schemaDirty = false;
    QStringList m_warnings;
};

} // namespace objects
} // namespace qb
