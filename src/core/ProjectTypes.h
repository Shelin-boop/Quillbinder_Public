#pragma once
// Plain data structures shared by the project model, the UI and compile.

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace qb {

inline QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper(); }

enum class ItemType {
    Draft,      // special: the manuscript folder
    Research,   // special: research folder
    Trash,      // special: trash folder
    Folder,
    Text,
    Image,
    Pdf,
    Media,      // audio / video
    WebPage,    // stored html
    File        // anything else, opened externally
};

QString itemTypeToString(ItemType t);
ItemType itemTypeFromString(const QString &s);
bool isFolderType(ItemType t);
bool isTextType(ItemType t);     // has editable text (Text + all folders)
bool isMediaType(ItemType t);    // stored as external file
// File name suffixes (lower case, without dot) that are audio or video
// (ItemType::Media); audio ones are a subset.
const QStringList &mediaSuffixes();
bool isAudioSuffix(const QString &suffix);

enum class StorageFormat { Markdown, Rtf };

struct Label {
    int id = 0;
    QString name;
    QColor color;
};

struct Status {
    int id = 0;
    QString name;
};

struct Keyword {
    QString id;
    QString name;
    QColor color;
    QString parentId;   // keywords can be grouped (e.g. "Characters" > "John")
};

enum class CustomFieldType { Text, Checkbox, List, Date };

struct CustomField {
    QString id;
    QString title;
    CustomFieldType type = CustomFieldType::Text;
    QStringList options;     // for List
    QString defaultValue;
    bool wrap = true;        // multi-line text in outliner
};

struct SectionType {
    QString id;
    QString name;
};

// "Default types by structure": index = binder level below the compile root
// (0 = root level). The last entry applies to all deeper levels.
struct StructureDefaults {
    QStringList folders;     // section type ids
    QStringList fileGroups;
    QStringList files;
};

struct StyleSheet; // text/Styles.h
namespace script {
struct Data;    // text/Script.h
}

struct Formatting {
    QString fontFamily;      // empty = automatic serif
    double fontSize = 13.0;  // points
    double lineSpacing = 1.2;
    double firstLineIndent = 24.0; // pixels at 100%
    double paragraphSpacing = 4.0; // pixels after paragraph
    // alignment of body text (0.40.3; before only "justify" on or off): Qt::AlignLeft,
    // AlignHCenter, AlignRight or AlignJustify
    Qt::Alignment align = Qt::AlignLeft;
    // paragraph and character styles; null = the built-in set (text/Styles.h)
    QSharedPointer<const StyleSheet> styles;
    // script format (elements of script documents); null = the standard screenplay format (text/Script.h)
    QSharedPointer<const script::Data> script;
};

// The four alignments of body text, from any Qt alignment (0.40.3).
inline Qt::Alignment bodyAlignment(Qt::Alignment a)
{
    return (a & Qt::AlignJustify) ? Qt::AlignJustify
           : (a & Qt::AlignHCenter) ? Qt::AlignHCenter
           : (a & Qt::AlignRight)   ? Qt::AlignRight
                                    : Qt::AlignLeft;
}
// "left", "center", "right", "justify": the project file, settings files and options
inline QString alignmentName(Qt::Alignment a)
{
    a = bodyAlignment(a);
    return a == Qt::AlignJustify   ? QStringLiteral("justify")
           : a == Qt::AlignHCenter ? QStringLiteral("center")
           : a == Qt::AlignRight   ? QStringLiteral("right")
                                   : QStringLiteral("left");
}
inline Qt::Alignment alignmentFromName(const QString &n, Qt::Alignment fallback)
{
    if (n == QLatin1String("left"))
        return Qt::AlignLeft;
    if (n == QLatin1String("center"))
        return Qt::AlignHCenter;
    if (n == QLatin1String("right"))
        return Qt::AlignRight;
    if (n == QLatin1String("justify"))
        return Qt::AlignJustify;
    return fallback;
}

struct Targets {
    int draftTarget = 0;
    bool draftChars = false;
    QDate deadline;
    int sessionTarget = 0;
    bool sessionChars = false;
    QString sessionReset = QStringLiteral("midnight"); // midnight | open | never
    bool countAnywhere = false;
    bool autoFromDeadline = false;
    QString writingDays = QStringLiteral("1111111");  // Mon..Sun
    // Session bookkeeping
    QDateTime sessionStart;
    int sessionStartDraft = 0;
    int sessionStartTotal = 0;
    int sessionStartDraftChars = 0;
    int sessionStartTotalChars = 0;
};

