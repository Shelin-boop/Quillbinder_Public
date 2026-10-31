#include "core/SettingsFile.h"

#include "AppInfo.h"
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Objects.h"
#include "core/PageSetup.h"
#include "core/Project.h"
#include "core/ProjectTypes.h"
#include "text/Script.h"
#include "text/Styles.h"
#include "text/TextProps.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>

namespace qb {
namespace settingsfile {

static const char *const kFormat = "quillbinder-project-settings";

QString key(Section s)
{
    switch (s) {
    case Labels: return QStringLiteral("labels");
    case Status: return QStringLiteral("status");
    case SectionTypes: return QStringLiteral("sectionTypes");
    case CustomMetadata: return QStringLiteral("customMetadata");
    case Keywords: return QStringLiteral("keywords");
    case Formatting: return QStringLiteral("formatting");
    case Styles: return QStringLiteral("styles");
    case ScriptFormat: return QStringLiteral("scriptFormat");
    case Targets: return QStringLiteral("targets");
    case Compile: return QStringLiteral("compile");
    case PageSetup: return QStringLiteral("pageSetup");
    case Printing: return QStringLiteral("printing");
    case Look: return QStringLiteral("look");
    case ObjectTemplates: return QStringLiteral("objectTemplates");
    case SectionCount: break;
    }
    return QString();
}

QString title(Section s)
{
    switch (s) {
    case Labels: return QObject::tr("Labels");
    case Status: return QObject::tr("Status");
    case SectionTypes: return QObject::tr("Section Types");
    case CustomMetadata: return QObject::tr("Custom Metadata");
    case Keywords: return QObject::tr("Keywords");
    case Formatting: return QObject::tr("Formatting");
    case Styles: return QObject::tr("Styles");
    case ScriptFormat: return QObject::tr("Script Format");
    case Targets: return QObject::tr("Targets");
    case Compile: return QObject::tr("Compile Settings");
    case PageSetup: return QObject::tr("Page View and Page Size");
    case Printing: return QObject::tr("Print Settings");
    case Look: return QObject::tr("Look (Colors, Fonts, Width, Border)");
    case ObjectTemplates: return QObject::tr("Object Templates and Colors");
    case SectionCount: break;
    }
    return QString();
}

// ------------------------------------------------------------------ capture

static QString colorStr(const QColor &c) { return c.isValid() ? c.name(QColor::HexRgb) : QString(); }

static QString sectionTypeName(const Project *p, const QString &id)
{
    const SectionType *st = p->sectionTypeById(id);
    return st ? st->name : QString();
}

static QJsonArray typeNames(const Project *p, const QStringList &ids)
{
    QJsonArray a;
    for (const QString &id : ids)
        a.append(sectionTypeName(p, id));
    return a;
}

static QString fieldType(CustomFieldType t)
{
    switch (t) {
    case CustomFieldType::Checkbox: return QStringLiteral("checkbox");
    case CustomFieldType::List: return QStringLiteral("list");
    case CustomFieldType::Date: return QStringLiteral("date");
    default: return QStringLiteral("text");
    }
}

static CustomFieldType fieldTypeOf(const QString &s)
{
    return s == QLatin1String("checkbox") ? CustomFieldType::Checkbox
           : s == QLatin1String("list")   ? CustomFieldType::List
           : s == QLatin1String("date")   ? CustomFieldType::Date
                                          : CustomFieldType::Text;
}

static QString itemTitle(const Project *p, const QString &id)
{
    const BinderItem *it = id.isEmpty() ? nullptr : p->item(id);
    return it ? it->title : QString();
}

QJsonObject capture(Project *p)
{
    QJsonObject s;
    {
        QJsonArray items;
        for (const Label &l : p->labels)
            items.append(QJsonObject{{QStringLiteral("name"), l.name}, {QStringLiteral("color"), colorStr(l.color)}});
        s.insert(key(Labels), QJsonObject{{QStringLiteral("title"), p->labelTitle}, {QStringLiteral("items"), items}});
    }
    {
        QJsonArray items;
        for (const qb::Status &st : p->statuses)
            items.append(st.name);
        s.insert(key(Status), QJsonObject{{QStringLiteral("title"), p->statusTitle}, {QStringLiteral("items"), items}});
    }
    {
        QJsonArray types;
        for (const SectionType &t : p->sectionTypes)
            types.append(t.name);
        s.insert(key(SectionTypes), QJsonObject{{QStringLiteral("types"), types},
                                                {QStringLiteral("folders"), typeNames(p, p->structureDefaults.folders)},
                                                {QStringLiteral("fileGroups"), typeNames(p, p->structureDefaults.fileGroups)},
                                                {QStringLiteral("files"), typeNames(p, p->structureDefaults.files)}});
    }
    {
        QJsonArray fields;
        for (const CustomField &f : p->customFields)
            fields.append(QJsonObject{{QStringLiteral("title"), f.title},
                                      {QStringLiteral("type"), fieldType(f.type)},
                                      {QStringLiteral("default"), f.defaultValue},
                                      {QStringLiteral("options"), QJsonArray::fromStringList(f.options)},
                                      {QStringLiteral("wrap"), f.wrap}});
        s.insert(key(CustomMetadata), fields);
    }
    {
        QJsonArray kws;
        for (const Keyword &k : p->keywords) {
            const Keyword *parent = k.parentId.isEmpty() ? nullptr : p->keywordById(k.parentId);
            kws.append(QJsonObject{{QStringLiteral("name"), k.name},
                                   {QStringLiteral("color"), colorStr(k.color)},
                                   {QStringLiteral("parent"), parent ? parent->name : QString()}});
        }
        s.insert(key(Keywords), kws);
    }
    {
        const qb::Formatting &f = p->formatting;
        s.insert(key(Formatting), QJsonObject{{QStringLiteral("font"), f.fontFamily},
                                              {QStringLiteral("size"), f.fontSize},
                                              {QStringLiteral("lineSpacing"), f.lineSpacing},
                                              {QStringLiteral("indent"), f.firstLineIndent},
                                              {QStringLiteral("spacing"), f.paragraphSpacing},
                                              {QStringLiteral("justify"), f.align == Qt::AlignJustify},
                                              {QStringLiteral("align"), alignmentName(f.align)}});
        QJsonObject so = f.styles ? QJsonObject{{QStringLiteral("xml"), styles::toXmlString(*f.styles)}}
                                  : QJsonObject{{QStringLiteral("builtIn"), true}};
        // the chosen style list of the format bar (0.40.3)
        if (!p->styleListOrder.isEmpty() || !p->styleListHidden.isEmpty()) {
            so.insert(QStringLiteral("list"), QJsonArray::fromStringList(p->styleListOrder));
            so.insert(QStringLiteral("hidden"), QJsonArray::fromStringList(p->styleListHidden));
        }
        s.insert(key(Styles), so);
        if (f.script)
            s.insert(key(ScriptFormat), QJsonObject{{QStringLiteral("format"), script::toJson(f.script->format)}});
        else
            s.insert(key(ScriptFormat), QJsonObject{{QStringLiteral("builtIn"), true}});
    }
    {
        const qb::Targets &t = p->targets;
        s.insert(key(Targets), QJsonObject{{QStringLiteral("draft"), t.draftTarget},
                                           {QStringLiteral("draftChars"), t.draftChars},
                                           {QStringLiteral("deadline"), t.deadline.toString(Qt::ISODate)},
                                           {QStringLiteral("session"), t.sessionTarget},
                                           {QStringLiteral("sessionChars"), t.sessionChars},
                                           {QStringLiteral("sessionReset"), t.sessionReset},
                                           {QStringLiteral("countAnywhere"), t.countAnywhere},
                                           {QStringLiteral("autoFromDeadline"), t.autoFromDeadline},
                                           {QStringLiteral("writingDays"), t.writingDays}});
    }
    {
        const CompileProjectSettings &c = p->compile;
        QJsonArray assign;
        for (auto it = c.layoutAssignments.cbegin(); it != c.layoutAssignments.cend(); ++it) {
            const QString format = it.key().section(QLatin1Char('|'), 0, 0);
            const QString type = sectionTypeName(p, it.key().section(QLatin1Char('|'), 1));
            if (!type.isEmpty())
                assign.append(QJsonObject{{QStringLiteral("format"), format}, {QStringLiteral("type"), type},
                                          {QStringLiteral("layout"), it.value()}});
        }
        QJsonArray repl;
        for (const CompileReplacement &r : c.replacements)
            repl.append(QJsonObject{{QStringLiteral("find"), r.find},       {QStringLiteral("with"), r.replace},
                                    {QStringLiteral("regex"), r.regex},     {QStringLiteral("case"), r.matchCase},
                                    {QStringLiteral("whole"), r.wholeWords}, {QStringLiteral("on"), r.enabled}});
        s.insert(key(Compile), QJsonObject{{QStringLiteral("format"), c.formatName},
                                           {QStringLiteral("exporter"), c.exporterId},
                                           {QStringLiteral("group"), itemTitle(p, c.compileGroupId)},
                                           {QStringLiteral("complete"), c.treatAsComplete},
                                           {QStringLiteral("groupText"), c.includeGroupText},
                                           {QStringLiteral("front"), itemTitle(p, c.frontMatterId)},
                                           {QStringLiteral("back"), itemTitle(p, c.backMatterId)},
                                           {QStringLiteral("titleOverride"), c.titleOverride},
                                           {QStringLiteral("authorOverride"), c.authorOverride},
                                           {QStringLiteral("subtitle"), c.subtitle},
                                           {QStringLiteral("subject"), c.subject},
                                           {QStringLiteral("keywords"), c.keywords},
                                           {QStringLiteral("publisher"), c.publisher},
                                           {QStringLiteral("description"), c.description},
                                           {QStringLiteral("language"), c.language},
                                           {QStringLiteral("fontOverride"), c.fontOverride},
                                           {QStringLiteral("objectSheets"), c.objectSheets},
                                           {QStringLiteral("objectCategories"), QJsonArray::fromStringList(c.objectCategories)},
                                           {QStringLiteral("assignments"), assign},
                                           {QStringLiteral("replacements"), repl}});
    }
    {
        const qb::PageSetup ps = p->docs() ? p->docs()->pageSetup()
                                           : qb::PageSetup::fromVariant(p->uiState.value(QStringLiteral("pageSetup")).toMap(),
                                                                        qb::PageSetup::standard());
        s.insert(key(PageSetup), QJsonObject{{QStringLiteral("pageView"), p->uiState.value(QStringLiteral("pageView"), false).toBool()},
                                             {QStringLiteral("page"), QJsonObject::fromVariantMap(ps.toVariant())}});
    }
    s.insert(key(Printing), QJsonObject::fromVariantMap(p->uiState.value(QStringLiteral("printSettings")).toMap()));
    {
        QFile f(QDir(p->settingsDir()).filePath(QStringLiteral("look.json")));
        QJsonObject look;
        if (f.open(QIODevice::ReadOnly))
            look = QJsonDocument::fromJson(f.readAll()).object();
        s.insert(key(Look), look);
    }
    s.insert(key(ObjectTemplates), p->objects()->schema().toJson(false));
    return s;
}

QJsonObject document(Project *p)
{
    return QJsonObject{{QStringLiteral("format"), QLatin1String(kFormat)},
                       {QStringLiteral("version"), 1},
                       {QStringLiteral("app"), QStringLiteral(QB_APP_NAME " " QB_APP_VERSION)},
                       {QStringLiteral("project"), p->title()},
                       {QStringLiteral("saved"), QDateTime::currentDateTime().toString(Qt::ISODate)},
                       {QStringLiteral("sections"), capture(p)}};
}

bool save(const QString &file, Project *p, QString *error)
{
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(document(p)).toJson()) < 0 || !f.commit()) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(file, f.errorString());
        return false;
    }
    return true;
}

