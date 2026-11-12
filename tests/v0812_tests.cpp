// Scripted run for version 0.81.2 beta: keywords, custom metadata and collections in Scrivener format.
//  - read as Scrivener 3 for Windows writes them: Scrivener's ids kept as this program's, values in this program's
//    form (a checkbox "Yes" / "No", a list's option ID, a date with time and zone), collections as one
//    comma-separated list, inline notes kept as text in the RTF;
//  - written into the .scriv in the same form, with the file's ids; a .scriv opened and saved without a change stays
//    as it was; ids stay the same from one save to the next;
//  - a re-read after a change elsewhere keeps what was made here (it is in the .scriv now) and does not bring back
//    what was deleted in Scrivener; a working copy of an older version is joined with the .scriv once;
//  - inline notes kept as text: converted when read, in a working copy of an older version, and in projects
//    imported before (Quillbinder format);
//  - the inspector shows the values.
// QB_SCRIV_REAL=<folder.scriv> also runs the checks on a copy of a real project (never changed itself).
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0812_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"

#include "AppInfo.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QXmlStreamReader>
#include <cstdio>
#include <functional>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("        %s\n", qPrintable(detail.left(1500)));
    }
    std::fflush(stdout);
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void writeAll(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

static void copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString t = to + QLatin1Char('/') + QDir(from).relativeFilePath(f);
        QDir().mkpath(QFileInfo(t).absolutePath());
        QFile::copy(f, t);
    }
}

static QDomDocument scrivxDom(const QString &scriv)
{
    QDomDocument d;
    d.setContent(readAll(scrivener::scrivxIn(scriv)));
    return d;
}

static QDomElement itemEl(const QDomDocument &d, const QString &uuid)
{
    const QDomNodeList list = d.elementsByTagName(QStringLiteral("BinderItem"));
    for (int i = 0; i < list.size(); ++i) {
        const QDomElement e = list.at(i).toElement();
        if (e.attribute(QStringLiteral("UUID")).compare(uuid, Qt::CaseInsensitive) == 0)
            return e;
    }
    return QDomElement();
}

static QStringList itemKeywordIds(const QDomElement &item)
{
    QStringList out;
    for (QDomElement k = item.firstChildElement(QStringLiteral("Keywords")).firstChildElement(); !k.isNull(); k = k.nextSiblingElement())
        out << k.text().trimmed();
    return out;
}

static QMap<QString, QString> itemValues(const QDomElement &item)
{
    QMap<QString, QString> out;
    const QDomElement cm = item.firstChildElement(QStringLiteral("MetaData")).firstChildElement(QStringLiteral("CustomMetaData"));
    for (QDomElement mi = cm.firstChildElement(QStringLiteral("MetaDataItem")); !mi.isNull(); mi = mi.nextSiblingElement())
        out.insert(mi.firstChildElement(QStringLiteral("FieldID")).text(), mi.firstChildElement(QStringLiteral("Value")).text());
    return out;
}

static QStringList childTags(const QDomElement &e)
{
    QStringList out;
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        out << c.tagName();
    return out;
}

static int countKind(const QTextDocument *doc, NoteKind kind)
{
    if (!doc)
        return -1;
    int n = 0;
    QString lastId;
    bool inRun = false;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            const bool is = NoteKind(cf.intProperty(props::NoteKind)) == kind;
            const QString id = cf.stringProperty(props::NoteId);
            if (is && (!inRun || id != lastId))
                ++n;
            inRun = is;
            lastId = id;
        }
    return n;
}

// The project file as Scrivener reads it: elements, sorted attributes (without the root's stamps), text; white space
// only where it is all of a leaf's text (the program compares the same way before it writes)
static QString canonical(const QByteArray &x)
{
    QXmlStreamReader r(x);
    QString out;
    int depth = 0;
    bool leaf = false;
    QString pending;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            leaf = true;
            pending.clear();
            ++depth;
            QStringList attrs;
            for (const QXmlStreamAttribute &a : r.attributes()) {
                const QString n = a.qualifiedName().toString();
                if (depth == 1 && (n == QLatin1String("Modified") || n == QLatin1String("ModID") || n == QLatin1String("Device")))
                    continue;
                attrs << n + QLatin1Char('=') + a.value().toString();
            }
            attrs.sort();
            out += QLatin1Char('<') + r.qualifiedName().toString() + QLatin1Char(' ') + attrs.join(QLatin1Char('|')) + QLatin1String(">\n");
        } else if (r.isEndElement()) {
            if (leaf)
                out += pending;
            leaf = false;
            pending.clear();
            --depth;
            out += QStringLiteral("</>\n");
        } else if (r.isCharacters()) {
            if (!r.isWhitespace()) {
                out += r.text().toString();
                leaf = false;
            } else if (leaf) {
                pending += r.text().toString();
            }
        }
    }
    return r.hasError() ? QString::fromUtf8(x) : out;
}

static bool sameFile(const QByteArray &a, const QByteArray &b) { return canonical(a) == canonical(b); }

// The first difference of two project files as Scrivener reads them (for the details of a failed check)
static QString firstDifference(const QByteArray &a, const QByteArray &b)
{
    if (sameFile(a, b))
        return QString();
    const QStringList la = canonical(a).split(QLatin1Char('\n')), lb = canonical(b).split(QLatin1Char('\n'));
    for (int i = 0; i < qMax(la.size(), lb.size()); ++i)
        if (la.value(i) != lb.value(i)) {
            QStringList ctx;
            for (int j = qMax(0, i - 6); j < i; ++j)
                ctx << la.value(j).left(120);
            return QStringLiteral("after: %1\n  was: %2\n  now: %3").arg(ctx.join(QLatin1String(" ")), la.value(i).left(400), lb.value(i).left(400));
        }
    return QString();
}

// ----------------------------------------------------------------- a real project (QB_SCRIV_REAL)