struct Bookmark {
    QString title;
    QString target;   // "qb://<id>" internal, or a URL / file path
    bool isInternal() const { return target.startsWith(QLatin1String("qb://")); }
    QString internalId() const { return isInternal() ? target.mid(5) : QString(); }
};

struct SearchQuery {
    QString text;
    // where to look
    bool inTitle = true, inText = true, inSynopsis = true, inNotes = true,
         inKeywords = true, inLabel = false, inStatus = false, inCustom = true;
    bool wholeWord = false;
    bool caseSensitive = false;
    bool allWords = true;      // all words vs exact phrase
    bool exactPhrase = false;
    bool includeTrash = false;
    // since 0.9
    bool anyWord = false;      // one of the words is enough
    bool regex = false;        // the text is a regular expression
    bool draftOnly = false;    // only documents inside the Draft folder
    int compile = 0;           // 0 = all, 1 = only "Include in Compile", 2 = only excluded ones
    bool invert = false;       // list the documents that do not match
    QStringList scopeIds;      // "binder selection only": these documents and what is inside them

    // Search in one place only (the others off); used by the options menu.
    void searchOnlyIn(bool SearchQuery::*member)
    {
        inTitle = inText = inSynopsis = inNotes = inKeywords = inLabel = inStatus = inCustom = false;
        this->*member = true;
    }
    void searchEverywhere() { inTitle = inText = inSynopsis = inNotes = inKeywords = inLabel = inStatus = inCustom = true; }
    bool searchesEverywhere() const
    {
        return inTitle && inText && inSynopsis && inNotes && inKeywords && inLabel && inStatus && inCustom;
    }
};

// What the filter bar of the corkboard and the outliner shows.
struct ItemFilter {
    QString text;
    bool inTitle = true, inSynopsis = true, inKeywords = false, inText = false;
    int label = -2;            // -2 = any, -1 = no label, else the label's id
    int status = -2;           // the same for the status
    QString keyword;           // keyword id; empty = any
    int compile = 0;           // 0 = all, 1 = included in compile, 2 = excluded
    bool invert = false;       // show what does not match

    bool isActive() const
    {
        return !text.trimmed().isEmpty() || label != -2 || status != -2 || !keyword.isEmpty() || compile != 0;
    }
};

struct Collection {
    QString id;
    QString name;
    QColor color;
    bool isSearch = false;
    SearchQuery query;       // for search collections
    QStringList items;       // for arbitrary collections
};

// A replacement of the project's own (File > Compile, Replacements tab, 0.21);
// applied after the replacements of the compile format.
struct CompileReplacement {
    QString find, replace;
    bool regex = false;
    bool matchCase = true;
    bool wholeWords = false;
    bool enabled = true;
};

// Per-project compile choices (formats themselves live in compile/)
struct CompileProjectSettings {
    QString formatName = QStringLiteral("Manuscript (Times)");
    QString exporterId = QStringLiteral("docx");
    QString compileGroupId;          // empty = Draft
    bool treatAsComplete = false;
    bool includeGroupText = true;
    QString frontMatterId, backMatterId;
    QMap<QString, QString> layoutAssignments; // key "<format>|<sectionTypeId>" -> layout name
    QMap<QString, bool> includeOverrides;     // id -> include (compile dialog ticks)
    QString lastOutputDir;
    QString titleOverride, authorOverride;
    // metadata of the compiled file (Compile > Metadata; 0.14)
    QString subtitle, subject, keywords, publisher, description, language, date;
    QString coverId;                 // a picture of the binder for the e-book cover
    QString identifier;              // e-book id (made once, kept for later versions of the book)
    // 0.21, the compile window after Scrivener's Compile Overview:
    QString fontOverride;            // one font for the whole manuscript; empty = "Determined by Section Layout"
    bool objectSheets = false;       // the sheets of the Objects (characters, locations ...) after the back matter
    QStringList objectCategories;    // ... of these categories (ids); empty = every category
    QList<CompileReplacement> replacements; // the project's own replacements
};

struct WritingDay {
    QDate date;
    int draftWords = 0;   // net words written in draft that day
    int otherWords = 0;
    int draftChars = 0;
    int otherChars = 0;
};

} // namespace qb