bool load(const QString &file, QJsonObject *doc, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open %1: %2").arg(file, f.errorString());
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject() || d.object().value(QStringLiteral("format")).toString() != QLatin1String(kFormat)
        || !d.object().value(QStringLiteral("sections")).isObject()) {
        if (error)
            *error = QObject::tr("This is not a file of project settings saved by Quillbinder.");
        return false;
    }
    *doc = d.object();
    return true;
}

// ------------------------------------------------------------------ compare

bool same(const QJsonValue &a, const QJsonValue &b) { return a == b; }

static QStringList namesOf(const QJsonValue &v, const QString &field = QString())
{
    QStringList out;
    const QJsonArray a = v.isObject() ? v.toObject().value(QStringLiteral("items")).toArray() : v.toArray();
    for (const QJsonValue &x : a)
        out << (x.isObject() ? x.toObject().value(field.isEmpty() ? QStringLiteral("name") : field).toString() : x.toString());
    return out;
}

static QString listDiff(const QStringList &project, const QStringList &file)
{
    QStringList onlyFile, onlyProject;
    for (const QString &n : file)
        if (!project.contains(n, Qt::CaseInsensitive))
            onlyFile << n;
    for (const QString &n : project)
        if (!file.contains(n, Qt::CaseInsensitive))
            onlyProject << n;
    QStringList parts;
    if (!onlyFile.isEmpty())
        parts << QObject::tr("only in the file: %1").arg(onlyFile.join(QStringLiteral(", ")));
    if (!onlyProject.isEmpty())
        parts << QObject::tr("only in the project: %1").arg(onlyProject.join(QStringLiteral(", ")));
    if (parts.isEmpty() && project != file)
        parts << QObject::tr("the same names in another order, or other colors");
    return parts.join(QStringLiteral("; "));
}

