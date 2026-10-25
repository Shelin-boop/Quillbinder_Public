// Headless checks for scriptwriting: script formats, elements as paragraph
// styles, Fountain and Final Draft files, import, compile (milestone M9).
// Part of qb_tests; scriptChecks() returns the number of failed checks.
#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "import/FileImporter.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/Script.h"
#include "text/ScriptIO.h"
#include "text/Styles.h"
#include "text/TextProps.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <cstdio>

using namespace qb;

namespace {

int failed = 0;
void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failed;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail));
    }
}

// "scene:INT. HOUSE - DAY|action:..." (roles and texts of a screenplay)
QString outline(const script::Screenplay &sp)
{
    QStringList out;
    for (const script::Para &p : sp.paras)
        out << (p.pageBreak ? QStringLiteral("^") : QString()) + script::roleKey(p.role) + QLatin1Char(':') + p.text();
    return out.join(QLatin1Char('|'));
}

// "Scene Heading:INT. HOUSE - DAY|..." (elements and texts of a document)
QString elements(const QTextDocument *doc)
{
    QStringList out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString style = b.blockFormat().stringProperty(props::Style);
        out << (script::isScriptStyle(style) ? script::elementName(style) : style) + QLatin1Char(':') + b.text();
    }
    return out.join(QLatin1Char('|'));
}

bool writeText(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const char *kFountain =
    "Title: The Lighthouse\n"
    "Credit: Written by\n"
    "Author: Ada Marsh\n"
    "Draft date: 2026-10-04\n"
    "Contact:\n"
    "    12 Harbour Road\n"
    "    Port Ellen\n"
    "\n"
    "# Act One\n"
    "\n"
    "= The keeper finds the letter.\n"
    "\n"
    "INT. LIGHTHOUSE - NIGHT\n"
    "\n"
    "Rain against the glass. MARTA climbs the *iron* stairs, a **lamp** in her hand.\n"
    "She stops.[[check the stair count]]\n"
    "\n"
    "MARTA\n"
    "(whispering)\n"
    "Who left this _here_?\n"
    "\n"
    "/* cut for length\n"
    "OLD VOICE\n"
    "Nobody. */\n"
    "\n"
    "TOM (O.S.)\n"
    "Marta? The boat is in.\n"
    "It is early.\n"
    "\n"
    "@McAllister ^\n"
    "Early is good.\n"
    "\n"
    "CUT TO:\n"
    "\n"
    ".THE QUAY #12#\n"
    "\n"
    "!EXT. is how the sign begins, the rest is gone.\n"
    "\n"
    "> FADE OUT.\n"
    "\n"
    "> THE END <\n"
    "\n"
    "===\n"
    "\n"
    "EXT. HARBOUR - DAY\n"
    "\n"
    "MARTA\n"
    "~The sea is wide\n"
    "\n"
    "A gull.\n";

} // namespace

