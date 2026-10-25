#pragma once
// Scriptwriting: script formats and their elements.
//
// A script format (Screenplay, Stage Play, Comic Script ...) is a list of
// elements (Scene Heading, Action, Character ...). Every element has a look
// (indents, capitals, alignment), a behaviour for the Tab and Enter keys and
// lists for auto-completion. A project uses one script format; which
// documents are scripts is a property of the document (BinderItem::scriptMode).
//
// In the text an element is a paragraph style of its own kind: the block
// property props::Style holds "Script > <element name>". The name cannot
// clash with the styles of the project (">" is not allowed in their names),
// and all code that knows paragraph styles (storage, compile, redefining)
// handles script elements through styles::find().

#include "core/ProjectTypes.h"
#include "text/Styles.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class QTextDocument;

namespace qb {
namespace script {

// What an element is to other programs (Fountain, Final Draft).
enum class Role {
    General = 0,   // text that is not part of the script format
    SceneHeading,
    Action,
    Character,
    Parenthetical,
    Dialogue,
    Transition,
    Shot,
    Centered,
    Lyrics,
    Section        // act or part heading
};
QString roleKey(Role r);              // "scene", "action" ...
Role roleFromKey(const QString &key);
QString roleLabel(Role r);            // "Scene heading" ... (for the settings window)
QList<Role> elementRoles();           // the roles an element can have

struct Element {
    QString name;
    Role role = Role::Action;
    // ---- look
    bool bold = false, italic = false, underline = false;
    bool allCaps = false;
    QColor color;                 // invalid = automatic
    int align = 0;                // 0 left, 1 centred, 2 right
    double leftMm = 0, rightMm = 0, firstMm = 0;   // indents from the text margins
    double linesBefore = 1;       // blank lines above the paragraph
    double lineSpacing = 1.0;     // 1 = single
    bool keepWithNext = false;
    // ---- keys
    QString onEnter;              // element of the next paragraph after Enter ("" = the same)
    QString onTabEmpty;           // Tab on an empty line changes it to this element ("" = the next in the list)
    QString onTabEnd;             // Tab after text starts a paragraph of this element ("" = nothing)
    QString tabInserts;           // Tab after text types this instead (" - " in scene headings)
    QString prefix, suffix;       // typed automatically around the text ("(" and ")")
    // ---- auto-complete
    QStringList completions;      // offered at the start of the paragraph
    QStringList tabCompletions;   // offered after the text typed by Tab ("DAY", "NIGHT")
    bool completeFromScript = false; // offer what other paragraphs of this element in the document say
    QString key;                  // letter that chooses the element in the elements menu

    bool operator==(const Element &o) const;
    bool operator!=(const Element &o) const { return !(*this == o); }
};

struct Format {
    QString name;                 // "Screenplay"
    QString description;
    QString fontFamily;           // empty = typewriter font of the system (Courier Prime, Courier New ...)
    double fontSize = 12;         // points
    QString unit = QStringLiteral("in");   // unit of the settings window: "in" or "cm"
    QString firstElement;         // element of the first paragraph of an empty script
    QList<Element> elements;

    const Element *find(const QString &name) const;
    int indexOf(const QString &name) const;
    const Element *byRole(Role r) const;
    const Element *first() const;  // firstElement, or the first of the list
    bool operator==(const Format &o) const;
    bool operator!=(const Format &o) const { return !(*this == o); }
};

// What a Formatting carries: the format and its elements as paragraph styles.
struct Data {
    Format format;
    StyleSheet sheet;
};

// ---- formats
QList<Format> builtIn();                 // Screenplay, Stage Play, Comic Script
const Format &standard();                // Screenplay
enum class Kind { BuiltIn, Addon, Own };
struct Entry {
    Format format;
    Kind kind = Kind::BuiltIn;
    QString path;                        // file of add-on and own formats
};
// Built-in formats, formats of add-ons (scripts/*.json) and the writer's own
// (~/.local/share/<app>/scripts/*.json), in this order; a later format with
// the name of an earlier one replaces it.
QList<Entry> available();
QString ownDir();
bool saveOwn(const Format &f, QString *error = nullptr);
bool removeOwn(const QString &name);
QJsonObject toJson(const Format &f);
Format fromJson(const QJsonObject &o, bool *ok = nullptr);
bool readFile(const QString &path, Format *f, QString *error = nullptr);
bool writeFile(const QString &path, const Format &f, QString *error = nullptr);
// Valid as an element name (it becomes part of a style name in the text files).
bool validElementName(const QString &name);

// ---- elements as paragraph styles
QString styleName(const QString &element);        // "Script > Scene Heading"
bool isScriptStyle(const QString &styleName);
QString elementName(const QString &styleName);    // "" if it is not the style of an element
// The typewriter font a format is shown in.
QString resolvedFontFamily(const Format &f);
StyleSheet toStyleSheet(const Format &f);

// The script format of a Formatting (the standard format if it has none).
const Data &data(const Formatting &f);
inline const Format &format(const Formatting &f) { return data(f).format; }
Formatting withFormat(Formatting f, const Format &format);
// The element of a paragraph style name in this formatting (nullptr = none).
const Element *element(const Formatting &f, const QString &styleName);
// Old element name -> new element name for a change from one format to
// another: elements missing in the new format go to the one with the same role.
QHash<QString, QString> renamedStyles(const Format &from, const Format &to,
                                      const QHash<QString, QString> &renamedElements = {});

// ---- documents
// Element name of the paragraph at a position ("" = general text).
QString elementAt(const QTextDocument *doc, int position);
// What other paragraphs of an element say (for auto-completion). part 0: the
// text before the element's tabInserts, part 1: the text after it.
QStringList usedTexts(const QTextDocument *doc, const Formatting &f, const Element &el, int part, int exceptBlock = -1);
// Does the document contain paragraphs of script elements?
bool hasElements(const QTextDocument *doc);

} // namespace script
} // namespace qb