static QString keyDiff(const QJsonObject &project, const QJsonObject &file)
{
    QStringList keys;
    QSet<QString> all;
    for (const QString &k : project.keys())
        all.insert(k);
    for (const QString &k : file.keys())
        all.insert(k);
    QStringList sorted = all.values();
    sorted.sort();
    for (const QString &k : sorted)
        if (project.value(k) != file.value(k)) {
            const QJsonValue pv = project.value(k), fv = file.value(k);
            if ((pv.isString() || pv.isDouble() || pv.isBool() || pv.isUndefined())
                && (fv.isString() || fv.isDouble() || fv.isBool() || fv.isUndefined()))
                keys << QStringLiteral("%1: %2 → %3").arg(k, pv.toVariant().toString(), fv.toVariant().toString());
            else
                keys << k;
        }
    return keys.join(QStringLiteral("; "));
}

QString differences(Section s, const QJsonValue &project, const QJsonValue &file)
{
    if (same(project, file))
        return QObject::tr("The same in the project and in the file.");
    switch (s) {
    case Labels:
    case Status: return listDiff(namesOf(project), namesOf(file));
    case Keywords: return listDiff(namesOf(project), namesOf(file));
    case CustomMetadata: return listDiff(namesOf(project, QStringLiteral("title")), namesOf(file, QStringLiteral("title")));
    case SectionTypes:
        return listDiff(namesOf(project.toObject().value(QStringLiteral("types"))), namesOf(file.toObject().value(QStringLiteral("types"))));
    case Styles: {
        StyleSheet a, b;
        bool ba = false, bb = false;
        stylesFrom(project, &a, &ba);
        stylesFrom(file, &b, &bb);
        QStringList na, nb, changed;
        for (const Style &st : a.styles)
            na << st.name;
        for (const Style &st : b.styles) {
            nb << st.name;
            const Style *o = a.find(st.name);
            if (o && o->name == st.name && !o->sameFormatting(st))
                changed << st.name;
        }
        QString d = listDiff(na, nb);
        if (!changed.isEmpty())
            d += (d.isEmpty() ? QString() : QStringLiteral("; ")) + QObject::tr("defined differently: %1").arg(changed.join(QStringLiteral(", ")));
        return d.isEmpty() ? QObject::tr("the styles differ") : d;
    }
    case ObjectTemplates: {
        QStringList a, b;
        for (const QJsonValue &c : project.toObject().value(QStringLiteral("categories")).toArray())
            a << c.toObject().value(QStringLiteral("name")).toString();
        for (const QJsonValue &c : file.toObject().value(QStringLiteral("categories")).toArray())
            b << c.toObject().value(QStringLiteral("name")).toString();
        const QString d = listDiff(a, b);
        return d.isEmpty() ? QObject::tr("fields or colors of the categories differ") : d;
    }
    case Look: {
        QJsonObject a = project.toObject(), b = file.toObject();
        a.remove(QStringLiteral("version"));
        b.remove(QStringLiteral("version"));
        return keyDiff(a, b);
    }
    case PageSetup: {
        QJsonObject a = project.toObject().value(QStringLiteral("page")).toObject(), b = file.toObject().value(QStringLiteral("page")).toObject();
        a.insert(QStringLiteral("pageView"), project.toObject().value(QStringLiteral("pageView")));
        b.insert(QStringLiteral("pageView"), file.toObject().value(QStringLiteral("pageView")));
        return keyDiff(a, b);
    }
    default: return keyDiff(project.toObject(), file.toObject());
    }
}