static void realProject(const QString &source, const QString &W)
{
    const QString L = QStringLiteral("real project: ");
    const QString scriv = W + QLatin1String("/real/") + QFileInfo(source).fileName();
    copyTree(source, scriv);
    const QByteArray original = readAll(scrivener::scrivxIn(scriv));
    QDomDocument d;
    check(bool(d.setContent(original)), L + QStringLiteral("the project file is XML"));
    const QDomElement root = d.documentElement();

    // what the file has
    QHash<QString, QString> kwTitle;
    {
        const QDomNodeList list = root.firstChildElement(QStringLiteral("Keywords")).elementsByTagName(QStringLiteral("Keyword"));
        for (int i = 0; i < list.size(); ++i)
            kwTitle.insert(list.at(i).toElement().attribute(QStringLiteral("ID")),
                           list.at(i).toElement().firstChildElement(QStringLiteral("Title")).text());
    }
    struct Field { QString type, title; QHash<QString, QString> options; };
    QHash<QString, Field> fields;
    for (QDomElement mf = root.firstChildElement(QStringLiteral("CustomMetaDataSettings")).firstChildElement(); !mf.isNull();
         mf = mf.nextSiblingElement()) {
        Field f;
        f.type = mf.attribute(QStringLiteral("Type"));
        f.title = mf.firstChildElement(QStringLiteral("Title")).text();
        for (QDomElement o = mf.firstChildElement(QStringLiteral("ListOptions")).firstChildElement(); !o.isNull(); o = o.nextSiblingElement())
            f.options.insert(o.attribute(QStringLiteral("ID")), o.text());
        fields.insert(mf.attribute(QStringLiteral("ID")), f);
    }
    QHash<QString, QStringList> collections;
    for (QDomElement c = root.firstChildElement(QStringLiteral("Collections")).firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (c.attribute(QStringLiteral("Type")) == QLatin1String("Arbitrary"))
            collections.insert(c.attribute(QStringLiteral("ID")).toUpper(),
                               c.firstChildElement(QStringLiteral("BinderUUIDs")).text().split(QLatin1Char(','), Qt::SkipEmptyParts));
    QHash<QString, int> textNotes; // UUID -> inline notes kept as text
    for (const QFileInfo &fi : QDir(scriv + QLatin1String("/Files/Data")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QByteArray rtf = readAll(fi.absoluteFilePath() + QLatin1String("/content.rtf"));
        const int n = rtf.count("\\\\Scrv_fn=") + rtf.count("\\\\Scrv_annot");
        if (n > 0)
            textNotes.insert(fi.fileName().toUpper(), n);
    }
    std::printf("        (the file: %d keywords, %d fields, %d collections, %d documents with inline notes kept as text)\n",
                int(kwTitle.size()), int(fields.size()), int(collections.size()), int(textNotes.size()));

    // --- read (as File > Import does)
    ScrivenerImportReport rep;
    QString err;
    auto p = ScrivenerImporter::importProject(scriv, W + QLatin1String("/real-import"), QStringLiteral("Real"), StorageFormat::Rtf, &rep, &err);
    check(bool(p), L + QStringLiteral("imported"), err);
    if (!p)
        return;
    {
        QStringList bad;
        for (auto k = kwTitle.cbegin(); k != kwTitle.cend(); ++k) {
            const Keyword *kw = p->keywordById(k.key());
            if (!kw || kw->name != k.value())
                bad << k.key() + QLatin1Char('=') + k.value();
        }
        check(bad.isEmpty() && p->keywords.size() == kwTitle.size(), L + QStringLiteral("every keyword under Scrivener's id and title"),
              bad.join(QLatin1String(", ")));
    }
    {
        QStringList bad;
        int checked = 0;
        const QDomNodeList items = root.firstChildElement(QStringLiteral("Binder")).elementsByTagName(QStringLiteral("BinderItem"));
        for (int i = 0; i < items.size(); ++i) {
            const QDomElement e = items.at(i).toElement();
            const BinderItem *it = p->item(e.attribute(QStringLiteral("UUID")));
            if (!it)
                continue;
            if (itemKeywordIds(e) != it->keywords)
                bad << it->title + QStringLiteral(": keywords ") + itemKeywordIds(e).join(QLatin1Char(',')) + QStringLiteral(" / ")
                           + it->keywords.join(QLatin1Char(','));
            const QMap<QString, QString> vals = itemValues(e);
            for (auto v = vals.cbegin(); v != vals.cend(); ++v) {
                ++checked;
                const Field f = fields.value(v.key());
                QString want = v.value();
                if (f.type == QLatin1String("Checkbox"))
                    want = v.value() == QLatin1String("Yes") ? QStringLiteral("1") : QStringLiteral("0");
                else if (f.type == QLatin1String("List"))
                    want = f.options.value(v.value());
                else if (f.type == QLatin1String("Date"))
                    want = v.value().left(10);
                if (it->custom.value(v.key()) != want || !it->custom.contains(v.key()))
                    bad << QStringLiteral("%1: %2 = %3 read as %4").arg(it->title, v.key(), v.value(), it->custom.value(v.key(), QStringLiteral("(none)")));
            }
        }
        check(bad.isEmpty(), L + QStringLiteral("every document's keywords and custom metadata values (%1 values), in this program's form").arg(checked),
              bad.join(QLatin1String("\n        ")));
    }
    {
        QStringList bad;
        for (auto f = fields.cbegin(); f != fields.cend(); ++f) {
            const CustomField *cf = p->customFieldById(f.key());
            if (!cf || cf->title != f.value().title || QStringList(f.value().options.values()).size() != cf->options.size())
                bad << f.key();
        }
        check(bad.isEmpty(), L + QStringLiteral("every custom metadata field under Scrivener's ID, with its options"), bad.join(QLatin1String(", ")));
    }
    {
        QStringList bad;
        for (auto c = collections.cbegin(); c != collections.cend(); ++c) {
            const Collection *col = nullptr;
            for (const Collection &x : std::as_const(p->collections))
                if (x.id == c.key())
                    col = &x;
            if (!col || col->items.size() != c.value().size())
                bad << QStringLiteral("%1: %2 of %3 documents").arg(c.key()).arg(col ? col->items.size() : -1).arg(c.value().size());
        }
        check(bad.isEmpty() && !collections.isEmpty(), L + QStringLiteral("every collection with all its documents"), bad.join(QLatin1String(", ")));
    }
    {
        QStringList bad;
        for (auto t = textNotes.cbegin(); t != textNotes.cend(); ++t) {
            QTextDocument *doc = p->docs()->text(t.key());
            const int n = countKind(doc, NoteKind::InlineFootnote) + countKind(doc, NoteKind::InlineAnnotation);
            if (n != t.value() || (doc && doc->toPlainText().contains(QLatin1String("Scrv_"))))
                bad << QStringLiteral("%1: %2 of %3").arg(t.key()).arg(n).arg(t.value());
        }
        check(bad.isEmpty(), L + QStringLiteral("inline notes kept as text become inline notes, no marks left"), bad.join(QLatin1String(", ")));
    }
    p.reset();

    // --- worked on in Scrivener format: opened and saved without a change
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("opened in Scrivener format"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    check(q->save(&err), L + QStringLiteral("saved without a change"), err);
    const QByteArray after = readAll(scrivener::scrivxIn(scriv));
    check(sameFile(original, after), L + QStringLiteral("the project file is as it was"), firstDifference(original, after));
    {
        QStringList changed;
        for (const QFileInfo &fi : QDir(scriv + QLatin1String("/Files/Data")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            for (const QFileInfo &f : QDir(fi.absoluteFilePath()).entryInfoList(QDir::Files))
                if (readAll(f.absoluteFilePath()) != readAll(source + QLatin1String("/Files/Data/") + fi.fileName() + QLatin1Char('/') + f.fileName()))
                    changed << fi.fileName() + QLatin1Char('/') + f.fileName();
        check(changed.isEmpty(), L + QStringLiteral("no document's files changed"), changed.join(QLatin1String(", ")));
    }

    // --- changes here: a keyword, values, a collection
    BinderItem *doc = nullptr;
    for (BinderItem *it : q->allItems())
        if (it->type == ItemType::Text && it->keywords.isEmpty() && it->custom.isEmpty() && !doc)
            doc = it;
    Keyword kw;
    kw.id = newId();
    kw.name = QStringLiteral("Lighthouse");
    kw.color = QColor(10, 120, 200);
    q->keywords << kw;
    doc->keywords << kw.id << q->keywords.first().id;
    for (const CustomField &f : std::as_const(q->customFields)) {
        if (f.type == CustomFieldType::Checkbox)
            doc->custom.insert(f.id, QStringLiteral("1"));
        else if (f.type == CustomFieldType::List && !f.options.isEmpty())
            doc->custom.insert(f.id, f.options.last());
        else if (f.type == CustomFieldType::Date)
            doc->custom.insert(f.id, QStringLiteral("2026-12-24"));
        else
            doc->custom.insert(f.id, QStringLiteral("Typed here"));
    }
    Collection col;
    col.id = newId();
    col.name = QStringLiteral("Made here");
    col.items << doc->id;
    q->collections << col;
    q->notifyItemChanged(doc, false);
    q->markIndexDirty();
    check(q->save(&err), L + QStringLiteral("saved with changes"), err);
    const QByteArray changedFile = readAll(scrivener::scrivxIn(scriv));
    QDomDocument d2;
    d2.setContent(changedFile);
    const QDomElement e = itemEl(d2, doc->id);
    const QStringList ids = itemKeywordIds(e);
    check(ids.size() == 2 && ids.at(0).toInt() > 0 && ids.at(1) == q->keywords.first().id,
          L + QStringLiteral("the document's keywords in Scrivener's form (a new number for the new one)"), ids.join(QLatin1Char(',')));
    const QMap<QString, QString> vals = itemValues(e);
    {
        QStringList bad;
        for (const CustomField &f : std::as_const(q->customFields)) {
            const QString v = vals.value(f.id);
            const bool ok = f.type == CustomFieldType::Checkbox ? v == QLatin1String("Yes")
                            : f.type == CustomFieldType::List   ? fields.value(f.id).options.value(v) == f.options.last()
                            : f.type == CustomFieldType::Date   ? v.startsWith(QLatin1String("2026-12-24 00:00:00 "))
                                                                : v == QLatin1String("Typed here");
            if (!ok)
                bad << f.id + QLatin1Char('=') + v;
        }
        check(bad.isEmpty(), L + QStringLiteral("the values in Scrivener's form"), bad.join(QLatin1String(", ")));
    }
    {
        // nothing else changed: the file without our document's entry, the new keyword and the new collection
        const QByteArray again = readAll(scrivener::scrivxIn(scriv));
        q->save(&err);
        check(sameFile(again, readAll(scrivener::scrivxIn(scriv))), L + QStringLiteral("a second save changes nothing (ids stay)"),
              firstDifference(again, readAll(scrivener::scrivxIn(scriv))));
    }
    const QString docId = doc->id;
    r.project.reset();
    if (!qEnvironmentVariableIsEmpty("QB_V0812_KEEP"))   // to look at the written file by hand
        QFile::copy(scrivener::scrivxIn(scriv), qEnvironmentVariable("QB_V0812_KEEP"));
    // changed elsewhere: read again; what was made here is there
    {
        // another document's title changed in Scrivener
        QDomDocument x;
        x.setContent(readAll(scrivener::scrivxIn(scriv)));
        QDomElement other = x.documentElement().firstChildElement(QStringLiteral("Binder")).firstChildElement(QStringLiteral("BinderItem"));
        QDomElement t = other.firstChildElement(QStringLiteral("Title"));
        t.firstChild().setNodeValue(t.text() + QStringLiteral(" (changed elsewhere)"));
        writeAll(scrivener::scrivxIn(scriv), x.toByteArray(4));
    }
    scrivener::OpenResult r2 = scrivener::open(scriv, &err);
    check(r2.project && r2.reread, L + QStringLiteral("read again after a change elsewhere"), err);
    if (r2.project) {
        Project *n = r2.project.get();
        const BinderItem *it = n->item(docId);
        bool hasLighthouse = false;
        for (const QString &k : it ? it->keywords : QStringList())
            if (const Keyword *x = n->keywordById(k); x && x->name == QLatin1String("Lighthouse"))
                hasLighthouse = true;
        check(it && hasLighthouse && it->keywords.size() == 2, L + QStringLiteral("read again: the keywords made here are there"));
        check(it && it->custom.size() == q->customFields.size() || (it && it->custom.size() == n->customFields.size()),
              L + QStringLiteral("read again: the values set here are there"));
        bool colOk = false;
        for (const Collection &c : std::as_const(n->collections))
            if (c.name == QLatin1String("Made here") && c.items == QStringList{docId})
                colOk = true;
        check(colOk, L + QStringLiteral("read again: the collection made here is there"));
    }
}

// ----------------------------------------------------------------- a project in Scrivener 3 for Windows' form

static const char *kU02 = "A1000000-0000-4000-8000-000000000002";   // Harbor at Dawn
static const char *kU03 = "A1000000-0000-4000-8000-000000000003";   // The Storm
static const char *kU11 = "A1000000-0000-4000-8000-000000000011";   // Gulls (audio)
static const char *kU12 = "A1000000-0000-4000-8000-000000000012";   // Tide tables (a saved web page)
static const char *kCol = "C0000000-0000-4000-8000-000000000001";   // Sea scenes

// A small project as Scrivener 3.1.6.0 for Windows writes one (the form of the elements; the content is made up)
static QString makeWindowsProject(const QString &dir)
{
    const QString scriv = dir + QLatin1String("/Harbor.scriv");
    const QByteArray x = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<ScrivenerProject Template="No" Version="2.0" Identifier="7D2B6F0A-1C3E-4B8A-9F21-3A4C5D6E7F80" Creator="SCRWIN-3.1.6.0" Device="Desk" Author="A. Writer" Modified="2026-11-12 09:00:00 +0100" ModID="11111111-2222-3333-4444-555555555555">
    <Binder>
        <BinderItem UUID="A1000000-0000-4000-8000-000000000001" Type="DraftFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Manuscript</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
            <TextSettings>
                <TextSelection>0,0</TextSelection>
            </TextSettings>
            <Children>
                <BinderItem UUID="A1000000-0000-4000-8000-000000000002" Type="Text" Created="2026-11-01 10:01:00 +0100" Modified="2026-11-02 10:01:00 +0100">
                    <Title>Harbor at Dawn</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                        <CustomMetaData>
                            <MetaDataItem>
                                <FieldID>mood</FieldID>
                                <Value>quiet</Value>
                            </MetaDataItem>
                            <MetaDataItem>
                                <FieldID>revised</FieldID>
                                <Value>Yes</Value>
                            </MetaDataItem>
                            <MetaDataItem>
                                <FieldID>season</FieldID>
                                <Value>3</Value>
                            </MetaDataItem>
                            <MetaDataItem>
                                <FieldID>storyday</FieldID>
                                <Value>2026-03-14 00:00:00 +0100</Value>
                            </MetaDataItem>
                        </CustomMetaData>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                    <Keywords>
                        <KeywordID>5</KeywordID>
                        <KeywordID>2</KeywordID>
                    </Keywords>
                    <Bookmarks>
                        <Bookmark BinderUUID="A1000000-0000-4000-8000-000000000003">The Storm</Bookmark>
                    </Bookmarks>
                </BinderItem>
                <BinderItem UUID="A1000000-0000-4000-8000-000000000003" Type="Text" Created="2026-11-01 10:02:00 +0100" Modified="2026-11-02 10:02:00 +0100">
                    <Title>The Storm</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                        <CustomMetaData>
                            <MetaDataItem>
                                <FieldID>revised</FieldID>
                                <Value>No</Value>
                            </MetaDataItem>
                            <MetaDataItem>
                                <FieldID>season</FieldID>
                                <Value>-1</Value>
                            </MetaDataItem>
                        </CustomMetaData>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                </BinderItem>
            </Children>
        </BinderItem>
        <BinderItem UUID="A1000000-0000-4000-8000-000000000010" Type="ResearchFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Research</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
            <Children>
                <BinderItem UUID="A1000000-0000-4000-8000-000000000011" Type="Media" Created="2026-11-01 10:03:00 +0100" Modified="2026-11-01 10:03:00 +0100">
                    <Title>Gulls</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>wav</FileExtension>
                    </MetaData>
                    <MediaSettings>
                        <PlaybackPosition>0.0</PlaybackPosition>
                    </MediaSettings>
                </BinderItem>
                <BinderItem UUID="A1000000-0000-4000-8000-000000000012" Type="WebArchive" Created="2026-11-01 10:04:00 +0100" Modified="2026-11-01 10:04:00 +0100">
                    <Title>Tide tables</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>mht</FileExtension>
                    </MetaData>
                    <MediaSettings>
                        <OriginalURL>https://example.com/tides</OriginalURL>
                    </MediaSettings>
                </BinderItem>
            </Children>
        </BinderItem>
        <BinderItem UUID="A1000000-0000-4000-8000-000000000020" Type="TrashFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Trash</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
        </BinderItem>
    </Binder>
    <Collections>
        <Collection Type="Binder" ID="B0000000-0000-4000-8000-000000000001" Color="0.941176 0.937255 0.956863">
            <Title>Binder</Title>
        </Collection>
        <Collection Type="RecentSearch" ID="B0000000-0000-4000-8000-000000000002" Color="0.901961 0.901961 0.980392">
            <Title>Search Results</Title>
            <SearchSettings Operator="Any" Type="Text" Scope="All" CompileSetting="All" CaseSensitive="No" IgnoreDiacritics="No">gull</SearchSettings>
        </Collection>
        <Collection Type="Arbitrary" ID="C0000000-0000-4000-8000-000000000001" Color="0.756863 0.850980 0.772549">
            <Title>Sea scenes</Title>
            <BinderUUIDs>A1000000-0000-4000-8000-000000000002,A1000000-0000-4000-8000-000000000003</BinderUUIDs>
        </Collection>
    </Collections>
    <Keywords>
        <Keyword ID="4">
            <Title>Places</Title>
            <Color>0.639216 0.890196 0.949020</Color>
            <Children>
                <Keyword ID="5">
                    <Title>Harbor</Title>
                    <Color>0.639216 0.949020 0.839216</Color>
                </Keyword>
            </Children>
        </Keyword>
        <Keyword ID="2">
            <Title>Weather</Title>
            <Color>1.000000 0.666667 0.000000</Color>
        </Keyword>
    </Keywords>
    <SectionTypes>
        <TypeDefinitions>
            <Type ID="DCA8571B-485A-4963-82E1-104A1428644B">Heading</Type>
            <Type ID="70D25CAF-4DBA-4E6E-98D2-C0482A3E1C77">Text</Type>
        </TypeDefinitions>
        <LevelTypes>
            <Folders>
                <Type>DCA8571B-485A-4963-82E1-104A1428644B</Type>
            </Folders>
            <Containers>
                <Type>70D25CAF-4DBA-4E6E-98D2-C0482A3E1C77</Type>
            </Containers>
            <Files>
                <Type>70D25CAF-4DBA-4E6E-98D2-C0482A3E1C77</Type>
            </Files>
        </LevelTypes>
    </SectionTypes>
    <LabelSettings>
        <Title>Label</Title>
        <DefaultLabelID>-1</DefaultLabelID>
        <Labels>
            <Label ID="-1">No Label</Label>
            <Label ID="0" Color="0.993500 0.701213 0.732586">Red</Label>
        </Labels>
    </LabelSettings>
    <StatusSettings>
        <Title>Status</Title>
        <DefaultStatusID>-1</DefaultStatusID>
        <StatusItems>
            <Status ID="-1">No Status</Status>
            <Status ID="0">To Do</Status>
        </StatusItems>
    </StatusSettings>
    <CustomMetaDataSettings>
        <MetaDataField Type="Text" ID="mood" Wraps="No" Align="Left">
            <Title>Mood</Title>
        </MetaDataField>
        <MetaDataField Type="Checkbox" ID="revised">
            <Title>Revised</Title>
        </MetaDataField>
        <MetaDataField Type="List" ID="season">
            <Title>Season</Title>
            <ListOptions None="None">
                <Option ID="2">Spring</Option>
                <Option ID="3">Summer</Option>
                <Option ID="5">Winter</Option>
            </ListOptions>
        </MetaDataField>
        <MetaDataField Type="Date" ID="storyday" DateType="Short">
            <Title>Story day</Title>
            <DateFormat />
        </MetaDataField>
        <MetaDataField Type="Checkbox" ID="toldbyanna" Default="Yes">
            <Title>Told by Anna</Title>
        </MetaDataField>
    </CustomMetaDataSettings>
    <ProjectBookmarks>
        <Bookmark BinderUUID="A1000000-0000-4000-8000-000000000002">Harbor at Dawn</Bookmark>
    </ProjectBookmarks>
    <ProjectTargets Notify="No">
        <DraftTarget Type="Words" CountIncludedOnly="Yes" CurrentCompileGroupOnly="No">0</DraftTarget>
        <SessionTarget Type="Words" CountDraftOnly="No" AllowNegatives="No" ResetType="Time" ResetTime="00:00" DeterminedFromDeadline="No" CanWriteOnDeadlineDate="Yes" WritingDays="">0</SessionTarget>
    </ProjectTargets>
    <PrintSettings PaperSize="595.0,842.0" LeftMargin="72.0" RightMargin="72.0" TopMargin="72.0" BottomMargin="72.0" PaperType="iso-a4" Orientation="Portrait" ScaleFactor="1.0" HorizontallyCentered="Yes" VerticallyCentered="Yes" Collates="No" PagesAcross="1" PagesDown="1" />
</ScrivenerProject>
)XML";
    writeAll(scriv + QLatin1String("/Harbor.scrivx"), x);
    const QString d = scriv + QLatin1String("/Files/Data/");
    // inline notes kept as text (the braces and backslashes are escaped RTF characters), and a linked comment
    writeAll(d + QLatin1String(kU02) + QLatin1String("/content.rtf"), R"RTF({\rtf1\ansi\ansicpg1252\uc1\deff0
{\fonttbl{\f0\fnil\fcharset0\fprq2 Cambria;}}
{\colortbl;\red0\green0\blue0;\red255\green255\blue255;\red128\green128\blue128;}
\paperw12240\paperh15840\margl1800\margr1800\margt1440\margb1440\f0\fs24\cf0
\pard\plain \ltrch\loch {\f0\fs24\b0\i0 The boats came in before the light.\{\\Scrv_fn=}{\f0\fs24\b0\i0 Fishing boats, not ferries.\\end_Scrv_fn\}}
\par\plain {\f0\fs24\b0\i0 \{\\Scrv_annot \\color=\{\\R=0.682353\\G=0.117647\\B=0.062745\} \\text=Check the tide times.\\end_Scrv_annot\}}
\par\plain {\f0\fs24\b0\i0 Anna counted }{\field{\*\fldinst HYPERLINK "scrivcmt://D1000000-0000-4000-8000-000000000001"}{\fldrslt\f0\fs24\b0\i0 the gulls}}{\f0\fs24\b0\i0 .}
\par\plain \f0\fs24\b0\i0}
)RTF");
    writeAll(d + QLatin1String(kU02) + QLatin1String("/content.comments"), R"XML(<?xml version="1.0" encoding="UTF-8"?>
<Comments Version="1.0">
    <Comment ID="D1000000-0000-4000-8000-000000000001" Collapsed="No" Color="0.996078 0.964706 0.737255"><![CDATA[{\rtf1\ansi\ansicpg1252\uc1\deff0 {\fonttbl{\f0\fnil\fcharset0\fprq2 Cambria;}} \pard\plain \ltrch\loch {\f0\fs20\b0\i0 Were there twelve?}}]]></Comment>
</Comments>
)XML");
    writeAll(d + QLatin1String(kU03) + QLatin1String("/content.rtf"), R"RTF({\rtf1\ansi\ansicpg1252\uc1\deff0
{\fonttbl{\f0\fnil\fcharset0\fprq2 Cambria;}}
\pard\plain \ltrch\loch {\f0\fs24\b0\i0 The storm came at noon.}
\par\plain \f0\fs24\b0\i0}
)RTF");
    writeAll(d + QLatin1String(kU11) + QLatin1String("/content.wav"), QByteArray("RIFF\x24\x00\x00\x00WAVEfmt ", 16) + QByteArray(28, '\0'));
    writeAll(d + QLatin1String(kU12) + QLatin1String("/content.mht"),
             "Content-Type: multipart/related; boundary=\"b1\"\r\nMIME-Version: 1.0\r\n\r\n--b1\r\nContent-Type: text/html; charset=utf-8\r\n"
             "Content-Location: https://example.com/tides\r\n\r\n<html><body><p>High water 06:12</p></body></html>\r\n--b1--\r\n");
    writeAll(scriv + QLatin1String("/Files/version.txt"), "23");
    return scriv;
}

static QWidget *fieldWidget(QWidget *inspector, const QString &title)
{
    for (QLabel *l : inspector->findChildren<QLabel *>())
        if (l->text() == title + QLatin1Char(':') && l->buddy())
            return l->buddy();
    return nullptr;
}

static void windowsProject(const QString &W)
{
    const QString L = QStringLiteral("Windows form: ");
    const QString scriv = makeWindowsProject(W + QLatin1String("/win"));
    const QByteArray original = readAll(scrivener::scrivxIn(scriv));
    QString err;

    // --- read (File > Import)
    {
        ScrivenerImportReport rep;
        auto p = ScrivenerImporter::importProject(scriv, W + QLatin1String("/win-import"), QStringLiteral("Harbor"), StorageFormat::Rtf, &rep, &err);
        check(bool(p), L + QStringLiteral("imported"), err);
        if (!p)
            return;
        const Keyword *places = p->keywordById(QStringLiteral("4")), *harbor = p->keywordById(QStringLiteral("5")),
                      *weather = p->keywordById(QStringLiteral("2"));
        check(places && harbor && weather && p->keywords.size() == 3 && places->name == QLatin1String("Places")
                  && harbor->parentId == QLatin1String("4") && weather->name == QLatin1String("Weather")
                  && harbor->color.toRgb() == QColor::fromRgbF(0.639216f, 0.949020f, 0.839216f).toRgb(),
              L + QStringLiteral("keywords: Scrivener's ids, titles, colors and groups"));
        const BinderItem *h = p->item(QLatin1String(kU02)), *st = p->item(QLatin1String(kU03));
        check(h && h->keywords == QStringList({QStringLiteral("5"), QStringLiteral("2")}), L + QStringLiteral("a document's keywords in its order"),
              h ? h->keywords.join(QLatin1Char(',')) : QString());
        const CustomField *mood = p->customFieldById(QStringLiteral("mood")), *rev = p->customFieldById(QStringLiteral("revised")),
                          *season = p->customFieldById(QStringLiteral("season")), *day = p->customFieldById(QStringLiteral("storyday")),
                          *anna = p->customFieldById(QStringLiteral("toldbyanna"));
        check(mood && rev && season && day && anna && mood->type == CustomFieldType::Text && !mood->wrap && rev->type == CustomFieldType::Checkbox
                  && season->type == CustomFieldType::List && day->type == CustomFieldType::Date && anna->defaultValue == QLatin1String("1")
                  && season->options == QStringList({QStringLiteral("Spring"), QStringLiteral("Summer"), QStringLiteral("Winter")}),
              L + QStringLiteral("custom metadata fields: Scrivener's ids, types, titles, options, a checkbox ticked by default, no wrapping"));
        check(h && h->custom.value(QStringLiteral("mood")) == QLatin1String("quiet") && h->custom.value(QStringLiteral("revised")) == QLatin1String("1")
                  && h->custom.value(QStringLiteral("season")) == QLatin1String("Summer")
                  && h->custom.value(QStringLiteral("storyday")) == QLatin1String("2026-03-14"),
              L + QStringLiteral("values in this program's form: \"Yes\" ticked, option 3 = Summer, the date's day"),
              h ? QStringList(h->custom.values()).join(QLatin1String(" | ")) : QString());
        check(st && st->custom.value(QStringLiteral("revised")) == QLatin1String("0") && st->custom.contains(QStringLiteral("season"))
                  && st->custom.value(QStringLiteral("season")).isEmpty(),
              L + QStringLiteral("\"No\" is not ticked, option -1 is none"));
        const Collection *col = nullptr;
        for (const Collection &c : std::as_const(p->collections))
            if (c.id == QLatin1String(kCol))
                col = &c;
        check(col && col->name == QLatin1String("Sea scenes") && col->items == QStringList({QLatin1String(kU02), QLatin1String(kU03)})
                  && p->collections.size() == 1,
              L + QStringLiteral("the collection with its documents (one comma-separated list), under Scrivener's id"));
        QTextDocument *doc = p->docs()->text(QLatin1String(kU02));
        check(countKind(doc, NoteKind::InlineFootnote) == 1 && countKind(doc, NoteKind::InlineAnnotation) == 1
                  && countKind(doc, NoteKind::Comment) == 1 && !doc->toPlainText().contains(QLatin1String("Scrv_"))
                  && !doc->toPlainText().contains(QLatin1Char('{')),
              L + QStringLiteral("inline notes kept as text: one footnote, one annotation, no marks left; the linked comment as before"),
              doc ? doc->toPlainText() : QString());
        bool colorOk = false, textsOk = false;
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat cf = it.fragment().charFormat();
                if (NoteKind(cf.intProperty(props::NoteKind)) == NoteKind::InlineAnnotation) {
                    colorOk = cf.property(props::NoteColor).value<QColor>().toRgb() == QColor::fromRgbF(0.682353f, 0.117647f, 0.062745f).toRgb();
                    textsOk = it.fragment().text() == QLatin1String("Check the tide times.");
                }
            }
        check(colorOk && textsOk, L + QStringLiteral("the annotation keeps its color and text"));
        check(doc->toPlainText().startsWith(QLatin1String("The boats came in before the light.Fishing boats, not ferries.\n")),
              L + QStringLiteral("the footnote stays where it was"), doc->toPlainText());
        check(p->item(QLatin1String(kU11)) && p->item(QLatin1String(kU11))->type == ItemType::Media && p->item(QLatin1String(kU12))
                  && p->item(QLatin1String(kU12))->type == ItemType::WebPage && p->item(QLatin1String(kU12))->fileExt == QLatin1String("mht"),
              L + QStringLiteral("the audio and the saved web page"));
    }

    // --- in Scrivener format: opened and saved without a change
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("opened in Scrivener format"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    check(q->save(&err), L + QStringLiteral("saved without a change"), err);
    check(sameFile(original, readAll(scrivener::scrivxIn(scriv))), L + QStringLiteral("the project file is as Scrivener wrote it"),
          firstDifference(original, readAll(scrivener::scrivxIn(scriv))));
    {
        QJsonObject st = QJsonDocument::fromJson(readAll(scrivener::stateFile(q))).object();
        // 0.90.0: the level is 2 since bookmarks, targets and the corkboard are in the .scriv too
        check(st.value(QStringLiteral("meta")).toInt() >= 1, L + QStringLiteral("the state says the .scriv holds keywords, metadata and collections"));
    }

    // --- the inspector shows the values
    {
        Inspector insp;
        insp.resize(380, 760);
        insp.setProject(q);
        insp.setItem(q->item(QLatin1String(kU02)));
        insp.showTab(Inspector::Metadata);
        insp.show();
        QTest::qWait(150);
        auto *rev = qobject_cast<QCheckBox *>(fieldWidget(&insp, QStringLiteral("Revised")));
        auto *season = qobject_cast<QComboBox *>(fieldWidget(&insp, QStringLiteral("Season")));
        auto *day = qobject_cast<QDateEdit *>(fieldWidget(&insp, QStringLiteral("Story day")));
        auto *anna = qobject_cast<QCheckBox *>(fieldWidget(&insp, QStringLiteral("Told by Anna")));
        auto *mood = qobject_cast<QLineEdit *>(fieldWidget(&insp, QStringLiteral("Mood")));
        check(rev && rev->isChecked() && season && season->currentText() == QLatin1String("Summer") && day
                  && day->date() == QDate(2026, 3, 14) && anna && anna->isChecked() && mood && mood->text() == QLatin1String("quiet"),
              L + QStringLiteral("the inspector: Revised ticked, Season Summer, Story day 2026-03-14, Told by Anna ticked (its default), Mood"),
              QStringLiteral("%1 %2 %3 %4 %5").arg(rev && rev->isChecked()).arg(season ? season->currentText() : QString())
                  .arg(day ? day->date().toString(Qt::ISODate) : QString()).arg(anna && anna->isChecked()).arg(mood ? mood->text() : QString()));
        insp.grab().save(outDir + QLatin1String("/v0812-inspector-metadata.png"));
        insp.setItem(q->item(QLatin1String(kU03)));
        QTest::qWait(50);
        rev = qobject_cast<QCheckBox *>(fieldWidget(&insp, QStringLiteral("Revised")));
        season = qobject_cast<QComboBox *>(fieldWidget(&insp, QStringLiteral("Season")));
        check(rev && !rev->isChecked() && season && season->currentText().isEmpty(), L + QStringLiteral("the inspector: \"No\" and none"));
        insp.setItem(nullptr);
    }

    // --- changes here
    BinderItem *h = q->item(QLatin1String(kU02)), *st = q->item(QLatin1String(kU03));
    Keyword light;
    light.id = newId();
    light.name = QStringLiteral("Lighthouse");
    light.color = QColor(200, 40, 40);
    light.parentId = QStringLiteral("4");
    q->keywords << light;
    for (Keyword &k : q->keywords)
        if (k.id == QLatin1String("2"))
            k.name = QStringLiteral("Storms");
    st->keywords << light.id;
    h->keywords.removeAll(QStringLiteral("2"));
    st->custom.insert(QStringLiteral("revised"), QStringLiteral("1"));
    st->custom.insert(QStringLiteral("season"), QStringLiteral("Winter"));
    st->custom.insert(QStringLiteral("storyday"), QStringLiteral("2026-07-01"));
    st->custom.insert(QStringLiteral("mood"), QStringLiteral("restless"));
    h->custom.insert(QStringLiteral("season"), QString());
    for (CustomField &f : q->customFields)
        if (f.id == QLatin1String("season"))
            f.options << QStringLiteral("Autumn");
    CustomField loc;
    loc.id = newId();
    loc.title = QStringLiteral("Location");
    q->customFields << loc;
    CustomField act;
    act.id = newId();
    act.title = QStringLiteral("Act");
    act.type = CustomFieldType::List;
    act.options = {QStringLiteral("One"), QStringLiteral("Two")};
    q->customFields << act;
    h->custom.insert(act.id, QStringLiteral("Two"));
    h->custom.insert(loc.id, QStringLiteral("Pier"));
    for (Collection &c : q->collections)
        if (c.id == QLatin1String(kCol)) {
            c.name = QStringLiteral("At sea");
            c.items.removeAll(QLatin1String(kU03));
        }
    Collection nights;
    nights.id = newId();
    nights.name = QStringLiteral("Nights");
    nights.items << QLatin1String(kU03);
    q->collections << nights;
    Collection search;
    search.id = newId();
    search.name = QStringLiteral("Gull search");
    search.isSearch = true;
    search.query.text = QStringLiteral("gull");
    q->collections << search;
    // an audio document made here
    BinderItem *song = q->model()->createItem(q->research(), -1, ItemType::Media, QStringLiteral("Foghorn"));
    song->fileExt = QStringLiteral("mp3");
    writeAll(q->mediaFilePath(song), QByteArray("ID3\x04\x00\x00\x00\x00\x00\x00", 10) + QByteArray(64, '\x11'));
    q->notifyItemChanged(h, false);
    q->notifyItemChanged(st, false);
    q->markIndexDirty();
    check(q->save(&err), L + QStringLiteral("saved with changes"), err);
    const QByteArray changed = readAll(scrivener::scrivxIn(scriv));
    QDomDocument d;
    d.setContent(changed);
    const QDomElement root = d.documentElement();
    {
        const QDomElement kws = root.firstChildElement(QStringLiteral("Keywords"));
        QStringList tree;
        std::function<void(const QDomElement &, const QString &)> walk = [&](const QDomElement &e, const QString &pre) {
            for (QDomElement k = e.firstChildElement(QStringLiteral("Keyword")); !k.isNull(); k = k.nextSiblingElement(QStringLiteral("Keyword"))) {
                tree << pre + k.attribute(QStringLiteral("ID")) + QLatin1Char('=') + k.firstChildElement(QStringLiteral("Title")).text();
                walk(k.firstChildElement(QStringLiteral("Children")), pre + QLatin1Char('>'));
            }
        };
        walk(kws, QString());
        check(tree == QStringList({QStringLiteral("4=Places"), QStringLiteral(">5=Harbor"), QStringLiteral(">6=Lighthouse"), QStringLiteral("2=Storms")}),
              L + QStringLiteral("written: the keywords with their ids, a new one numbered after the others, in its group; a renamed one keeps its id"),
              tree.join(QLatin1String(", ")));
        check(itemKeywordIds(itemEl(d, QLatin1String(kU03))) == QStringList{QStringLiteral("6")}
                  && itemKeywordIds(itemEl(d, QLatin1String(kU02))) == QStringList{QStringLiteral("5")},
              L + QStringLiteral("written: the documents' keywords"));
        check(childTags(itemEl(d, QLatin1String(kU02))) == QStringList({QStringLiteral("Title"), QStringLiteral("MetaData"), QStringLiteral("TextSettings"),
                                                                       QStringLiteral("Keywords"), QStringLiteral("Bookmarks")}),
              L + QStringLiteral("written: a document's keywords before its bookmarks, as Scrivener has them"),
              childTags(itemEl(d, QLatin1String(kU02))).join(QLatin1Char(',')));
    }
    {
        QStringList fieldsLine;
        for (QDomElement f = root.firstChildElement(QStringLiteral("CustomMetaDataSettings")).firstChildElement(); !f.isNull(); f = f.nextSiblingElement()) {
            QString line = f.attribute(QStringLiteral("ID")) + QLatin1Char('/') + f.attribute(QStringLiteral("Type"));
            for (QDomElement o = f.firstChildElement(QStringLiteral("ListOptions")).firstChildElement(); !o.isNull(); o = o.nextSiblingElement())
                line += QLatin1Char(' ') + o.attribute(QStringLiteral("ID")) + QLatin1Char('=') + o.text();
            fieldsLine << line;
        }
        check(fieldsLine == QStringList({QStringLiteral("mood/Text"), QStringLiteral("revised/Checkbox"),
                                         QStringLiteral("season/List 2=Spring 3=Summer 5=Winter 6=Autumn"), QStringLiteral("storyday/Date"),
                                         QStringLiteral("toldbyanna/Checkbox"), QStringLiteral("location/Text"), QStringLiteral("act/List 1=One 2=Two")}),
              L + QStringLiteral("written: the fields under their ids, new ones named after their titles, options keep their ids"),
              fieldsLine.join(QLatin1String(" | ")));
        const QMap<QString, QString> sv = itemValues(itemEl(d, QLatin1String(kU03))), hv = itemValues(itemEl(d, QLatin1String(kU02)));
        check(sv.value(QStringLiteral("revised")) == QLatin1String("Yes") && sv.value(QStringLiteral("season")) == QLatin1String("5")
                  && sv.value(QStringLiteral("storyday")).startsWith(QLatin1String("2026-07-01 00:00:00 "))
                  && sv.value(QStringLiteral("mood")) == QLatin1String("restless"),
              L + QStringLiteral("written: ticked = Yes, a list's option ID, a date with time and zone, text"),
              QStringList(sv.values()).join(QLatin1String(" | ")));
        check(hv.value(QStringLiteral("season")) == QLatin1String("-1") && hv.value(QStringLiteral("act")) == QLatin1String("2")
                  && hv.value(QStringLiteral("location")) == QLatin1String("Pier") && hv.value(QStringLiteral("storyday")) == QLatin1String("2026-03-14 00:00:00 +0100")
                  && hv.value(QStringLiteral("revised")) == QLatin1String("Yes"),
              L + QStringLiteral("written: none = -1; unchanged values keep the file's text (the date's zone)"),
              QStringList(hv.values()).join(QLatin1String(" | ")));
        QStringList order;
        const QDomElement cm = itemEl(d, QLatin1String(kU02)).firstChildElement(QStringLiteral("MetaData")).firstChildElement(QStringLiteral("CustomMetaData"));
        for (QDomElement mi = cm.firstChildElement(); !mi.isNull(); mi = mi.nextSiblingElement())
            order << mi.firstChildElement(QStringLiteral("FieldID")).text();
        QStringList sorted = order;
        sorted.sort();
        check(order == sorted && order.size() == 6, L + QStringLiteral("written: values sorted by their field, as Scrivener has them"), order.join(QLatin1Char(',')));
    }
    {
        QStringList cols;
        for (QDomElement c = root.firstChildElement(QStringLiteral("Collections")).firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            cols << c.attribute(QStringLiteral("Type")) + QLatin1Char('/') + c.firstChildElement(QStringLiteral("Title")).text() + QLatin1Char('/')
                        + c.firstChildElement(QStringLiteral("BinderUUIDs")).text();
        check(cols == QStringList({QStringLiteral("Binder/Binder/"), QStringLiteral("RecentSearch/Search Results/"),
                                   QStringLiteral("Arbitrary/At sea/") + QLatin1String(kU02), QStringLiteral("Arbitrary/Nights/") + QLatin1String(kU03),
                                   QStringLiteral("SavedSearch/Gull search/")}),   // 0.90.0: a search collection is a saved search there
              L + QStringLiteral("written: collections (renamed, a document taken out, a new one); this program's search collection as a saved search (0.90.0)"),
              cols.join(QLatin1String(" | ")));
    }
    {
        QStringList top;
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
            top << e.tagName();
        check(top.indexOf(QStringLiteral("Keywords")) == top.indexOf(QStringLiteral("Collections")) + 1
                  && top.indexOf(QStringLiteral("CustomMetaDataSettings")) == top.indexOf(QStringLiteral("StatusSettings")) + 1,
              L + QStringLiteral("written: Scrivener's order of the parts"), top.join(QLatin1Char(',')));
        const QDomElement gulls = itemEl(d, QLatin1String(kU11)), tides = itemEl(d, QLatin1String(kU12)), fog = itemEl(d, song->id);
        check(gulls.attribute(QStringLiteral("Type")) == QLatin1String("Media") && tides.attribute(QStringLiteral("Type")) == QLatin1String("WebArchive")
                  && tides.firstChildElement(QStringLiteral("MediaSettings")).firstChildElement(QStringLiteral("OriginalURL")).text()
                         == QLatin1String("https://example.com/tides"),
              L + QStringLiteral("written: Scrivener's audio and web page keep their types (they became text documents before)"));
        check(fog.attribute(QStringLiteral("Type")) == QLatin1String("Media")
                  && fog.firstChildElement(QStringLiteral("MetaData")).firstChildElement(QStringLiteral("FileExtension")).text() == QLatin1String("mp3")
                  && !fog.firstChildElement(QStringLiteral("MediaSettings")).firstChildElement(QStringLiteral("PlaybackPosition")).isNull()
                  && QFile::exists(scriv + QLatin1String("/Files/Data/") + song->id.toUpper() + QLatin1String("/content.mp3"))
                  && !QFile::exists(scriv + QLatin1String("/Files/Data/") + song->id.toUpper() + QLatin1String("/content.rtf")),
              L + QStringLiteral("written: an audio document made here as Scrivener has one (type, file, no stand-in text)"),
              QStringLiteral("%1 %2").arg(fog.attribute(QStringLiteral("Type")), song->id));
    }
    {
        const QByteArray before = readAll(scrivener::scrivxIn(scriv));
        check(q->save(&err) && sameFile(before, readAll(scrivener::scrivxIn(scriv))), L + QStringLiteral("a second save changes nothing (ids stay)"),
              firstDifference(before, readAll(scrivener::scrivxIn(scriv))));
    }
    const QString lightId = QStringLiteral("6");
    r.project.reset();

    // --- changed in Scrivener meanwhile: a title; Harbor deleted (keyword and its use)
    {
        QDomDocument x;
        x.setContent(readAll(scrivener::scrivxIn(scriv)));
        QDomElement t = itemEl(x, QLatin1String(kU03)).firstChildElement(QStringLiteral("Title"));
        t.firstChild().setNodeValue(QStringLiteral("The Great Storm"));
        QDomElement h5 = itemEl(x, QLatin1String(kU02)).firstChildElement(QStringLiteral("Keywords"));
        itemEl(x, QLatin1String(kU02)).removeChild(h5);
        const QDomNodeList ks = x.elementsByTagName(QStringLiteral("Keyword"));
        for (int i = 0; i < ks.size(); ++i)
            if (ks.at(i).toElement().attribute(QStringLiteral("ID")) == QLatin1String("5")) {
                ks.at(i).parentNode().removeChild(ks.at(i));
                break;
            }
        writeAll(scrivener::scrivxIn(scriv), x.toByteArray(4));
    }
    {
        scrivener::OpenResult r2 = scrivener::open(scriv, &err);
        check(r2.project && r2.reread, L + QStringLiteral("changed elsewhere: read again"), err);
        if (r2.project) {
            Project *n = r2.project.get();
            const BinderItem *nh = n->item(QLatin1String(kU02)), *ns = n->item(QLatin1String(kU03));
            check(ns && ns->title == QLatin1String("The Great Storm"), L + QStringLiteral("changed elsewhere: the new title"));
            check(!n->keywordById(QStringLiteral("5")) && nh && nh->keywords.isEmpty(),
                  L + QStringLiteral("changed elsewhere: a keyword deleted in Scrivener stays deleted (not brought back from the working copy)"));
            check(n->keywordById(lightId) && n->keywordById(lightId)->name == QLatin1String("Lighthouse") && ns && ns->keywords == QStringList{lightId},
                  L + QStringLiteral("changed elsewhere: the keyword made here is there (it is in the .scriv)"));
            check(ns && ns->custom.value(QStringLiteral("season")) == QLatin1String("Winter") && ns->custom.value(QStringLiteral("revised")) == QLatin1String("1")
                      && nh && nh->custom.value(QStringLiteral("act")) == QLatin1String("Two") && nh->custom.value(QStringLiteral("location")) == QLatin1String("Pier"),
                  L + QStringLiteral("changed elsewhere: values and fields made here are there"));
            QStringList names;
            for (const Collection &c : std::as_const(n->collections))
                names << c.name + (c.isSearch ? QStringLiteral(" (search)") : QString());
            check(names == QStringList({QStringLiteral("At sea"), QStringLiteral("Nights"), QStringLiteral("Gull search (search)")}),
                  L + QStringLiteral("changed elsewhere: the collections, and this program's search collection kept"), names.join(QLatin1String(", ")));
        }
    }

    // --- a working copy of an older version: things only it has, the state without "meta"
    {
        scrivener::OpenResult r3 = scrivener::open(scriv, &err);
        Project *o = r3.project.get();
        check(o && !r3.reread, L + QStringLiteral("older working copy: set up"), err);
        if (!o)
            return;
        Keyword fog;
        fog.id = newId();
        fog.name = QStringLiteral("Fog");
        o->keywords << fog;
        o->item(QLatin1String(kU02))->keywords << fog.id;
        o->item(QLatin1String(kU02))->custom.insert(QStringLiteral("mood"), QStringLiteral("grey"));   // the .scriv has "quiet"
        o->item(QLatin1String(kU03))->custom.remove(QStringLiteral("mood"));
        CustomField oldField;
        oldField.id = newId();
        oldField.title = QStringLiteral("Old field");
        oldField.type = CustomFieldType::Checkbox;
        o->customFields << oldField;
        o->item(QLatin1String(kU03))->custom.insert(oldField.id, QStringLiteral("Yes"));   // as an older version kept it
        // a text read by an older version: the inline note as text
        QTextDocument raw;
        raw.setPlainText(QStringLiteral("The storm came at noon.{\\Scrv_fn=Noon by the church clock.\\end_Scrv_fn}"));
        o->docs()->setContent(QLatin1String(kU03), &raw);
        o->markIndexDirty();
        const QString path = o->scrivenerPath;
        o->scrivenerPath.clear();   // saved like an older version would have: nothing of this reaches the .scriv
        o->save(&err);
        o->scrivenerPath = path;
        QJsonObject stObj = QJsonDocument::fromJson(readAll(scrivener::stateFile(o))).object();
        stObj.remove(QStringLiteral("meta"));
        writeAll(scrivener::stateFile(o), QJsonDocument(stObj).toJson());
    }
    {
        scrivener::OpenResult r4 = scrivener::open(scriv, &err);
        check(r4.project && r4.reread, L + QStringLiteral("older working copy: read again once, though the .scriv did not change"), err);
        if (r4.project) {
            Project *n = r4.project.get();
            const BinderItem *nh = n->item(QLatin1String(kU02)), *ns = n->item(QLatin1String(kU03));
            QString fogId;
            for (const Keyword &k : std::as_const(n->keywords))
                if (k.name == QLatin1String("Fog"))
                    fogId = k.id;
            check(!fogId.isEmpty() && nh && nh->keywords.contains(fogId), L + QStringLiteral("older working copy: its own keyword and its use are kept"));
            check(nh && nh->custom.value(QStringLiteral("mood")) == QLatin1String("quiet"),
                  L + QStringLiteral("older working copy: a value the .scriv has stays the .scriv's"));
            const CustomField *of = nullptr;
            for (const CustomField &f : std::as_const(n->customFields))
                if (f.title == QLatin1String("Old field"))
                    of = &f;
            check(of && ns && ns->custom.value(of->id) == QLatin1String("1"),
                  L + QStringLiteral("older working copy: its own field is kept, its value in this version's form"));
            QTextDocument *t = n->docs()->text(QLatin1String(kU03));
            check(countKind(t, NoteKind::InlineFootnote) == 1 && !t->toPlainText().contains(QLatin1String("Scrv_")),
                  L + QStringLiteral("older working copy: an inline note it had as text becomes a footnote"), t ? t->toPlainText() : QString());
            check(QJsonDocument::fromJson(readAll(scrivener::stateFile(n))).object().value(QStringLiteral("meta")).toInt() == 0,
                  L + QStringLiteral("older working copy: joined, not yet written: the next opening joins again"));
            check(n->save(&err), L + QStringLiteral("older working copy: saved"), err);
            QDomDocument x;
            x.setContent(readAll(scrivener::scrivxIn(scriv)));
            bool fogWritten = false;
            const QDomNodeList ks = x.documentElement().firstChildElement(QStringLiteral("Keywords")).elementsByTagName(QStringLiteral("Keyword"));
            for (int i = 0; i < ks.size(); ++i)
                fogWritten = fogWritten || ks.at(i).toElement().firstChildElement(QStringLiteral("Title")).text() == QLatin1String("Fog");
            check(fogWritten && QJsonDocument::fromJson(readAll(scrivener::stateFile(n))).object().value(QStringLiteral("meta")).toInt() >= 1,
                  L + QStringLiteral("older working copy: after the save, its keyword is in the .scriv and the state says so"));
        }
    }
    {
        scrivener::OpenResult r5 = scrivener::open(scriv, &err);
        check(r5.project && !r5.reread, L + QStringLiteral("after that: opened as it is again"), err);
    }
}

// ----------------------------------------------------------------- this program's projects

static void ownProjects(const QString &W)
{
    const QString L = QStringLiteral("own project: ");
    QString err;
    // imported before 0.81.2 (inline notes as text): put right when it opens
    {
        auto p = Project::create(W + QLatin1String("/own"), QStringLiteral("Imported Earlier"), &err);
        check(bool(p), L + QStringLiteral("made"), err);
        if (!p)
            return;
        BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Chapter"));
        QTextDocument raw;
        raw.setPlainText(QStringLiteral("Rain.{\\Scrv_annot \\color={\\R=0.1\\G=0.2\\B=0.9} \\text=Too much rain?\\end_Scrv_annot} More rain."));
        p->docs()->setContent(it->id, &raw);
        p->markIndexDirty();
        p->save(&err);
        const QString id = it->id;
        const int changed = ScrivenerImporter::removeLeftoverMarkers(p.get(), &err);
        QTextDocument *t = p->docs()->text(id);
        check(changed == 1 && countKind(t, NoteKind::InlineAnnotation) == 1 && t->toPlainText() == QLatin1String("Rain.Too much rain? More rain."),
              L + QStringLiteral("inline notes an earlier import kept as text become notes when the project opens"), t->toPlainText());

        // custom metadata values an earlier import kept in Scrivener's form
        {
            CustomField box, day;
            box.id = newId();
            box.title = QStringLiteral("Checked");
            box.type = CustomFieldType::Checkbox;
            day.id = newId();
            day.title = QStringLiteral("Day");
            day.type = CustomFieldType::Date;
            p->customFields << box << day;
            it->custom.insert(box.id, QStringLiteral("Yes"));
            it->custom.insert(day.id, QStringLiteral("2026-03-14 00:00:00 +0100"));
            const int n = ScrivenerImporter::fixOldCustomValues(p.get());
            check(n == 1 && it->custom.value(box.id) == QLatin1String("1") && it->custom.value(day.id) == QLatin1String("2026-03-14"),
                  L + QStringLiteral("custom metadata values an earlier import kept in Scrivener's form are put right when the project opens"));
            p->customFields.removeLast();
            p->customFields.removeLast();
            it->custom.remove(box.id);
            it->custom.remove(day.id);
        }

        // --- saved as a Scrivener project: its keywords, fields and collections get Scrivener's form
        Keyword a, b;
        a.id = newId();
        a.name = QStringLiteral("Anna");
        b.id = newId();
        b.name = QStringLiteral("Ben");
        b.parentId = a.id;   // a group
        p->keywords << a << b;
        it->keywords << b.id;
        CustomField pov;
        pov.id = newId();
        pov.title = QStringLiteral("Point of view");
        pov.type = CustomFieldType::List;
        pov.options = {QStringLiteral("Anna"), QStringLiteral("Ben")};
        CustomField done;
        done.id = newId();
        done.title = QStringLiteral("Done?");
        done.type = CustomFieldType::Checkbox;
        p->customFields << pov << done;
        it->custom.insert(pov.id, QStringLiteral("Ben"));
        it->custom.insert(done.id, QStringLiteral("0"));
        Collection c;
        c.id = newId();
        c.name = QStringLiteral("Rainy");
        c.items << id;
        p->collections << c;
        p->markIndexDirty();
        p->save(&err);
        const QString scriv = W + QLatin1String("/own/Converted.scriv");
        auto wc = scrivener::convertToScrivener(p.get(), scriv, &err);
        check(bool(wc), L + QStringLiteral("switched to Scrivener format"), err);
        if (!wc)
            return;
        QDomDocument d;
        d.setContent(readAll(scrivener::scrivxIn(scriv)));
        const QDomElement root = d.documentElement();
        const QDomElement k1 = root.firstChildElement(QStringLiteral("Keywords")).firstChildElement(QStringLiteral("Keyword"));
        const QDomElement k2 = k1.firstChildElement(QStringLiteral("Children")).firstChildElement(QStringLiteral("Keyword"));
        check(k1.attribute(QStringLiteral("ID")) == QLatin1String("1") && k1.firstChildElement(QStringLiteral("Title")).text() == QLatin1String("Anna")
                  && k2.attribute(QStringLiteral("ID")) == QLatin1String("2") && k2.firstChildElement(QStringLiteral("Title")).text() == QLatin1String("Ben")
                  && itemKeywordIds(itemEl(d, id)) == QStringList{QStringLiteral("2")},
              L + QStringLiteral("keywords numbered as Scrivener numbers them, with their group"));
        QStringList ids;
        for (QDomElement f = root.firstChildElement(QStringLiteral("CustomMetaDataSettings")).firstChildElement(); !f.isNull(); f = f.nextSiblingElement())
            ids << f.attribute(QStringLiteral("ID"));
        const QMap<QString, QString> v = itemValues(itemEl(d, id));
        check(ids == QStringList({QStringLiteral("pointofview"), QStringLiteral("done")}) && v.value(QStringLiteral("pointofview")) == QLatin1String("2")
                  && v.value(QStringLiteral("done")) == QLatin1String("No"),
              L + QStringLiteral("fields named after their titles, values in Scrivener's form"), ids.join(QLatin1Char(',')));
        bool colOk = false;
        for (QDomElement e = root.firstChildElement(QStringLiteral("Collections")).firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
            if (e.attribute(QStringLiteral("Type")) == QLatin1String("Arbitrary") && e.firstChildElement(QStringLiteral("Title")).text() == QLatin1String("Rainy")
                && e.firstChildElement(QStringLiteral("BinderUUIDs")).text() == id.toUpper())
                colOk = true;
        check(colOk, L + QStringLiteral("the collection"));
        const QByteArray once = readAll(scrivener::scrivxIn(scriv));
        check(wc->save(&err) && sameFile(once, readAll(scrivener::scrivxIn(scriv))), L + QStringLiteral("saved again: nothing changes"),
              firstDifference(once, readAll(scrivener::scrivxIn(scriv))));
        // read by an import, as Scrivener would read it: the same keywords, values and collection
        ScrivenerImportReport rep;
        auto back = ScrivenerImporter::importProject(scriv, W + QLatin1String("/own-back"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
        const BinderItem *bi = back ? back->item(id) : nullptr;
        const Keyword *bk = back && bi && bi->keywords.size() == 1 ? back->keywordById(bi->keywords.first()) : nullptr;
        check(bk && bk->name == QLatin1String("Ben") && back->keywordById(bk->parentId) && bi->custom.value(QStringLiteral("pointofview")) == QLatin1String("Ben")
                  && bi->custom.value(QStringLiteral("done")) == QLatin1String("0") && back->collections.size() == 1
                  && back->collections.first().items == QStringList{id},
              L + QStringLiteral("read back: the same keywords, values and collection"));
    }
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.81.2")) >= 0,
          QStringLiteral("version 0.81.2 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    AppSettings::instance().load();
    AppSettings::instance().backupOnOpen = false;
    AppSettings::instance().backupOnClose = false;
    AppSettings::instance().save();
    QTemporaryDir work;
    const QString W = work.path();

    windowsProject(W);
    ownProjects(W);
    const QString real = qEnvironmentVariable("QB_SCRIV_REAL");
    if (!real.isEmpty())
        realProject(real, W);

    std::printf("%d failure(s)\n", failures);
    return failures;
}