int scriptChecks()
{
    using namespace script;
    std::printf("\n---- scriptwriting\n");

    // ------------------------------------------------------------ formats
    {
        const QList<Format> all = builtIn();
        QStringList names, problems;
        for (const Format &f : all) {
            names << f.name;
            QSet<QString> keys;
            if (!f.first())
                problems << f.name + QLatin1String(": no first element");
            for (const Element &e : f.elements) {
                if (!validElementName(e.name))
                    problems << f.name + QLatin1String(": name ") + e.name;
                for (const QString &target : {e.onEnter, e.onTabEmpty, e.onTabEnd})
                    if (!target.isEmpty() && !f.find(target))
                        problems << f.name + QLatin1String(": ") + e.name + QLatin1String(" -> ") + target;
                if (!e.key.isEmpty() && keys.contains(e.key.toUpper()))
                    problems << f.name + QLatin1String(": key ") + e.key;
                keys.insert(e.key.toUpper());
                if (e.key.compare(QLatin1String("G"), Qt::CaseInsensitive) == 0 && e.name != QLatin1String("Page"))
                    problems << f.name + QLatin1String(": G is the key of General Text");
            }
        }
        check(names == QStringList({QStringLiteral("Screenplay"), QStringLiteral("Stage Play"), QStringLiteral("Comic Script")})
                  && problems.isEmpty() && standard().name == QLatin1String("Screenplay"),
              QStringLiteral("script formats: Screenplay, Stage Play, Comic Script; every Tab and Enter target exists, keys are unique"),
              names.join(QLatin1String(", ")) + QLatin1String(" / ") + problems.join(QLatin1String("; ")));

        bool same = true;
        for (const Format &f : all) {
            bool ok = false;
            const Format back = fromJson(QJsonDocument::fromJson(QJsonDocument(toJson(f)).toJson()).object(), &ok);
            same = same && ok && back == f;
        }
        bool bad = true;
        fromJson(QJsonObject(), &bad);
        QTemporaryDir tmp;
        Format fromFile;
        QString err;
        const bool wrote = writeFile(tmp.filePath(QStringLiteral("x.json")), all.at(1), &err);
        const bool read = readFile(tmp.filePath(QStringLiteral("x.json")), &fromFile, &err);
        Format none;
        const bool readBad = writeText(tmp.filePath(QStringLiteral("bad.json")), "{\"name\":") && readFile(tmp.filePath(QStringLiteral("bad.json")), &none, &err);
        check(same && !bad && wrote && read && fromFile == all.at(1) && !readBad && !err.isEmpty(),
              QStringLiteral("script formats: JSON round trip, files, a broken file is refused"), err);
    }

    // ------------------------------------------------------------ elements as styles
    {
        Formatting f;
        const Style *ch = styles::find(f, styleName(QStringLiteral("Character")));
        const Element *el = element(f, QStringLiteral("Script > Character"));
        const QTextBlockFormat bf = display::blockFormatFor(f, QStringLiteral("Script > Character"));
        const QTextBlockFormat dlg = display::blockFormatFor(f, QStringLiteral("Script > Dialogue"));
        const QTextCharFormat cf = display::charFormatFor(f, QStringLiteral("Script > Scene Heading"));
        const double inch = 96.0;
        check(ch && el && el->role == Role::Character && isScriptStyle(QStringLiteral("Script > Character"))
                  && !isScriptStyle(QStringLiteral("Heading 1")) && elementName(QStringLiteral("Script > Scene Heading")) == QLatin1String("Scene Heading")
                  && qAbs(bf.leftMargin() - 2.2 * inch) < 1.5 && qAbs(dlg.leftMargin() - 1.0 * inch) < 1.5
                  && qAbs(dlg.rightMargin() - 1.5 * inch) < 1.5 && !styles::find(f, QStringLiteral("Script > Nothing"))
                  && styles::sheet(f).indexOf(QStringLiteral("Script > Character")) < 0,
              QStringLiteral("elements are paragraph styles \"Script > <name>\" with the indents of the format; they are not among the project's styles"),
              QStringLiteral("left %1 dialogue %2/%3").arg(bf.leftMargin()).arg(dlg.leftMargin()).arg(dlg.rightMargin()));
        const QFontInfo fi(cf.font());
        check(cf.fontFixedPitch() || fi.fixedPitch() || !cf.fontFamilies().toStringList().isEmpty(),
              QStringLiteral("elements are shown in the typewriter font of the format"),
              cf.fontFamilies().toStringList().join(QLatin1Char(',')));
    }

    // ------------------------------------------------------------ Fountain
    const Screenplay sp = fromFountain(QString::fromUtf8(kFountain));
    {
        const QString o = outline(sp);
        const QString want = QStringLiteral(
            "section:Act One|action:|scene:INT. LIGHTHOUSE - NIGHT|"
            "action:Rain against the glass. MARTA climbs the iron stairs, a lamp in her hand.\nShe stops.|"
            "character:MARTA|parenthetical:(whispering)|dialogue:Who left this here?|"
            "character:TOM (O.S.)|dialogue:Marta? The boat is in.\nIt is early.|"
            "character:McAllister|dialogue:Early is good.|transition:CUT TO:|scene:THE QUAY|"
            "action:EXT. is how the sign begins, the rest is gone.|transition:FADE OUT.|centered:THE END|"
            "^scene:EXT. HARBOUR - DAY|character:MARTA|lyrics:The sea is wide|action:A gull.");
        check(o == want, QStringLiteral("Fountain: scene headings, action, character, parenthetical, dialogue, transitions, "
                                        "centred text, lyrics, sections, forced elements, scene numbers, boneyard, page break"),
              o);
        check(sp.titleValue(QStringLiteral("title")) == QLatin1String("The Lighthouse")
                  && sp.titleValue(QStringLiteral("Author")) == QLatin1String("Ada Marsh")
                  && sp.titleValue(QStringLiteral("Contact")) == QLatin1String("12 Harbour Road\nPort Ellen")
                  && titlePageText(sp).contains(QLatin1String("Draft date: 2026-10-04")),
              QStringLiteral("Fountain: title page with values over several lines"), titlePageText(sp));
        // emphasis and notes
        const Para &action = sp.paras.at(3);
        bool italic = false, bold = false, note = false;
        for (const Span &s : action.spans) {
            italic = italic || (s.italic && s.text == QLatin1String("iron"));
            bold = bold || (s.bold && s.text == QLatin1String("lamp"));
            note = note || (s.note && s.text == QLatin1String("check the stair count"));
        }
        bool under = false;
        for (const Span &s : sp.paras.at(6).spans)
            under = under || (s.underline && s.text == QLatin1String("here"));
        check(italic && bold && note && under && sp.paras.at(1).spans.size() == 1 && sp.paras.at(1).spans.first().note,
              QStringLiteral("Fountain: *italic*, **bold**, _underline_, [[notes]] and = synopsis lines"));

        const QString written = toFountain(sp);
        const Screenplay again = fromFountain(written);
        const QString twice = toFountain(again);
        check(outline(again) == o && twice == written && written.startsWith(QLatin1String("Title: The Lighthouse\n"))
                  && written.contains(QLatin1String("\n.THE QUAY\n")) && written.contains(QLatin1String("\n@McAllister\n"))
                  && written.contains(QLatin1String("\n!EXT. is how the sign"))
                  && written.contains(QLatin1String("MARTA\n(whispering)\nWho left this _here_?\n"))
                  && written.contains(QLatin1String("\n===\n")) && written.contains(QLatin1String("> THE END <"))
                  && written.contains(QLatin1String("[[check the stair count]]")) && written.contains(QLatin1String("the *iron* stairs, a **lamp**")),
              QStringLiteral("Fountain: written and read again gives the same script; writing is stable"), written);

        // what would be misread is marked
        Screenplay tricky;
        auto para = [](Role r, const QString &t) {
            Para p;
            p.role = r;
            Span s;
            s.text = t;
            p.spans << s;
            return p;
        };
        tricky.paras << para(Role::Action, QStringLiteral("INT. is short for interior"))
                     << para(Role::Action, QStringLiteral("5 * 3 and a_b_c and [[brackets"))
                     << para(Role::SceneHeading, QStringLiteral("the old mill")) << para(Role::Character, QStringLiteral("della"))
                     << para(Role::Dialogue, QStringLiteral("Hello.")) << para(Role::Transition, QStringLiteral("Lights down"))
                     << para(Role::Action, QStringLiteral("> not a transition")) << para(Role::Action, QStringLiteral("# not a section"))
                     << para(Role::Shot, QStringLiteral("close on the key"));
        const QString tw = toFountain(tricky);
        const Screenplay tb = fromFountain(tw);
        check(outline(tb) == QStringLiteral("action:INT. is short for interior|action:5 * 3 and a_b_c and [[brackets|scene:THE OLD MILL|"
                                            "character:della|dialogue:Hello.|transition:LIGHTS DOWN|action:> not a transition|"
                                            "action:# not a section|action:CLOSE ON THE KEY"),
              QStringLiteral("Fountain: text that would be read as another element is marked when written"),
              tw + QLatin1String("\n-> ") + outline(tb));
    }

    // ------------------------------------------------------------ Final Draft
    {
        const QByteArray fdx = toFdx(sp);
        QDomDocument dom;
        QString err;
        const bool wellFormed = dom.setContent(fdx, &err);
        QStringList types;
        const QDomElement content = dom.documentElement().firstChildElement(QStringLiteral("Content"));
        for (QDomElement p = content.firstChildElement(QStringLiteral("Paragraph")); !p.isNull();
             p = p.nextSiblingElement(QStringLiteral("Paragraph")))
            if (!types.contains(p.attribute(QStringLiteral("Type"))))
                types << p.attribute(QStringLiteral("Type"));
        check(wellFormed && dom.documentElement().tagName() == QLatin1String("FinalDraft")
                  && dom.documentElement().attribute(QStringLiteral("DocumentType")) == QLatin1String("Script")
                  && types.contains(QStringLiteral("Scene Heading")) && types.contains(QStringLiteral("Action"))
                  && types.contains(QStringLiteral("Character")) && types.contains(QStringLiteral("Parenthetical"))
                  && types.contains(QStringLiteral("Dialogue")) && types.contains(QStringLiteral("Transition"))
                  && fdx.contains("Style=\"Italic\"") && fdx.contains("Style=\"Bold\"") && fdx.contains("Style=\"Underline\"")
                  && fdx.contains("<TitlePage>") && fdx.contains("The Lighthouse") && fdx.contains("StartsNewPage=\"Yes\""),
              QStringLiteral("Final Draft: well-formed .fdx with paragraph types, text styles, title page and page break"),
              err + QLatin1Char(' ') + types.join(QLatin1String(", ")));
        Screenplay back;
        const bool ok = fromFdx(fdx, &back, &err);
        // notes of the writer are not part of a Final Draft script; sections come back as act headings
        Screenplay expect = sp;
        for (Para &p : expect.paras) {
            QList<Span> keep;
            for (const Span &s : p.spans)
                if (!s.note)
                    keep << s;
            p.spans = keep;
        }
        QStringList a, b;
        for (const Para &p : expect.paras)
            if (!p.isEmpty())
                a << roleKey(p.role) + QLatin1Char(':') + p.text();
        for (const Para &p : back.paras)
            b << roleKey(p.role) + QLatin1Char(':') + p.text();
        check(ok && a == b && back.titleValue(QStringLiteral("Title")) == QLatin1String("The Lighthouse")
                  && back.titleValue(QStringLiteral("Author")) == QLatin1String("Ada Marsh"),
              QStringLiteral("Final Draft: read again gives the same elements and texts, title and author"),
              b.join(QLatin1Char('|')) + QLatin1String("\nwant ") + a.join(QLatin1Char('|')) + QLatin1String("\ntitle page: ") + titlePageText(back));

        // a file as another program writes it
        const QByteArray foreign =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n"
            "<FinalDraft DocumentType=\"Script\" Template=\"No\" Version=\"5\">\n"
            "<Content>\n"
            "<Paragraph Number=\"1\" Type=\"Scene Heading\"><SceneProperties Length=\"1/8\" Page=\"1\" Title=\"\"/>"
            "<Text>INT. KITCHEN - DAY</Text></Paragraph>\n"
            "<Paragraph Type=\"Action\"><Text>Ben </Text><Text Style=\"Bold+Italic\">drops</Text><Text> the &amp; cup.</Text></Paragraph>\n"
            "<Paragraph Type=\"Action\"><DualDialogue>"
            "<Paragraph Type=\"Character\"><Text>BEN</Text></Paragraph>"
            "<Paragraph Type=\"Dialogue\"><Text>Sorry!</Text></Paragraph>"
            "<Paragraph Type=\"Character\"><Text>IDA</Text></Paragraph>"
            "<Paragraph Type=\"Parenthetical\"><Text>(laughing)</Text></Paragraph>"
            "<Paragraph Type=\"Dialogue\"><Text Style=\"Underline\">Again?</Text></Paragraph>"
            "</DualDialogue></Paragraph>\n"
            "<Paragraph Type=\"Shot\"><Text>CLOSE ON THE CUP</Text></Paragraph>\n"
            "<Paragraph Type=\"General\"><Text>A note to self.</Text></Paragraph>\n"
            "<Paragraph Type=\"New Act\"><Text>ACT TWO</Text></Paragraph>\n"
            "<Paragraph Type=\"Transition\"><Text>CUT TO:</Text></Paragraph>\n"
            "<Paragraph Type=\"Unknown Kind\"><Text>Something else.</Text></Paragraph>\n"
            "</Content>\n"
            "<TitlePage><Content>"
            "<Paragraph Alignment=\"Center\"><Text>KITCHEN STORIES</Text></Paragraph>"
            "<Paragraph Alignment=\"Center\"><Text></Text></Paragraph>"
            "<Paragraph Alignment=\"Center\"><Text>Written by</Text></Paragraph>"
            "<Paragraph Alignment=\"Center\"><Text></Text></Paragraph>"
            "<Paragraph Alignment=\"Center\"><Text>Ida Voss</Text></Paragraph>"
            "</Content></TitlePage>\n"
            "</FinalDraft>\n";
        Screenplay other;
        const bool ok2 = fromFdx(foreign, &other, &err);
        bool boldItalic = false;
        if (other.paras.size() > 1)
            for (const Span &s : other.paras.at(1).spans)
                boldItalic = boldItalic || (s.bold && s.italic && s.text == QLatin1String("drops"));
        const QString oo = outline(other);
        check(ok2 && oo == QStringLiteral("scene:INT. KITCHEN - DAY|action:Ben drops the & cup.|character:BEN|dialogue:Sorry!|"
                                          "character:IDA|parenthetical:(laughing)|dialogue:Again?|shot:CLOSE ON THE CUP|"
                                          "general:A note to self.|section:ACT TWO|transition:CUT TO:|action:Something else.")
                  && boldItalic && other.titleValue(QStringLiteral("Title")) == QLatin1String("KITCHEN STORIES")
                  && other.titleValue(QStringLiteral("Author")) == QLatin1String("Ida Voss"),
              QStringLiteral("Final Draft: a foreign file with scene properties, dual dialogue, combined styles, unknown types"),
              oo + QLatin1String("\ntitle page: ") + titlePageText(other) + QLatin1Char(' ') + err);
        Screenplay nothing;
        QString e1, e2;
        check(!fromFdx("<html><body/></html>", &nothing, &e1) && !e1.isEmpty() && !fromFdx("not xml at all", &nothing, &e2) && !e2.isEmpty(),
              QStringLiteral("Final Draft: a file that is no script is refused with a message"), e1 + QLatin1String(" / ") + e2);
    }

    // ------------------------------------------------------------ documents and storage
    {
        Formatting f;
        QTextDocument doc;
        toDocument(sp, &doc, f);
        const QString els = elements(&doc);
        check(els.startsWith(QLatin1String("Heading 1:Act One|")) && els.contains(QLatin1String("|Scene Heading:INT. LIGHTHOUSE - NIGHT|Action:Rain against"))
                  && els.contains(QLatin1String("|Character:MARTA|Parenthetical:(whispering)|Dialogue:Who left this here?|"))
                  && els.contains(QLatin1String("|Transition:CUT TO:|Scene Heading:THE QUAY|"))
                  && els.contains(QLatin1String("|:THE END|")) && els.endsWith(QLatin1String("|Character:MARTA|Dialogue:The sea is wide|Action:A gull.")),
              QStringLiteral("a script becomes a document with the elements of the project's format"), els);
        const Screenplay fromDoc = fromDocument(&doc, f);
        QString want = outline(sp);
        // the screenplay format has no lyrics element; its character names are shown and written in capitals
        want.replace(QLatin1String("lyrics:"), QLatin1String("dialogue:"));
        want.replace(QLatin1String("character:McAllister"), QLatin1String("character:MCALLISTER"));
        check(outline(fromDoc) == want, QStringLiteral("a document becomes a script again: roles, texts, notes, page break"),
              outline(fromDoc));

        // Markdown storage
        const QString mdText = md::write(&doc, f);
        QTextDocument back;
        md::read(mdText, &back, f);
        check(mdText.contains(QLatin1String("::Script > Scene Heading:: INT. LIGHTHOUSE - NIGHT\n"))
                  && mdText.contains(QLatin1String("::Script > Character:: MARTA\n")) && elements(&back) == els
                  && md::write(&back, f) == mdText && outline(fromDocument(&back, f)) == want
                  && !mdText.contains(QLatin1String("font-family"))
                  && mdText.count(QLatin1String("{:")) == 1 && mdText.contains(QLatin1String("{: page=before} ::Script > Scene Heading:: EXT. HARBOUR - DAY\n")),
              QStringLiteral("Markdown storage: elements are stored by name only, a page break as \"{: page=before}\", and read back the same"), mdText);
        const QTextBlock chBlock = back.findBlockByNumber(4);
        check(chBlock.text() == QLatin1String("MARTA") && qAbs(chBlock.blockFormat().leftMargin() - 2.2 * 96) < 1.5,
              QStringLiteral("Markdown storage: the element's indent is back after reading"),
              QStringLiteral("%1 %2").arg(chBlock.text()).arg(chBlock.blockFormat().leftMargin()));

        // RTF storage
        const QByteArray rtfData = rtf::write(&doc, f);
        QTextDocument rback;
        const rtf::ReadResult rr = rtf::read(rtfData, &rback, f);
        display::normalizeDocument(&rback, f);
        check(rr.ok && rtfData.contains("Script > Scene Heading;") && elements(&rback) == els
                  && outline(fromDocument(&rback, f)) == want,
              QStringLiteral("RTF storage: elements are entries of the style sheet, and read back the same"),
              elements(&rback));
        QTextDocument plain;
        QTextCursor(&plain).insertText(QStringLiteral("Just prose."));
        check(!rtf::write(&plain, f).contains("Script > "),
              QStringLiteral("RTF storage: a text without script elements has no script styles in its style sheet"));

        // another format: elements by role
        const Formatting stage = withFormat(f, builtIn().at(1));
        QTextDocument sdoc;
        toDocument(sp, &sdoc, stage);
        const QString sels = elements(&sdoc);
        check(sels.startsWith(QLatin1String("Act Heading:Act One|")) && sels.contains(QLatin1String("|Stage Direction:Rain against"))
                  && format(stage).name == QLatin1String("Stage Play") && format(f).name == QLatin1String("Screenplay"),
              QStringLiteral("with the stage play format an imported script gets act headings and stage directions"), sels);

        // completion sources
        const Element *chEl = format(f).find(QStringLiteral("Character"));
        const Element *scEl = format(f).find(QStringLiteral("Scene Heading"));
        const QStringList who = usedTexts(&doc, f, *chEl, 0);
        const QStringList where = usedTexts(&doc, f, *scEl, 0);
        const QStringList when = usedTexts(&doc, f, *scEl, 1);
        check(who.contains(QStringLiteral("MARTA")) && who.contains(QStringLiteral("TOM")) && who.count(QStringLiteral("MARTA")) == 1
                  && where.contains(QStringLiteral("INT. LIGHTHOUSE")) && when.contains(QStringLiteral("NIGHT")) && hasElements(&doc) && !hasElements(&plain),
              QStringLiteral("auto-completion knows the characters, places and times of the script"),
              who.join(QLatin1Char(',')) + QLatin1String(" / ") + where.join(QLatin1Char(',')) + QLatin1String(" / ") + when.join(QLatin1Char(',')));
    }

    // ------------------------------------------------------------ project
    {
        QTemporaryDir tmp;
        QString err;
        auto p = Project::create(tmp.path(), QStringLiteral("Scripts"), &err);
        check(p != nullptr, QStringLiteral("script project created"), err);
        if (!p)
            return failed;
        const QString folder = p->folderPath();
        writeText(tmp.filePath(QStringLiteral("lighthouse.fountain")), kFountain);
        QStringList warnings;
        const QList<BinderItem *> imported = importer::importFiles(p.get(), {tmp.filePath(QStringLiteral("lighthouse.fountain"))},
                                                                   p->draft(), -1, &warnings);
        BinderItem *whole = imported.value(0);
        check(whole && whole->scriptMode && whole->title == QLatin1String("lighthouse")
                  && p->docs()->notesPlainText(whole->id).contains(QLatin1String("Author: Ada Marsh"))
                  && elements(p->docs()->text(whole->id)).contains(QLatin1String("Scene Heading:INT. LIGHTHOUSE - NIGHT")),
              QStringLiteral("import: a .fountain file becomes a script document, the title page goes into its notes"),
              warnings.join(QLatin1Char(';')));
        const QString wholeId = whole ? whole->id : QString();
        QString sceneId;

        const QList<BinderItem *> scenes = importer::importAndSplit(p.get(), tmp.filePath(QStringLiteral("lighthouse.fountain")),
                                                                    importer::splitAtScenes(), false, p->draft(), -1, &err);
        QStringList sceneTitles;
        bool allScript = true;
        for (BinderItem *it : scenes) {
            sceneTitles << it->title;
            allScript = allScript && it->scriptMode;
        }
        check(sceneTitles == QStringList({QStringLiteral("Act One"), QStringLiteral("INT. LIGHTHOUSE - NIGHT"), QStringLiteral("THE QUAY"),
                                                 QStringLiteral("EXT. HARBOUR - DAY")}),
              QStringLiteral("import and split: a document per scene, named after its heading"), sceneTitles.join(QLatin1String(" | ")));
        if (!scenes.isEmpty())
            sceneId = scenes.last()->id;
        check(allScript && !scenes.isEmpty() && elements(p->docs()->text(scenes.last()->id)).startsWith(QLatin1String("Scene Heading:EXT. HARBOUR - DAY|Character:MARTA|")) && p->docs()->plainText(scenes.last()->id).startsWith(QLatin1String("EXT. HARBOUR - DAY")),
              QStringLiteral("import and split: the scenes are script documents and keep their heading in the text"));

        // Final Draft file through the importer
        writeText(tmp.filePath(QStringLiteral("draft.fdx")), toFdx(sp));
        const QList<BinderItem *> fd = importer::importFiles(p.get(), {tmp.filePath(QStringLiteral("draft.fdx"))}, p->draft(), -1, &warnings);
        check(fd.size() == 1 && fd.first()->scriptMode
                  && elements(p->docs()->text(fd.first()->id)).contains(QLatin1String("Character:MARTA|Parenthetical:(whispering)|Dialogue:Who left this here?")),
              QStringLiteral("import: a .fdx file becomes a script document"));

        // script mode in an empty document
        BinderItem *fresh = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("New scene"));
        p->setScriptMode(fresh, true);
        const QTextDocument *fdoc = p->docs()->text(fresh->id);
        check(fresh->scriptMode && fdoc->begin().blockFormat().stringProperty(props::Style) == QLatin1String("Script > Scene Heading")
                  && !fdoc->isUndoAvailable(),
              QStringLiteral("script mode in an empty document: the first paragraph is the first element of the format"),
              fdoc->begin().blockFormat().stringProperty(props::Style));
        BinderItem *folderItem = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Act"));
        p->setScriptMode(folderItem, true);
        const QString freshId = fresh->id;

        check(p->save(&err), QStringLiteral("script project saved"), err);
        p.reset();
        p = Project::open(folder, &err);
        {
            // the project file says which version it needs
            const QByteArray idx = readAll(p ? p->indexPath() : QString());
            QTemporaryDir other;
            auto plain = Project::create(other.path(), QStringLiteral("Prose"), &err);
            plain->save(&err);
            const QByteArray plainIdx = readAll(plain->indexPath());
            QByteArray future = plainIdx;
            future.replace("<QuillbinderProject ", "<QuillbinderProject needs=\"0.30.0\" ");
            const QString pf = plain->indexPath();
            plain.reset();
            writeText(pf, future);
            auto fp = Project::open(pf, &err);
            const QString read = fp ? fp->needsVersion : QString();
            if (fp)
                fp->save(&err);
            check(idx.contains("needs=\"0.10.0\"") && !plainIdx.contains("needs=") && read == QLatin1String("0.30.0")
                      && readAll(pf).contains("needs=\"0.30.0\"") && Project::compareVersionText(QStringLiteral("0.10.0"), QStringLiteral("0.9.1")) > 0
                      && Project::compareVersionText(QStringLiteral("0.10"), QStringLiteral("0.10.0")) == 0
                      && Project::compareVersionText(QString(), QStringLiteral("0.1")) < 0,
                  QStringLiteral("a project with scripts says that it needs version 0.10; a mark of a newer version is kept"),
                  QString::fromUtf8(idx.left(200)));
        }
        check(p && p->item(wholeId) && p->item(wholeId)->scriptMode && p->item(freshId)->scriptMode
                  && format(p->formatting).name == QLatin1String("Screenplay") && !p->formatting.script
                  && elements(p->docs()->text(wholeId)).contains(QLatin1String("|Character:TOM (O.S.)|Dialogue:Marta? The boat is in.")),
              QStringLiteral("reopened: script mode and elements are kept; the standard format is not written into the project"), err);
        if (!p)
            return failed;

        // ---- another format: closed documents are rewritten
        const QString pathWhole = p->docs()->textPath(wholeId);
        const Format stage = builtIn().at(1);
        int steps = 0;
        // one document is open (loaded), the other is not
        p->docs()->text(wholeId);
        const bool changed = p->setScriptFormat(stage, {}, [&](int, int) { ++steps; return true; }, &err);
        p->save(&err);
        const QString whole2 = elements(p->docs()->text(wholeId));
        const QByteArray sceneFile = sceneId.isEmpty() ? QByteArray() : readAll(p->docs()->textPath(sceneId));
        check(changed && format(p->formatting).name == QLatin1String("Stage Play") && whole2.contains(QLatin1String("|Stage Direction:Rain against"))
                  && whole2.contains(QLatin1String("|Character:MARTA|Parenthetical:(whispering)|Dialogue:Who left this here?|"))
                  && whole2.contains(QLatin1String("|Scene Heading:THE QUAY|")) && !whole2.contains(QLatin1String("|Action:"))
                  && sceneFile.contains("::Script > Scene Heading:: EXT. HARBOUR - DAY") && sceneFile.contains("::Script > Stage Direction:: A gull.")
                  && readAll(pathWhole).contains("::Script > Stage Direction:: Rain"),
              QStringLiteral("another script format: elements go to the element of the same kind, in open documents and in the files of closed ones"),
              whole2 + QLatin1String("\n") + QString::fromUtf8(sceneFile) + err);
        const QTextBlock dirBlock = p->docs()->text(wholeId)->findBlockByNumber(3);
        check(dirBlock.text().startsWith(QLatin1String("Rain")) && qAbs(dirBlock.blockFormat().leftMargin() - 2.0 * 96) < 1.5,
              QStringLiteral("another script format: the paragraphs have the new layout"),
              QStringLiteral("%1").arg(dirBlock.blockFormat().leftMargin()));
        p.reset();
        p = Project::open(folder, &err);
        check(p && p->formatting.script && format(p->formatting).name == QLatin1String("Stage Play")
                  && format(p->formatting) == stage && elements(p->docs()->text(wholeId)) == whole2,
              QStringLiteral("another script format: stored in the project and read again"), err);
        if (!p)
            return failed;

        // ---- a format of one's own: a renamed element, a removed one
        Format own = format(p->formatting);
        own.name = QStringLiteral("My Play");
        const int di = own.indexOf(QStringLiteral("Stage Direction"));
        own.elements[di].name = QStringLiteral("Direction");
        own.elements[di].leftMm = 30;
        for (Element &e : own.elements)
            for (QString *t : {&e.onEnter, &e.onTabEmpty, &e.onTabEnd})
                if (*t == QLatin1String("Stage Direction"))
                    *t = QStringLiteral("Direction");
        own.elements.removeAt(own.indexOf(QStringLiteral("Transition")));
        QHash<QString, QString> renamed;
        renamed.insert(QStringLiteral("Stage Direction"), QStringLiteral("Direction"));
        const bool ok3 = p->setScriptFormat(own, renamed, {}, &err);
        const QString whole3 = elements(p->docs()->text(wholeId));
        const QTextBlock tr = p->docs()->text(wholeId)->findBlockByNumber(11);
        check(ok3 && whole3.contains(QLatin1String("|Direction:Rain against")) && !whole3.contains(QLatin1String("Stage Direction"))
                  && whole3.contains(QLatin1String("|:CUT TO:|")) && (tr.blockFormat().alignment() & Qt::AlignRight),
              QStringLiteral("own format: a renamed element keeps its paragraphs; a removed element leaves general text that keeps its look"),
              whole3 + QStringLiteral(" / align %1 '%2'").arg(int(tr.blockFormat().alignment())).arg(tr.text()));
        check(saveOwn(own, &err) && QFileInfo::exists(ownDir()) , QStringLiteral("own format saved for other projects"), err);
        QStringList avail;
        Kind kindOfOwn = Kind::BuiltIn;
        for (const Entry &e : available()) {
            avail << e.format.name;
            if (e.format.name == QLatin1String("My Play"))
                kindOfOwn = e.kind;
        }
        const bool removed = removeOwn(QStringLiteral("My Play"));
        QStringList after;
        for (const Entry &e : available())
            after << e.format.name;
        check(avail.contains(QStringLiteral("My Play")) && kindOfOwn == Kind::Own && avail.indexOf(QStringLiteral("Screenplay")) == 0 && removed
                  && !after.contains(QStringLiteral("My Play")) && after.size() == 3,
              QStringLiteral("own formats are listed after the built-in ones and can be removed"), avail.join(QLatin1String(", ")));

        // ---- compile
        p->setScriptFormat(standard(), {}, {}, &err);
        compile::PluginManager::instance().load();
        ExporterInterface *fountainOut = compile::PluginManager::instance().exporter(QStringLiteral("fountain"));
        ExporterInterface *fdxOut = compile::PluginManager::instance().exporter(QStringLiteral("fdx"));
        check(fountainOut && fdxOut, QStringLiteral("compile: Fountain and Final Draft are among the output formats"));
        // only the whole script is compiled
        for (BinderItem *it : p->draft()->children)
            it->includeInCompile = it->id == wholeId;
        compile::Format fmt = compile::formatByName(QStringLiteral("Default (Editor Formatting)"));
        CompileProjectSettings cs = p->compile;
        compile::Compiler c(p.get(), fmt, cs, {QStringLiteral("Ada"), QStringLiteral("Marsh")});
        const compile::Manuscript m = c.build();
        int sceneParas = 0, charParas = 0;
        double charLeft = 0;
        for (const compile::Paragraph &mp : m.paragraphs) {
            sceneParas += mp.script == QLatin1String("scene");
            if (mp.script == QLatin1String("character")) {
                ++charParas;
                charLeft = mp.leftIndentMm;
            }
        }
        check(sceneParas == 3 && charParas == 4 && qAbs(charLeft - 2.2 * 25.4) < 0.6,
              QStringLiteral("compile: script paragraphs carry their role and their indents"),
              QStringLiteral("scenes %1 characters %2 left %3").arg(sceneParas).arg(charParas).arg(charLeft));
        if (fountainOut && fdxOut) {
            const QString outF = tmp.filePath(QStringLiteral("out.fountain")), outX = tmp.filePath(QStringLiteral("out.fdx"));
            const bool w1 = fountainOut->exportManuscript(m, outF, &err);
            const bool w2 = fdxOut->exportManuscript(m, outX, &err);
            const QString ft = QString::fromUtf8(readAll(outF));
            const Screenplay compiled = fromFountain(ft);
            Screenplay fromX;
            const bool r2 = fromFdx(readAll(outX), &fromX, &err);
            const QString co = outline(compiled);
            check(w1 && co.contains(QLatin1String("scene:INT. LIGHTHOUSE - NIGHT|action:Rain against the glass."))
                      && co.contains(QLatin1String("|character:MARTA|parenthetical:(whispering)|dialogue:Who left this here?|"))
                      && co.contains(QLatin1String("|scene:THE QUAY|")) && !ft.contains(QLatin1String("**INT."))
                      && compiled.titleValue(QStringLiteral("Author")) == QLatin1String("Ada Marsh"),
                  QStringLiteral("compile to Fountain: the script comes out as a script"), ft);
            check(w2 && r2 && outline(fromX).contains(QLatin1String("scene:INT. LIGHTHOUSE - NIGHT|action:Rain against the glass."))
                      && outline(fromX).contains(QLatin1String("|character:TOM (O.S.)|dialogue:Marta? The boat is in.")),
                  QStringLiteral("compile to Final Draft: the script comes out as a script"), outline(fromX) + err);
        }

        // ---- RTF project
        check(p->docs()->convertStorage(StorageFormat::Rtf, &err), QStringLiteral("script project converted to RTF"), err);
        p->save(&err);
        p.reset();
        p = Project::open(folder, &err);
        const QString rtfEls = p ? elements(p->docs()->text(wholeId)) : QString();
        check(p && rtfEls.contains(QLatin1String("|Scene Heading:INT. LIGHTHOUSE - NIGHT|Action:Rain against"))
                  && rtfEls.contains(QLatin1String("|Character:MARTA|Parenthetical:(whispering)|Dialogue:Who left this here?|")),
              QStringLiteral("RTF project: the elements are kept"), rtfEls);
        if (p) {
            const bool ok4 = p->setScriptFormat(builtIn().at(1), {}, {}, &err);
            p->save(&err);
            const QByteArray sceneRtf = sceneId.isEmpty() ? QByteArray() : readAll(p->docs()->textPath(sceneId));
            check(ok4 && sceneRtf.contains("Script > Stage Direction") && !sceneRtf.contains("Script > Action;"),
                  QStringLiteral("RTF project: another script format rewrites the files of closed documents"), err);
        }
    }
    return failed;
}