// ------------------------------------------------------------------ apply

// How many documents use a label / status / ... (for the notes).
template<typename F>
static int usedBy(Project *p, F pred)
{
    int n = 0;
    for (BinderItem *it : p->allItems())
        if (pred(it))
            ++n;
    return n;
}

static void applyLabels(Project *p, const QJsonObject &o, QStringList *notes)
{
    QList<Label> out;
    int next = p->nextLabelId();
    QSet<int> taken;
    for (const QJsonValue &v : o.value(QStringLiteral("items")).toArray()) {
        const QJsonObject x = v.toObject();
        const QString name = x.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        Label l;
        l.name = name;
        l.color = QColor(x.value(QStringLiteral("color")).toString());
        l.id = -1;
        for (const Label &old : p->labels)
            if (old.name.compare(name, Qt::CaseInsensitive) == 0 && !taken.contains(old.id))
                l.id = old.id;
        if (l.id < 0)
            l.id = next++;
        taken.insert(l.id);
        out << l;
    }
    for (const Label &old : p->labels) {
        if (taken.contains(old.id))
            continue;
        const int n = usedBy(p, [&](BinderItem *it) { return it->label == old.id; });
        if (n > 0) {
            out << old;
            if (notes)
                *notes << QObject::tr("Label “%1” kept: %n document(s) have it.", nullptr, n).arg(old.name);
        }
    }
    p->labelTitle = o.value(QStringLiteral("title")).toString(p->labelTitle);
    p->labels = out;
}

static void applyStatus(Project *p, const QJsonObject &o, QStringList *notes)
{
    QList<qb::Status> out;
    int next = p->nextStatusId();
    QSet<int> taken;
    for (const QJsonValue &v : o.value(QStringLiteral("items")).toArray()) {
        const QString name = v.toString();
        if (name.isEmpty())
            continue;
        qb::Status st;
        st.name = name;
        st.id = -1;
        for (const qb::Status &old : p->statuses)
            if (old.name.compare(name, Qt::CaseInsensitive) == 0 && !taken.contains(old.id))
                st.id = old.id;
        if (st.id < 0)
            st.id = next++;
        taken.insert(st.id);
        out << st;
    }
    for (const qb::Status &old : p->statuses) {
        if (taken.contains(old.id))
            continue;
        const int n = usedBy(p, [&](BinderItem *it) { return it->status == old.id; });
        if (n > 0) {
            out << old;
            if (notes)
                *notes << QObject::tr("Status “%1” kept: %n document(s) have it.", nullptr, n).arg(old.name);
        }
    }
    p->statusTitle = o.value(QStringLiteral("title")).toString(p->statusTitle);
    p->statuses = out;
}

static void applySectionTypes(Project *p, const QJsonObject &o, QStringList *notes)
{
    QList<SectionType> out;
    QSet<QString> taken;
    auto idFor = [&](const QString &name) {
        for (const SectionType &t : out)
            if (t.name.compare(name, Qt::CaseInsensitive) == 0)
                return t.id;
        return QString();
    };
    for (const QJsonValue &v : o.value(QStringLiteral("types")).toArray()) {
        const QString name = v.toString();
        if (name.isEmpty() || !idFor(name).isEmpty())
            continue;
        SectionType t;
        t.name = name;
        for (const SectionType &old : p->sectionTypes)
            if (old.name.compare(name, Qt::CaseInsensitive) == 0)
                t.id = old.id;
        if (t.id.isEmpty())
            t.id = newId();
        taken.insert(t.id);
        out << t;
    }
    for (const SectionType &old : p->sectionTypes) {
        if (taken.contains(old.id))
            continue;
        const int n = usedBy(p, [&](BinderItem *it) { return it->sectionType == old.id; });
        if (n > 0) {
            out << old;
            taken.insert(old.id);
            if (notes)
                *notes << QObject::tr("Section type “%1” kept: %n document(s) have it.", nullptr, n).arg(old.name);
        }
    }
    p->sectionTypes = out;
    auto levels = [&](const QString &k) {
        QStringList ids;
        for (const QJsonValue &v : o.value(k).toArray()) {
            const QString id = idFor(v.toString());
            if (!id.isEmpty())
                ids << id;
        }
        return ids;
    };
    p->structureDefaults.folders = levels(QStringLiteral("folders"));
    p->structureDefaults.fileGroups = levels(QStringLiteral("fileGroups"));
    p->structureDefaults.files = levels(QStringLiteral("files"));
}

static void applyCustomMetadata(Project *p, const QJsonArray &a, QStringList *notes)
{
    QList<CustomField> out;
    QSet<QString> taken;
    for (const QJsonValue &v : a) {
        const QJsonObject x = v.toObject();
        CustomField f;
        f.title = x.value(QStringLiteral("title")).toString();
        if (f.title.isEmpty())
            continue;
        f.type = fieldTypeOf(x.value(QStringLiteral("type")).toString());
        f.defaultValue = x.value(QStringLiteral("default")).toString();
        for (const QJsonValue &opt : x.value(QStringLiteral("options")).toArray())
            f.options << opt.toString();
        f.wrap = x.value(QStringLiteral("wrap")).toBool(true);
        for (const CustomField &old : p->customFields)
            if (old.title.compare(f.title, Qt::CaseInsensitive) == 0 && !taken.contains(old.id))
                f.id = old.id;
        if (f.id.isEmpty())
            f.id = newId();
        taken.insert(f.id);
        out << f;
    }
    for (const CustomField &old : p->customFields) {
        if (taken.contains(old.id))
            continue;
        const int n = usedBy(p, [&](BinderItem *it) { return !it->custom.value(old.id).isEmpty(); });
        if (n > 0) {
            out << old;
            if (notes)
                *notes << QObject::tr("Custom metadata “%1” kept: %n document(s) have a value in it.", nullptr, n).arg(old.title);
        }
    }
    p->customFields = out;
}

static void applyKeywords(Project *p, const QJsonArray &a, QStringList *notes)
{
    QList<Keyword> out;
    QSet<QString> taken;
    QHash<QString, QString> parentName; // id -> name of its parent, set after all are known
    for (const QJsonValue &v : a) {
        const QJsonObject x = v.toObject();
        Keyword k;
        k.name = x.value(QStringLiteral("name")).toString();
        if (k.name.isEmpty())
            continue;
        k.color = QColor(x.value(QStringLiteral("color")).toString());
        for (const Keyword &old : p->keywords)
            if (old.name.compare(k.name, Qt::CaseInsensitive) == 0 && !taken.contains(old.id))
                k.id = old.id;
        if (k.id.isEmpty())
            k.id = newId();
        taken.insert(k.id);
        parentName.insert(k.id, x.value(QStringLiteral("parent")).toString());
        out << k;
    }
    for (const Keyword &old : p->keywords) {
        if (taken.contains(old.id))
            continue;
        const int n = usedBy(p, [&](BinderItem *it) { return it->keywords.contains(old.id); });
        if (n > 0) {
            Keyword k = old;
            out << k;
            taken.insert(k.id);
            const Keyword *par = old.parentId.isEmpty() ? nullptr : p->keywordById(old.parentId);
            parentName.insert(k.id, par ? par->name : QString());
            if (notes)
                *notes << QObject::tr("Keyword “%1” kept: %n document(s) have it.", nullptr, n).arg(old.name);
        }
    }
    for (Keyword &k : out) {
        const QString pn = parentName.value(k.id);
        k.parentId.clear();
        for (const Keyword &cand : out)
            if (!pn.isEmpty() && cand.name.compare(pn, Qt::CaseInsensitive) == 0 && cand.id != k.id)
                k.parentId = cand.id;
    }
    p->keywords = out;
}

static void applyTargets(Project *p, const QJsonObject &o)
{
    qb::Targets &t = p->targets;
    t.draftTarget = o.value(QStringLiteral("draft")).toInt(t.draftTarget);
    t.draftChars = o.value(QStringLiteral("draftChars")).toBool(t.draftChars);
    t.deadline = QDate::fromString(o.value(QStringLiteral("deadline")).toString(), Qt::ISODate);
    t.sessionTarget = o.value(QStringLiteral("session")).toInt(t.sessionTarget);
    t.sessionChars = o.value(QStringLiteral("sessionChars")).toBool(t.sessionChars);
    t.sessionReset = o.value(QStringLiteral("sessionReset")).toString(t.sessionReset);
    t.countAnywhere = o.value(QStringLiteral("countAnywhere")).toBool(t.countAnywhere);
    t.autoFromDeadline = o.value(QStringLiteral("autoFromDeadline")).toBool(t.autoFromDeadline);
    t.writingDays = o.value(QStringLiteral("writingDays")).toString(t.writingDays);
}

static QString itemByTitle(Project *p, const QString &title, const QString &what, QStringList *notes)
{
    if (title.isEmpty())
        return QString();
    for (BinderItem *it : p->allItems())
        if (it->title == title && !p->isInTrash(it))
            return it->id;
    if (notes)
        *notes << QObject::tr("Compile: no document or folder “%1” for the %2 here; the project's choice stays.").arg(title, what);
    return QStringLiteral("\x01"); // not found
}

static void applyCompile(Project *p, const QJsonObject &o, QStringList *notes)
{
    CompileProjectSettings &c = p->compile;
    c.formatName = o.value(QStringLiteral("format")).toString(c.formatName);
    c.exporterId = o.value(QStringLiteral("exporter")).toString(c.exporterId);
    c.treatAsComplete = o.value(QStringLiteral("complete")).toBool(c.treatAsComplete);
    c.includeGroupText = o.value(QStringLiteral("groupText")).toBool(c.includeGroupText);
    auto ref = [&](const char *k, QString *id, const QString &what) {
        const QString found = itemByTitle(p, o.value(QLatin1String(k)).toString(), what, notes);
        if (found == QLatin1String("\x01"))
            return;
        *id = found;
    };
    ref("group", &c.compileGroupId, QObject::tr("compile group"));
    ref("front", &c.frontMatterId, QObject::tr("front matter"));
    ref("back", &c.backMatterId, QObject::tr("back matter"));
    c.titleOverride = o.value(QStringLiteral("titleOverride")).toString();
    c.authorOverride = o.value(QStringLiteral("authorOverride")).toString();
    c.subtitle = o.value(QStringLiteral("subtitle")).toString();
    c.subject = o.value(QStringLiteral("subject")).toString();
    c.keywords = o.value(QStringLiteral("keywords")).toString();
    c.publisher = o.value(QStringLiteral("publisher")).toString();
    c.description = o.value(QStringLiteral("description")).toString();
    c.language = o.value(QStringLiteral("language")).toString();
    c.fontOverride = o.value(QStringLiteral("fontOverride")).toString();
    c.objectSheets = o.value(QStringLiteral("objectSheets")).toBool();
    c.objectCategories.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("objectCategories")).toArray())
        c.objectCategories << v.toString();
    c.layoutAssignments.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("assignments")).toArray()) {
        const QJsonObject a = v.toObject();
        const QString type = a.value(QStringLiteral("type")).toString();
        QString id;
        for (const SectionType &t : p->sectionTypes)
            if (t.name.compare(type, Qt::CaseInsensitive) == 0)
                id = t.id;
        if (id.isEmpty())
            continue;
        c.layoutAssignments.insert(a.value(QStringLiteral("format")).toString() + QLatin1Char('|') + id,
                                   a.value(QStringLiteral("layout")).toString());
    }
    c.replacements.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("replacements")).toArray()) {
        const QJsonObject a = v.toObject();
        CompileReplacement r;
        r.find = a.value(QStringLiteral("find")).toString();
        r.replace = a.value(QStringLiteral("with")).toString();
        r.regex = a.value(QStringLiteral("regex")).toBool();
        r.matchCase = a.value(QStringLiteral("case")).toBool(true);
        r.wholeWords = a.value(QStringLiteral("whole")).toBool();
        r.enabled = a.value(QStringLiteral("on")).toBool(true);
        if (!r.find.isEmpty())
            c.replacements << r;
    }
}

static void applyObjectTemplates(Project *p, const QJsonObject &o, QStringList *notes)
{
    objects::Store *store = p->objects();
    objects::Schema in = objects::Schema::fromJson(o);
    objects::Schema now = store->schema();
    in.folders = now.folders; // folders hold objects: they stay
    // categories that have objects stay, also when the file does not have them
    for (const objects::Category &c : now.categories) {
        if (in.category(c.id))
            continue;
        bool sameName = false;
        for (objects::Category &ic : in.categories)
            if (ic.name.compare(c.name, Qt::CaseInsensitive) == 0) {
                // the same category under another id (another project): it keeps this project's id
                ic.id = c.id;
                sameName = true;
            }
        if (sameName)
            continue;
        const int n = int(store->objects(c.id).size());
        if (n > 0) {
            in.categories << c;
            if (notes)
                *notes << QObject::tr("Object category “%1” kept: it has %n object(s).", nullptr, n).arg(c.name);
        }
    }
    store->setSchema(in);
}

void applyData(Project *p, Section s, const QJsonValue &value, QStringList *notes)
{
    switch (s) {
    case Labels: applyLabels(p, value.toObject(), notes); break;
    case Status: applyStatus(p, value.toObject(), notes); break;
    case SectionTypes: applySectionTypes(p, value.toObject(), notes); break;
    case CustomMetadata: applyCustomMetadata(p, value.toArray(), notes); break;
    case Keywords: applyKeywords(p, value.toArray(), notes); break;
    case Targets: applyTargets(p, value.toObject()); break;
    case Compile: applyCompile(p, value.toObject(), notes); break;
    case PageSetup: {
        const QJsonObject o = value.toObject();
        const qb::PageSetup ps = qb::PageSetup::fromVariant(o.value(QStringLiteral("page")).toObject().toVariantMap(), qb::PageSetup::standard());
        if (p->docs()) {
            p->docs()->setPageSetup(ps);
            p->docs()->setPageView(o.value(QStringLiteral("pageView")).toBool());
        } else {
            p->uiState[QStringLiteral("pageSetup")] = ps.toVariant();
            p->uiState[QStringLiteral("pageView")] = o.value(QStringLiteral("pageView")).toBool();
        }
        break;
    }
    case Printing:
        if (value.toObject().isEmpty())
            p->uiState.remove(QStringLiteral("printSettings"));
        else
            p->uiState[QStringLiteral("printSettings")] = value.toObject().toVariantMap();
        break;
    case ObjectTemplates: applyObjectTemplates(p, value.toObject(), notes); break;
    default: return; // the window takes these over
    }
    p->markIndexDirty();
    p->notifySettingsChanged();
}

// ------------------------------------------------------------------ for the window

void formattingFrom(const QJsonValue &v, ::qb::Formatting *f)
{
    const QJsonObject o = v.toObject();
    f->fontFamily = o.value(QStringLiteral("font")).toString(f->fontFamily);
    f->fontSize = o.value(QStringLiteral("size")).toDouble(f->fontSize);
    f->lineSpacing = o.value(QStringLiteral("lineSpacing")).toDouble(f->lineSpacing);
    f->firstLineIndent = o.value(QStringLiteral("indent")).toDouble(f->firstLineIndent);
    f->paragraphSpacing = o.value(QStringLiteral("spacing")).toDouble(f->paragraphSpacing);
    // "align" since 0.40.3; files of older versions have "justify" only
    if (o.contains(QStringLiteral("align")))
        f->align = alignmentFromName(o.value(QStringLiteral("align")).toString(), f->align);
    else if (o.contains(QStringLiteral("justify")))
        f->align = o.value(QStringLiteral("justify")).toBool() ? Qt::AlignJustify : Qt::AlignLeft;
}

bool stylesFrom(const QJsonValue &v, StyleSheet *sheet, bool *builtIn)
{
    const QJsonObject o = v.toObject();
    *builtIn = o.value(QStringLiteral("builtIn")).toBool();
    if (*builtIn) {
        *sheet = styles::defaults();
        return true;
    }
    return styles::fromXmlString(o.value(QStringLiteral("xml")).toString(), sheet);
}

QStringList stylesInUse(Project *p)
{
    QSet<QString> used;
    for (BinderItem *it : p->allItems()) {
        if (!it->hasText())
            continue;
        p->docs()->editStored(it->id, false, [&](QTextDocument *doc) {
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                const QString ps = b.blockFormat().stringProperty(props::Style);
                if (!ps.isEmpty())
                    used.insert(ps);
                for (auto f = b.begin(); !f.atEnd(); ++f) {
                    const QString cs = f.fragment().charFormat().stringProperty(props::CharStyle);
                    if (!cs.isEmpty())
                        used.insert(cs);
                }
            }
            return 0;
        });
    }
    QStringList out = used.values();
    out.sort();
    return out;
}

} // namespace settingsfile
} // namespace qb
