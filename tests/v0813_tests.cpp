// Scripted run for version 0.81.3 beta: Scrivener compatibility, part 2.
//  - styles: Scrivener's paragraph and character styles read (also styles the project has no style of that name
//    for), and written back as Scrivener keeps them: the marks "<$Scr_Ps::n>" and "<$Scr_Cs::n>" in the text, the
//    text's content.styles, a style made here added to Files/styles.xml; a .scriv saved without a change unchanged;
//  - inline footnotes and annotations written as Scrivener for Windows keeps them (text inside the RTF), and read back;
//  - web pages: Scrivener's .mht (a MIME archive) read and written; this program's pages (HTML with folders) go into
//    a .scriv as .mht; the page's address (OriginalURL); the view with its address and two buttons (the page in the
//    browser from a copy in the Downloads folder, the original address); a hostile archive stays in its folder;
//  - notes of pictures, PDFs, audio and video written into the .scriv;
//  - the repair of what earlier versions wrote for audio, video and web pages (text documents naming the file).
// QB_SCRIV_REAL=<folder.scriv> also opens and saves a copy of a real project (never changed itself).
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0813_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/WebArchive.h"
#include "import/FileImporter.h"
#include "import/ImporterApi.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/DisplayFormat.h"
#include "text/RtfIO.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/LinkOpen.h"
#include "ui/MediaView.h"

#include "AppInfo.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <cstdio>
#include <memory>

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

// every file of a folder with its bytes (to see that nothing changed)
static QMap<QString, QByteArray> filesOf(const QString &dir)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        out.insert(QDir(dir).relativeFilePath(f), readAll(f));
    }
    return out;
}

static QString differences(const QMap<QString, QByteArray> &a, const QMap<QString, QByteArray> &b)
{
    QStringList out;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        if (!b.contains(it.key()))
            out << QStringLiteral("gone: ") + it.key();
        else if (b.value(it.key()) != it.value()) {
            QString what = QStringLiteral("changed: ") + it.key();
            if (it.key().endsWith(QLatin1String(".checksum")) || it.key().endsWith(QLatin1String(".styles"))) {
                // the lines that differ
                const QList<QByteArray> x = it.value().split('\n'), y = b.value(it.key()).split('\n');
                QStringList gone, added;
                for (const QByteArray &l : x)
                    if (!y.contains(l))
                        gone << QString::fromUtf8(l);
                for (const QByteArray &l : y)
                    if (!x.contains(l))
                        added << QString::fromUtf8(l);
                what += QStringLiteral(" (-%1 +%2)").arg(gone.mid(0, 4).join(QLatin1Char(' ')), added.mid(0, 4).join(QLatin1Char(' ')));
            }
            out << what;
        }
    for (auto it = b.cbegin(); it != b.cend(); ++it)
        if (!a.contains(it.key()))
            out << QStringLiteral("new: ") + it.key();
    return out.join(QLatin1String(", "));
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

static QString metaText(const QDomElement &item, const QString &name)
{
    return item.firstChildElement(QStringLiteral("MetaData")).firstChildElement(name).text();
}

// The text of an RTF file as it is written, Scrivener's marks included
static QString markedText(const QString &rtfPath)
{
    QTextDocument d;
    rtf::read(readAll(rtfPath), &d, Formatting());
    return d.toPlainText();
}

static QStringList idsIn(const QString &stylesPath)
{
    return QString::fromUtf8(readAll(stylesPath)).split(QLatin1Char(','), Qt::SkipEmptyParts);
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

static QString noteText(const QTextDocument *doc, NoteKind kind, QColor *color = nullptr)
{
    QString out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (NoteKind(cf.intProperty(props::NoteKind)) != kind)
                continue;
            out += isInlineNote(kind) ? it.fragment().text() : cf.stringProperty(props::NoteText);
            if (color)
                *color = cf.property(props::NoteColor).value<QColor>();
        }
    return out;
}

static QString blockStyle(const QTextDocument *doc, int n)
{
    const QTextBlock b = doc->findBlockByNumber(n);
    return b.isValid() ? b.blockFormat().stringProperty(props::Style) : QStringLiteral("(no paragraph %1)").arg(n);
}

// The character style of the text `word` (the first place it is found), "" = none
static QString charStyleOf(const QTextDocument *doc, const QString &word)
{
    const QTextCursor c = doc->find(word);
    if (c.isNull())
        return QStringLiteral("(not found)");
    QTextCursor one(const_cast<QTextDocument *>(doc));
    one.setPosition(c.selectionStart() + 1);
    return one.charFormat().stringProperty(props::CharStyle);
}

// The pictures of a text: their sizes in pixels ("" when the picture's data is missing)
static QStringList pictures(QTextDocument *doc)
{
    QStringList out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (!cf.isImageFormat())
                continue;
            const QVariant v = doc->resource(QTextDocument::ImageResource, QUrl(cf.toImageFormat().name()));
            const QImage img = v.value<QImage>();
            out << (img.isNull() ? QString() : QStringLiteral("%1x%2").arg(img.width()).arg(img.height()));
        }
    return out;
}

static QByteArray pngBytes(const QColor &color, int w = 8, int h = 6)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

static QByteArray plainRtf(const QString &text)
{
    QTextDocument d;
    d.setPlainText(text);
    return rtf::write(&d, Formatting());
}

// ------------------------------------------------------------------------------------------------------------------
// A small project as Scrivener 3.1.6.0 for Windows writes one (the form of the elements; the content is made up)

static const char *kT1 = "A2000000-0000-4000-8000-000000000002";    // "Chapter One": paragraph and character styles
static const char *kT2 = "A2000000-0000-4000-8000-000000000003";    // "Plain": no styles, an inline footnote
static const char *kT3 = "A2000000-0000-4000-8000-000000000004";    // "Lamp": a picture in the text, as Scrivener for Windows keeps it
static const char *kPic = "A2000000-0000-4000-8000-000000000011";   // "Lighthouse": a picture with notes
static const char *kWeb = "A2000000-0000-4000-8000-000000000012";   // "Tide tables": a saved web page with a picture
static const char *kSH1 = "5A000000-0000-4000-8000-000000000001";   // Heading 1
static const char *kSBQ = "5A000000-0000-4000-8000-000000000002";   // Block Quote
static const char *kSEm = "5A000000-0000-4000-8000-000000000003";   // Emphasis (character)
static const char *kSCh = "5A000000-0000-4000-8000-000000000004";   // Sea Chant (no style of this name here)

static QByteArray styleFormat(const char *rtfBody)
{
    return QByteArray("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0{\\fonttbl{\\f0\\fnil\\fcharset0\\fprq2 Cambria;}}") + rtfBody + "}";
}

static QString makeStyledProject(const QString &dir)
{
    const QString scriv = dir + QLatin1String("/Sea.scriv");
    const QByteArray x = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<ScrivenerProject Template="No" Version="2.0" Identifier="8E3C7A1B-2D4F-4C9B-8A32-4B5C6D7E8F90" Creator="SCRWIN-3.1.6.0" Device="Desk" Author="A. Writer" Modified="2026-11-12 09:00:00 +0100" ModID="21111111-2222-3333-4444-555555555555">
    <Binder>
        <BinderItem UUID="A2000000-0000-4000-8000-000000000001" Type="DraftFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Manuscript</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
            <Children>
                <BinderItem UUID="A2000000-0000-4000-8000-000000000002" Type="Text" Created="2026-11-01 10:01:00 +0100" Modified="2026-11-02 10:01:00 +0100">
                    <Title>Chapter One</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                </BinderItem>
                <BinderItem UUID="A2000000-0000-4000-8000-000000000003" Type="Text" Created="2026-11-01 10:02:00 +0100" Modified="2026-11-02 10:02:00 +0100">
                    <Title>Plain</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                </BinderItem>
                <BinderItem UUID="A2000000-0000-4000-8000-000000000004" Type="Text" Created="2026-11-01 10:02:30 +0100" Modified="2026-11-02 10:02:30 +0100">
                    <Title>Lamp</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>rtf</FileExtension>
                    </MetaData>
                    <TextSettings>
                        <TextSelection>0,0</TextSelection>
                    </TextSettings>
                </BinderItem>
            </Children>
        </BinderItem>
        <BinderItem UUID="A2000000-0000-4000-8000-000000000010" Type="ResearchFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Research</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
            <Children>
                <BinderItem UUID="A2000000-0000-4000-8000-000000000011" Type="Image" Created="2026-11-01 10:03:00 +0100" Modified="2026-11-01 10:03:00 +0100">
                    <Title>Lighthouse</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>png</FileExtension>
                    </MetaData>
                </BinderItem>
                <BinderItem UUID="A2000000-0000-4000-8000-000000000012" Type="WebArchive" Created="2026-11-01 10:04:00 +0100" Modified="2026-11-01 10:04:00 +0100">
                    <Title>Tide tables</Title>
                    <MetaData>
                        <IncludeInCompile>Yes</IncludeInCompile>
                        <FileExtension>mht</FileExtension>
                    </MetaData>
                    <MediaSettings>
                        <OriginalURL>https://example.com/tides/today.html</OriginalURL>
                    </MediaSettings>
                </BinderItem>
            </Children>
        </BinderItem>
        <BinderItem UUID="A2000000-0000-4000-8000-000000000020" Type="TrashFolder" Created="2026-11-01 10:00:00 +0100" Modified="2026-11-01 10:00:00 +0100">
            <Title>Trash</Title>
            <MetaData>
                <IncludeInCompile>Yes</IncludeInCompile>
            </MetaData>
        </BinderItem>
    </Binder>
    <Collections>
        <Collection Type="Binder" ID="B2000000-0000-4000-8000-000000000001" Color="0.941176 0.937255 0.956863">
            <Title>Binder</Title>
        </Collection>
    </Collections>
    <LabelSettings>
        <Title>Label</Title>
        <DefaultLabelID>-1</DefaultLabelID>
        <Labels>
            <Label ID="-1">No Label</Label>
        </Labels>
    </LabelSettings>
    <StatusSettings>
        <Title>Status</Title>
        <DefaultStatusID>-1</DefaultStatusID>
        <StatusItems>
            <Status ID="-1">No Status</Status>
        </StatusItems>
    </StatusSettings>
    <ProjectTargets Notify="No">
        <DraftTarget Type="Words" CountIncludedOnly="Yes" CurrentCompileGroupOnly="No">0</DraftTarget>
    </ProjectTargets>
</ScrivenerProject>
)XML";
    writeAll(scriv + QLatin1String("/Sea.scrivx"), x);
    QByteArray styles = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Styles>\n";
    auto style = [&](const char *id, const char *name, const char *type, const char *change, const char *body) {
        styles += QByteArray("    <Style ID=\"") + id + "\" Name=\"" + name + "\" Type=\"" + type + "\"";
        if (*change)
            styles += QByteArray(" FontChange=\"") + change + "\"";
        styles += ">\n        <Format>" + styleFormat(body) + "</Format>\n    </Style>\n";
    };
    style(kSH1, "Heading 1", "Para+Char", "Face+Size", "\\pard\\plain \\sb240\\sa120 {\\f0\\fs36\\b Heading 1}");
    style(kSBQ, "Block Quote", "Para", "", "\\pard\\plain \\li720\\ri720 {\\f0\\fs24 Block Quote}");
    style(kSEm, "Emphasis", "Char", "", "\\pard\\plain {\\f0\\fs24\\i Emphasis}");
    style(kSCh, "Sea Chant", "Para+Char", "", "\\pard\\plain \\qc {\\f0\\fs24\\i Sea Chant}");
    styles += "</Styles>\n";
    writeAll(scriv + QLatin1String("/Files/styles.xml"), styles);
    const QString d = scriv + QLatin1String("/Files/Data/");
    writeAll(d + QLatin1String(kT1) + QLatin1String("/content.rtf"), R"RTF({\rtf1\ansi\ansicpg1252\uc1\deff0
{\fonttbl{\f0\fnil\fcharset0\fprq2 Cambria;}}
\paperw12240\paperh15840\margl1800\margr1800\margt1440\margb1440\f0\fs24\cf0
\pard\plain \sb240\sa120 {\f0\fs36\b <$Scr_Ps::0>Chapter One<!$Scr_Ps::0>}
\par\pard\plain \li720\ri720 {\f0\fs24 <$Scr_Ps::1>The sea was }{\f0\fs24\i <$Scr_Cs::2>grey<!$Scr_Cs::2>}{\f0\fs24  and wide.<!$Scr_Ps::1>}
\par\pard\plain \qc {\f0\fs24\i <$Scr_Ps::3>Heave and ho.<!$Scr_Ps::3>}
\par\pard\plain {\f0\fs24 Plain words.}
\par}
)RTF");
    writeAll(d + QLatin1String(kT1) + QLatin1String("/content.styles"), QByteArray(kSH1) + ',' + kSBQ + ',' + kSEm + ',' + kSCh);
    writeAll(d + QLatin1String(kT2) + QLatin1String("/content.rtf"), R"RTF({\rtf1\ansi\ansicpg1252\uc1\deff0
{\fonttbl{\f0\fnil\fcharset0\fprq2 Cambria;}}
\pard\plain {\f0\fs24 The lamp was lit at dusk.\{\\Scrv_fn=}{\f0\fs24 Every night.\\end_Scrv_fn\}}
\par}
)RTF");
    // a picture in the text as Scrivener for Windows keeps it: a \shppict group, a file name group inside the \pict
    writeAll(d + QLatin1String(kT3) + QLatin1String("/content.rtf"),
             QByteArray("{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl{\\f0\\fnil\\fcharset0\\fprq2 Cambria;}}\n"
                        "\\pard\\plain {\\f0\\fs24 The lamp:}\n\\par\\pard\\plain \\qc {\\f0\\fs24 }{\\*\\shppict{\\pict{\\*\\nisusfilename Scrivener}"
                        "\\picw40\\pich30\\picwgoal600\\pichgoal450\\pngblip ")
                 + pngBytes(QColor(250, 200, 0), 40, 30).toHex() + "}}\n\\par\\pard\\plain {\\f0\\fs24 Seen from the pier.}\n\\par}\n");
    writeAll(d + QLatin1String(kPic) + QLatin1String("/content.png"), pngBytes(QColor(20, 60, 160)));
    writeAll(d + QLatin1String(kPic) + QLatin1String("/notes.rtf"), plainRtf(QStringLiteral("Taken from the north pier.")));
    const QByteArray pic = pngBytes(QColor(200, 40, 40)).toBase64();
    writeAll(d + QLatin1String(kWeb) + QLatin1String("/content.mht"),
             "From: <Saved by Windows Internet Explorer 11>\r\nSubject: Tide tables\r\nMIME-Version: 1.0\r\n"
             "Content-Type: multipart/related;\r\n\ttype=\"text/html\";\r\n\tboundary=\"----=_NextPart_000_0000_01DB0000.00000000\"\r\n\r\n"
             "This is a multi-part message in MIME format.\r\n\r\n"
             "------=_NextPart_000_0000_01DB0000.00000000\r\nContent-Type: text/html;\r\n\tcharset=\"utf-8\"\r\n"
             "Content-Transfer-Encoding: quoted-printable\r\nContent-Location: https://example.com/tides/today.html\r\n\r\n"
             "<html><head><meta charset=3D\"utf-8\"><link rel=3D\"stylesheet\" href=3D\"css/site.css\"></head>"
             "<body><h1>Tide tables</h1><p>High water 06:12, low water 12:3=\r\n0.</p><img src=3D\"img/wave.png\"></body></html>\r\n\r\n"
             "------=_NextPart_000_0000_01DB0000.00000000\r\nContent-Type: image/png\r\nContent-Transfer-Encoding: base64\r\n"
             "Content-Location: https://example.com/tides/img/wave.png\r\n\r\n" + pic + "\r\n\r\n"
             "------=_NextPart_000_0000_01DB0000.00000000\r\nContent-Type: text/css\r\nContent-Transfer-Encoding: 8bit\r\n"
             "Content-Location: https://example.com/tides/css/site.css\r\n\r\nbody { background: url(../img/wave.png); }\r\n\r\n"
             "------=_NextPart_000_0000_01DB0000.00000000--\r\n");
    writeAll(scriv + QLatin1String("/Files/version.txt"), "23");
    return scriv;
}

// ------------------------------------------------------------------------------------------------------------------
// Styles and inline notes in a .scriv of Scrivener for Windows

static void styledProject(const QString &W)
{
    const QString L = QStringLiteral("styles: ");
    const QString scriv = makeStyledProject(W + QLatin1String("/sea"));
    const QString data = scriv + QLatin1String("/Files/Data/");
    const QString t1 = data + QLatin1String(kT1), t2 = data + QLatin1String(kT2);
    QString err;

    // --- read (File > Import)
    {
        ScrivenerImportReport rep;
        auto p = ScrivenerImporter::importProject(scriv, W + QLatin1String("/sea-import"), QStringLiteral("Sea"), StorageFormat::Rtf, &rep, &err);
        check(bool(p), L + QStringLiteral("imported"), err);
        if (!p)
            return;
        QTextDocument *doc = p->docs()->text(QLatin1String(kT1));
        check(doc && blockStyle(doc, 0) == QLatin1String("Heading 1") && blockStyle(doc, 1) == QLatin1String("Block Quote")
                  && blockStyle(doc, 2) == QLatin1String("Sea Chant") && blockStyle(doc, 3).isEmpty(),
              L + QStringLiteral("paragraphs keep their styles, also one the project had no style for"),
              doc ? QStringList({blockStyle(doc, 0), blockStyle(doc, 1), blockStyle(doc, 2), blockStyle(doc, 3)}).join(QLatin1String(" | ")) : QString());
        check(doc && charStyleOf(doc, QStringLiteral("grey")) == QLatin1String("Emphasis") && charStyleOf(doc, QStringLiteral("wide")).isEmpty(),
              L + QStringLiteral("the character style of a word"), doc ? charStyleOf(doc, QStringLiteral("grey")) : QString());
        check(doc && !doc->toPlainText().contains(QLatin1String("Scr_"))
                  && doc->toPlainText() == QLatin1String("Chapter One\nThe sea was grey and wide.\nHeave and ho.\nPlain words."),
              L + QStringLiteral("no marks left in the text"), doc ? doc->toPlainText() : QString());
        QTextDocument *lamp = p->docs()->text(QLatin1String(kT3));
        check(lamp && pictures(lamp) == QStringList{QStringLiteral("40x30")} && lamp->toPlainText().contains(QLatin1String("Seen from the pier.")),
              L + QStringLiteral("a picture in the text as Scrivener for Windows keeps it (a file name inside the picture's group)"),
              lamp ? pictures(lamp).join(QLatin1Char(',')) : QString());
        const Style *chant = styles::find(p->formatting, QStringLiteral("Sea Chant"));
        check(chant && !chant->character && chant->italic, L + QStringLiteral("Scrivener's style \"Sea Chant\" became a paragraph style of the project, italic as its sample"));
        const Style *emph = styles::findChar(p->formatting, QStringLiteral("Emphasis"));
        check(emph && emph->character, L + QStringLiteral("\"Emphasis\" is the project's character style of that name"));
        QTextDocument *plain = p->docs()->text(QLatin1String(kT2));
        check(plain && countKind(plain, NoteKind::InlineFootnote) == 1 && noteText(plain, NoteKind::InlineFootnote) == QLatin1String("Every night."),
              L + QStringLiteral("the inline footnote of the other text"), plain ? plain->toPlainText() : QString());
        const BinderItem *pic = p->item(QLatin1String(kPic));
        check(pic && pic->type == ItemType::Image && p->docs()->notes(QLatin1String(kPic))
                  && p->docs()->notes(QLatin1String(kPic))->toPlainText() == QLatin1String("Taken from the north pier."),
              L + QStringLiteral("the picture's notes"));
        const BinderItem *web = p->item(QLatin1String(kWeb));
        check(web && web->type == ItemType::WebPage && web->fileExt == QLatin1String("mht")
                  && web->sourceUrl == QLatin1String("https://example.com/tides/today.html"),
              L + QStringLiteral("the saved web page with its original address"), web ? web->sourceUrl : QString());
    }

    // --- in Scrivener format: opened and saved without a change
    const QMap<QString, QByteArray> before = filesOf(scriv + QLatin1String("/Files"));
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("opened in Scrivener format"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    check(q->save(&err), L + QStringLiteral("saved without a change"), err);
    {
        QMap<QString, QByteArray> after = filesOf(scriv + QLatin1String("/Files"));
        after.remove(QStringLiteral("search.indexes"));   // not there before; made with the first change
        check(differences(before, after).isEmpty(), L + QStringLiteral("texts, their style lists, styles.xml, the picture and the web page as they were"),
              differences(before, after));
    }

    // --- a styled text changed: the marks written as Scrivener keeps them
    {
        QTextDocument *doc = q->docs()->text(QLatin1String(kT1));
        std::unique_ptr<QTextDocument> c(doc->clone());
        QTextCursor cur(c.get());
        cur.movePosition(QTextCursor::End);
        cur.insertBlock();
        cur.insertText(QStringLiteral("Gulls over the water."));
        cur.insertBlock();
        cur.insertText(QStringLiteral("Terns too."));
        cur.movePosition(QTextCursor::PreviousBlock, QTextCursor::KeepAnchor);
        display::applyParagraphStyle(cur, QStringLiteral("Block Quote"), q->formatting);
        const int at = c->find(QStringLiteral("Gulls")).selectionStart();
        styles::applyCharacterStyle(c.get(), at, at + 5, QStringLiteral("Emphasis"), q->formatting);
        q->docs()->setContent(QLatin1String(kT1), c.get());
    }
    {
        QTextCursor c(q->docs()->text(QLatin1String(kT3)));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" At night."));
    }
    check(q->save(&err), L + QStringLiteral("saved after a change"), err);
    {
        const QByteArray rtf = readAll(data + QLatin1String(kT3) + QLatin1String("/content.rtf"));
        QTextDocument back;
        rtf::read(rtf, &back, Formatting());
        check(rtf.contains("\\pict") && pictures(&back) == QStringList{QStringLiteral("40x30")}
                  && back.toPlainText().contains(QLatin1String("Seen from the pier. At night.")),
              L + QStringLiteral("a text with a picture written anew keeps its picture"), QString::number(rtf.size()));
    }
    {
        const QStringList ids = idsIn(t1 + QLatin1String("/content.styles"));
        const QString text = markedText(t1 + QLatin1String("/content.rtf"));
        auto n = [&](const char *id) { return QString::number(ids.indexOf(QLatin1String(id))); };
        // as Scrivener for Windows writes them: one pair around paragraphs in a row with the same style; the opening mark
        // after the closing mark of the paragraph before when that one has a style, else at the paragraph's start
        const QString want = QStringLiteral("<$Scr_Ps::H>Chapter One<!$Scr_Ps::H><$Scr_Ps::Q>\nThe sea was <$Scr_Cs::E>grey<!$Scr_Cs::E> and "
                                            "wide.<!$Scr_Ps::Q><$Scr_Ps::C>\nHeave and ho.<!$Scr_Ps::C>\nPlain words.\n<$Scr_Ps::Q><$Scr_Cs::E>Gulls"
                                            "<!$Scr_Cs::E> over the water.\nTerns too.<!$Scr_Ps::Q>")
                                 .replace(QLatin1String("::H>"), QLatin1String("::") + n(kSH1) + QLatin1Char('>'))
                                 .replace(QLatin1String("::Q>"), QLatin1String("::") + n(kSBQ) + QLatin1Char('>'))
                                 .replace(QLatin1String("::E>"), QLatin1String("::") + n(kSEm) + QLatin1Char('>'))
                                 .replace(QLatin1String("::C>"), QLatin1String("::") + n(kSCh) + QLatin1Char('>'));
        check(ids.size() == 4 && QSet<QString>(ids.begin(), ids.end()) == QSet<QString>({QLatin1String(kSH1), QLatin1String(kSBQ), QLatin1String(kSEm), QLatin1String(kSCh)}),
              L + QStringLiteral("content.styles: the IDs of styles.xml the text uses, comma-separated"), ids.join(QLatin1Char(',')));
        check(text == want, L + QStringLiteral("paragraph and character style marks around the paragraphs and the words, placed as Scrivener places them"),
              text + QLatin1String("\n        wanted:\n") + want);
        check(readAll(scriv + QLatin1String("/Files/styles.xml")) == before.value(QStringLiteral("styles.xml")),
              L + QStringLiteral("styles.xml unchanged: every style the text uses is in it"));
        QTextDocument raw;
        rtf::read(readAll(t1 + QLatin1String("/content.rtf")), &raw, Formatting());
        const QTextCursor g = raw.find(QStringLiteral("Gulls"));
        QTextCursor one(&raw);
        one.setPosition(g.selectionStart() + 1);
        check(!g.isNull() && one.charFormat().fontItalic(), L + QStringLiteral("the styled word keeps its look in the RTF (italic)"));
    }
    // read back as Scrivener (or a new import) would: the styles of the paragraphs and words again
    {
        ScrivenerImportReport rep;
        auto back = ScrivenerImporter::importProject(scriv, W + QLatin1String("/sea-back"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
        QTextDocument *doc = back ? back->docs()->text(QLatin1String(kT1)) : nullptr;
        check(doc && blockStyle(doc, 0) == QLatin1String("Heading 1") && blockStyle(doc, 1) == QLatin1String("Block Quote")
                  && blockStyle(doc, 2) == QLatin1String("Sea Chant") && blockStyle(doc, 3).isEmpty() && blockStyle(doc, 4) == QLatin1String("Block Quote")
                  && blockStyle(doc, 5) == QLatin1String("Block Quote")
                  && charStyleOf(doc, QStringLiteral("Gulls")) == QLatin1String("Emphasis") && charStyleOf(doc, QStringLiteral("grey")) == QLatin1String("Emphasis")
                  && !doc->toPlainText().contains(QLatin1String("Scr_")),
              L + QStringLiteral("read back: the same styles"), doc ? doc->toPlainText() : QString());
    }

    // --- a style made here: added to styles.xml once
    {
        StyleSheet sheet = styles::sheet(q->formatting);
        Style log;
        log.name = QStringLiteral("Ship Log");
        log.italic = true;
        log.chars.family = QStringLiteral("DejaVu Serif");
        sheet.styles.append(log);
        q->formatting = styles::withSheet(q->formatting, sheet);
        q->markIndexDirty();
        QTextDocument *doc = q->docs()->text(QLatin1String(kT2));
        std::unique_ptr<QTextDocument> c(doc->clone());
        QTextCursor cur(c.get());
        cur.movePosition(QTextCursor::End);
        cur.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cur.insertText(QStringLiteral("Wind from the west."), QTextCharFormat());
        display::applyParagraphStyle(cur, QStringLiteral("Ship Log"), q->formatting);
        q->docs()->setContent(QLatin1String(kT2), c.get());
    }
    check(q->save(&err), L + QStringLiteral("saved with a style made here"), err);
    QString logId;
    {
        QDomDocument sx;
        sx.setContent(readAll(scriv + QLatin1String("/Files/styles.xml")));
        int n = 0;
        QDomElement e;
        for (QDomElement s = sx.documentElement().firstChildElement(QStringLiteral("Style")); !s.isNull(); s = s.nextSiblingElement(QStringLiteral("Style")), ++n)
            if (s.attribute(QStringLiteral("Name")) == QLatin1String("Ship Log"))
                e = s;
        logId = e.attribute(QStringLiteral("ID"));
        check(n == 5 && !e.isNull() && e.attribute(QStringLiteral("Type")) == QLatin1String("Para+Char")
                  && e.attribute(QStringLiteral("FontChange")) == QLatin1String("Face") && QUuid::fromString(logId).isNull() == false
                  && logId == logId.toUpper() && e.firstChildElement(QStringLiteral("Format")).text().startsWith(QLatin1String("{\\rtf1")),
              L + QStringLiteral("styles.xml: the new style with an ID, its kind, the font it changes and its look as RTF"),
              QString::fromUtf8(readAll(scriv + QLatin1String("/Files/styles.xml"))).right(900));
        const QStringList ids = idsIn(t2 + QLatin1String("/content.styles"));
        const QString text = markedText(t2 + QLatin1String("/content.rtf"));
        check(ids == QStringList{logId} && text.contains(QLatin1String("<$Scr_Ps::0>Wind from the west.<!$Scr_Ps::0>")),
              L + QStringLiteral("the text that uses it: its marks and content.styles"), ids.join(QLatin1Char(',')) + QLatin1String(" / ") + text);
        check(text.contains(QLatin1String("{\\Scrv_fn=Every night.\\end_Scrv_fn}")) && !QFile::exists(t2 + QLatin1String("/content.comments")),
              L + QStringLiteral("the inline footnote of that text is written as Scrivener for Windows keeps it, not as a linked note"), text);
    }
    {
        const QMap<QString, QByteArray> once = filesOf(scriv + QLatin1String("/Files"));
        q->markIndexDirty();
        check(q->save(&err), L + QStringLiteral("saved again"), err);
        QMap<QString, QByteArray> again = filesOf(scriv + QLatin1String("/Files"));
        check(differences(once, again).isEmpty(), L + QStringLiteral("saved again: styles.xml and the texts stay as they are (the style is not added twice)"),
              differences(once, again));
    }
    {
        ScrivenerImportReport rep;
        auto back = ScrivenerImporter::importProject(scriv, W + QLatin1String("/sea-back2"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
        const Style *log = back ? styles::find(back->formatting, QStringLiteral("Ship Log")) : nullptr;
        QTextDocument *doc = back ? back->docs()->text(QLatin1String(kT2)) : nullptr;
        check(log && log->italic && !log->character && doc && blockStyle(doc, 1) == QLatin1String("Ship Log")
                  && countKind(doc, NoteKind::InlineFootnote) == 1,
              L + QStringLiteral("read back: the new style with its look, on its paragraph; the footnote"));
    }

    // --- the style taken off again: content.styles goes
    {
        QTextDocument *doc = q->docs()->text(QLatin1String(kT2));
        std::unique_ptr<QTextDocument> c(doc->clone());
        QTextCursor cur(c->findBlockByNumber(1));
        display::applyParagraphStyle(cur, styles::noStyle(), q->formatting);
        q->docs()->setContent(QLatin1String(kT2), c.get());
    }
    check(q->save(&err), L + QStringLiteral("saved with the style taken off"), err);
    check(!QFile::exists(t2 + QLatin1String("/content.styles")) && !markedText(t2 + QLatin1String("/content.rtf")).contains(QLatin1String("Scr_Ps")),
          L + QStringLiteral("a text that uses no style any more has no content.styles and no marks"));
}

// ------------------------------------------------------------------------------------------------------------------
// Web archives (the MIME form of a saved page)

static void webArchives(const QString &W)
{
    const QString L = QStringLiteral("web archive: ");
    // a page of this program: HTML with its pictures and style sheet in folders beside it
    const QString page = W + QLatin1String("/page/content.html");
    writeAll(page, "<html><head><link rel=\"stylesheet\" href=\"page-files/style.css\"></head><body><p>Harbor news</p>"
                   "<img src=\"page-files/boat.png\" alt=\"boat\"></body></html>");
    const QByteArray boat = pngBytes(QColor(10, 120, 40));
    writeAll(W + QLatin1String("/page/page-files/boat.png"), boat);
    writeAll(W + QLatin1String("/page/page-files/style.css"), "p { background-image: url('boat.png'); }");
    writeAll(W + QLatin1String("/page/notes.txt"), "not part of the page");   // files beside the page are not parts
    const QByteArray mht = webarchive::pack(page, QStringLiteral("https://example.org/news/harbor.html"));
    check(mht.contains("Content-Type: multipart/related") && mht.contains("Content-Location: https://example.org/news/harbor.html")
              && mht.contains("Content-Location: https://example.org/news/page-files/boat.png")
              && mht.contains("Content-Location: https://example.org/news/page-files/style.css") && !mht.contains("not part of the page")
              && !mht.contains("notes.txt"),
          L + QStringLiteral("packed: the page and the files in the folders beside it, each with its address"));
    check(webarchive::addressOf(mht) == QLatin1String("https://example.org/news/harbor.html"), L + QStringLiteral("the page's address"));
    const QString out = W + QLatin1String("/unpacked");
    QString shown, url, err;
    check(webarchive::unpack(mht, out, &shown, &url, &err) && shown == out + QLatin1String("/index.html")
              && url == QLatin1String("https://example.org/news/harbor.html"),
          L + QStringLiteral("unpacked"), err);
    {
        const QString html = QString::fromUtf8(readAll(shown));
        const QRegularExpression img(QStringLiteral("<img src=\"([^\"]+)\""));
        const QString file = img.match(html).captured(1);
        check(html.contains(QLatin1String("Harbor news")) && !file.isEmpty() && !file.contains(QLatin1Char('/'))
                  && readAll(out + QLatin1Char('/') + file) == boat,
              L + QStringLiteral("the page points to its picture beside it, the picture's bytes as they were"), html);
        QString css;
        for (const QFileInfo &fi : QDir(out).entryInfoList({QStringLiteral("*.css")}, QDir::Files))
            css = QString::fromUtf8(readAll(fi.absoluteFilePath()));
        check(css.contains(QStringLiteral("url(\"%1\")").arg(file)), L + QStringLiteral("the style sheet's own picture link points to the file too"), css);
    }
    // as Internet Explorer and Scrivener for Windows write it: folded headers, quoted-printable, a picture by Content-ID
    {
        const QByteArray ie = "MIME-Version: 1.0\r\nContent-Type: multipart/related;\r\n\tboundary=\"XX\";\r\n\ttype=\"text/html\"\r\n\r\n"
                              "--XX\r\nContent-Type: text/html;\r\n\tcharset=\"windows-1252\"\r\nContent-Transfer-Encoding: quoted-printable\r\n"
                              "Content-Location: http://example.net/a.html\r\n\r\n"
                              "<html><head><meta http-equiv=3D\"Content-Type\" content=3D\"text/html; charset=3Dwindows-1252\"></head>"
                              "<body><p>Caf=E9 by the qu=\r\nay</p><img src=3D\"cid:pic1@example\"></body></html>\r\n"
                              "--XX\r\nContent-Type: image/png\r\nContent-Transfer-Encoding: base64\r\nContent-ID: <pic1@example>\r\n\r\n"
                              + boat.toBase64() + "\r\n--XX--\r\n";
        const QString o2 = W + QLatin1String("/unpacked-ie");
        QString p2;
        const bool ok = webarchive::unpack(ie, o2, &p2, nullptr, &err);
        const QString html = QString::fromUtf8(readAll(p2));
        const QRegularExpression img(QStringLiteral("<img src=\"([^\"]+)\""));
        const QString file = img.match(html).captured(1);
        check(ok && html.contains(QStringLiteral("Café by the quay")) && html.contains(QLatin1String("charset=utf-8")) && !file.startsWith(QLatin1String("cid:"))
                  && readAll(o2 + QLatin1Char('/') + file) == boat,
              L + QStringLiteral("folded headers, quoted-printable in windows-1252, a picture named by its Content-ID"), html);
    }
    // a hostile archive: names that climb out of the folder, a program
    {
        const QByteArray bad = "Content-Type: multipart/related; boundary=\"B\"\r\n\r\n"
                               "--B\r\nContent-Type: text/html\r\nContent-Location: ../../../index.html\r\n\r\n<p>x</p><a href=\"../../evil.sh\">e</a>\r\n"
                               "--B\r\nContent-Type: application/x-shellscript\r\nContent-Location: ../../evil.sh\r\n\r\n#!/bin/sh\necho hi\r\n"
                               "--B\r\nContent-Type: application/octet-stream\r\nContent-Location: /etc/x.desktop\r\n\r\n[Desktop Entry]\r\n"
                               "--B--\r\n";
        const QString o3 = W + QLatin1String("/hostile/a/b/out");
        QString p3;
        const bool ok = webarchive::unpack(bad, o3, &p3, nullptr, &err);
        QStringList outside, names;
        QDirIterator it(W + QLatin1String("/hostile"), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (!f.startsWith(o3 + QLatin1Char('/')))
                outside << f;
            names << QFileInfo(f).fileName();
        }
        bool noProgram = true;
        for (const QString &n : std::as_const(names))
            if (n.endsWith(QLatin1String(".sh")) || n.endsWith(QLatin1String(".desktop")))
                noProgram = false;
        check(ok && outside.isEmpty() && noProgram && names.size() == 3, L + QStringLiteral("a hostile archive: every file inside its folder, no program's name"),
              names.join(QLatin1String(", ")) + QLatin1String(" / outside: ") + outside.join(QLatin1String(", ")));
    }
    check(!webarchive::unpack(QByteArray("Content-Type: multipart/related; boundary=\"Q\"\r\n\r\n--Q\r\nContent-Type: image/png\r\n\r\nxx\r\n--Q--\r\n"),
                              W + QLatin1String("/nopage"), &shown, nullptr, &err)
              && !err.isEmpty(),
          L + QStringLiteral("an archive without a page is refused with a reason"), err);
}

// ------------------------------------------------------------------------------------------------------------------
// The page view: the page, its address, Open Saved Page in Browser, Open Original Address

static void webView(Project *p, BinderItem *it, const QString &L, const QString &expectUrl, const QString &pageWord, const QString &shot)
{
    QList<QUrl> opened;
    links::setOpenerForTests([&](const QUrl &u) { opened << u; });
    MediaView v;
    v.resize(820, 520);
    v.setProject(p);
    v.showItem(it);
    v.show();
    QTest::qWait(150);
    auto *address = v.findChild<QLabel *>(QStringLiteral("webAddress"));
    auto *saved = v.findChild<QPushButton *>(QStringLiteral("webOpenSaved"));
    auto *original = v.findChild<QPushButton *>(QStringLiteral("webOpenOriginal"));
    QTextBrowser *browser = nullptr;
    for (QTextBrowser *b : v.findChildren<QTextBrowser *>())
        if (b->isVisible())
            browser = b;
    check(address && saved && original && browser && browser->toPlainText().contains(pageWord) && !browser->toPlainText().contains(QLatin1String("Content-Type")),
          L + QStringLiteral("the page is shown (a saved .mht unpacked first)"), browser ? browser->toPlainText().left(300) : QString());
    if (!address || !saved || !original)
        return;
    v.grab().save(outDir + QLatin1Char('/') + shot);
    if (expectUrl.isEmpty()) {
        check(address->text() == QLatin1String("No address") && !original->isEnabled() && saved->isEnabled(),
              L + QStringLiteral("no address: \"No address\", Open Original Address off"));
    } else {
        check(address->text() == expectUrl && original->isEnabled() && saved->isEnabled(), L + QStringLiteral("the address and both buttons"),
              address->text());
        opened.clear();
        QTest::mouseClick(original, Qt::LeftButton);
        check(opened.size() == 1 && opened.first().toString() == expectUrl, L + QStringLiteral("Open Original Address opens the address in the browser"),
              opened.isEmpty() ? QString() : opened.first().toString());
    }
    opened.clear();
    QTest::mouseClick(saved, Qt::LeftButton);
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QLatin1String("/Quillbinder web pages/");
    const QString file = opened.isEmpty() ? QString() : opened.first().toLocalFile();
    const QString html = QString::fromUtf8(readAll(file));
    check(opened.size() == 1 && file.startsWith(downloads) && file.contains(it->title) && html.contains(pageWord),
          L + QStringLiteral("Open Saved Page in Browser opens a copy in the Downloads folder (a browser in a sandbox can read it)"),
          file + QLatin1String(" / ") + html.left(200));
    const QRegularExpression img(QStringLiteral("<img src=\"([^\"]+)\""));
    const QString pic = img.match(html).captured(1);
    check(!pic.isEmpty() && QFileInfo(QFileInfo(file).dir().filePath(pic)).size() > 0, L + QStringLiteral("the copy has its picture"), pic);
    auto *note = v.findChild<QLabel *>(QStringLiteral("webNote"));
    check(note && note->isVisible() && note->text().contains(QLatin1String("Opened in your browser")), L + QStringLiteral("the view says where the copy is"),
          note ? note->text() : QString());
    // a second time: the copy is made anew, not beside the old one
    QTest::mouseClick(saved, Qt::LeftButton);
    check(QDir(downloads).entryList(QDir::Dirs | QDir::NoDotAndDotDot).filter(it->title).size() == 1, L + QStringLiteral("opened again: one copy"));
    links::setOpenerForTests(nullptr);
}

// ------------------------------------------------------------------------------------------------------------------
// A project of this program written as a Scrivener project: inline notes, notes of pictures, PDFs, audio and video,
// a web page saved here

static void ownProject(const QString &W)
{
    const QString L = QStringLiteral("own project: ");
    QString err;
    auto p = Project::create(W + QLatin1String("/own"), QStringLiteral("Lamp"), &err);
    check(bool(p), L + QStringLiteral("made"), err);
    if (!p)
        return;
    BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Night"));
    const QColor red(180, 20, 40);
    {
        QTextDocument d;
        QTextCursor c(&d);
        c.insertText(QStringLiteral("The lamp turned"));
        c.insertText(QStringLiteral("Twelve times a minute."), text::noteFormat(NoteKind::InlineFootnote, newId()));
        c.insertText(QStringLiteral(" all night. "), QTextCharFormat());
        c.insertText(QStringLiteral("Check the keeper's log."), text::noteFormat(NoteKind::InlineAnnotation, newId(), QString(), red));
        c.insertText(QStringLiteral(" Ships "), QTextCharFormat());
        c.insertText(QStringLiteral("passed"), text::noteFormat(NoteKind::Comment, newId(), QStringLiteral("How many?")));
        c.insertText(QStringLiteral(" in the dark {with braces} and a \\ backslash."), QTextCharFormat());
        p->docs()->setContent(ch->id, &d);
    }
    // a picture, a PDF and an audio file with notes
    BinderItem *pic = p->model()->createItem(p->research(), -1, ItemType::Image, QStringLiteral("Lamp room"));
    pic->fileExt = QStringLiteral("png");
    writeAll(p->mediaFilePath(pic), pngBytes(QColor(240, 200, 40)));
    BinderItem *snd = p->model()->createItem(p->research(), -1, ItemType::Media, QStringLiteral("Foghorn"));
    snd->fileExt = QStringLiteral("wav");
    writeAll(p->mediaFilePath(snd), QByteArray("RIFF\x24\x00\x00\x00WAVEfmt ", 16) + QByteArray(28, '\0'));
    for (BinderItem *it : {pic, snd}) {
        QTextDocument n;
        n.setPlainText(QStringLiteral("Notes on ") + it->title + QLatin1Char('.'));
        p->docs()->setNotesContent(it->id, &n);
    }
    // a web page saved here (HTML with its picture in a folder beside it), as File > Import > Web Page leaves it
    const QString tmpPage = W + QLatin1String("/fetched/page.html");
    writeAll(tmpPage, "<html><body><h1>Lighthouse hours</h1><p>Open on Sundays.</p><img src=\"page-files/tower.png\"></body></html>");
    writeAll(W + QLatin1String("/fetched/page-files/tower.png"), pngBytes(QColor(90, 90, 200)));
    importers::WebPage wp;
    wp.url = QStringLiteral("https://example.org/lighthouse/hours");
    wp.title = QStringLiteral("Lighthouse hours");
    wp.asWebPage = true;
    wp.htmlFile = tmpPage;
    BinderItem *web = importer::addWebPage(p.get(), wp, p->research(), -1, nullptr);
    check(web && web->sourceUrl == wp.url && web->fileExt == QLatin1String("html"), L + QStringLiteral("a web page saved here keeps its address"));
    if (!web)
        return;
    webView(p.get(), web, QStringLiteral("view of a page saved here: "), wp.url, QStringLiteral("Open on Sundays."), QStringLiteral("v0813-own-page.png"));
    p->markIndexDirty();
    check(p->save(&err), L + QStringLiteral("saved"), err);
    {
        // the address is kept with the project
        auto again = Project::open(p->indexPath(), &err);
        check(again && again->item(web->id) && again->item(web->id)->sourceUrl == wp.url, L + QStringLiteral("the address is kept in the project file"), err);
    }

    const QString scriv = W + QLatin1String("/own/Lamp.scriv");
    auto wc = scrivener::convertToScrivener(p.get(), scriv, &err);
    check(bool(wc), L + QStringLiteral("switched to Scrivener format"), err);
    if (!wc)
        return;
    const QString data = scriv + QLatin1String("/Files/Data/");
    const QDomDocument x = scrivxDom(scriv);
    // the ids in the .scriv: the item's UUID
    auto uuidOf = [&](const QString &title) {
        const QDomNodeList list = x.elementsByTagName(QStringLiteral("BinderItem"));
        for (int i = 0; i < list.size(); ++i)
            if (list.at(i).toElement().firstChildElement(QStringLiteral("Title")).text() == title)
                return list.at(i).toElement().attribute(QStringLiteral("UUID"));
        return QString();
    };
    {
        const QString u = uuidOf(QStringLiteral("Night"));
        const QByteArray raw = readAll(data + u + QLatin1String("/content.rtf"));
        const QString text = markedText(data + u + QLatin1String("/content.rtf"));
        check(text.contains(QLatin1String("The lamp turned{\\Scrv_fn=Twelve times a minute.\\end_Scrv_fn} all night."))
                  && raw.contains("\\{\\\\Scrv_fn=") && raw.contains("\\\\end_Scrv_fn\\}"),
              L + QStringLiteral("an inline footnote as Scrivener for Windows keeps it (escaped braces and backslashes in the RTF)"), text);
        const QRegularExpression annot(QStringLiteral("\\{\\\\Scrv_annot \\\\color=\\{\\\\R=([0-9.]+)\\\\G=([0-9.]+)\\\\B=([0-9.]+)\\} \\\\text=Check the keeper's log\\.\\\\end_Scrv_annot\\}"));
        const QRegularExpressionMatch m = annot.match(text);
        check(m.hasMatch() && qAbs(m.captured(1).toDouble() - red.redF()) < 0.001 && qAbs(m.captured(2).toDouble() - red.greenF()) < 0.001
                  && qAbs(m.captured(3).toDouble() - red.blueF()) < 0.001,
              L + QStringLiteral("an inline annotation with its color"), text);
        check(text.contains(QLatin1String("in the dark {with braces} and a \\ backslash.")),
              L + QStringLiteral("braces and backslashes of the text itself stay text"), text);
        const QByteArray cm = readAll(data + u + QLatin1String("/content.comments"));
        check(cm.count("<Comment ") == 1 && cm.contains("How many?") && raw.contains("scrivcmt://"),
              L + QStringLiteral("the linked comment as before (content.comments), the inline notes not among them"));
        check(!QFile::exists(data + u + QLatin1String("/content.styles")), L + QStringLiteral("a text without styles has no content.styles"));
    }
    {
        const QDomElement pe = itemEl(x, uuidOf(QStringLiteral("Lamp room"))), se = itemEl(x, uuidOf(QStringLiteral("Foghorn")));
        check(markedText(data + pe.attribute(QStringLiteral("UUID")) + QLatin1String("/notes.rtf")) == QLatin1String("Notes on Lamp room.")
                  && markedText(data + se.attribute(QStringLiteral("UUID")) + QLatin1String("/notes.rtf")) == QLatin1String("Notes on Foghorn."),
              L + QStringLiteral("notes of a picture and of an audio file written into the .scriv"));
        check(se.attribute(QStringLiteral("Type")) == QLatin1String("Media") && metaText(se, QStringLiteral("FileExtension")) == QLatin1String("wav")
                  && !QFile::exists(data + se.attribute(QStringLiteral("UUID")) + QLatin1String("/content.rtf")),
              L + QStringLiteral("the audio file as Scrivener's audio document, no stand-in text"));
    }
    {
        const QDomElement we = itemEl(x, uuidOf(QStringLiteral("Lighthouse hours")));
        const QString dir = data + we.attribute(QStringLiteral("UUID"));
        check(we.attribute(QStringLiteral("Type")) == QLatin1String("WebArchive") && metaText(we, QStringLiteral("FileExtension")) == QLatin1String("mht")
                  && we.firstChildElement(QStringLiteral("MediaSettings")).firstChildElement(QStringLiteral("OriginalURL")).text() == wp.url
                  && we.firstChildElement(QStringLiteral("TextSettings")).isNull(),
              L + QStringLiteral("the web page as Scrivener's web page: Type WebArchive, .mht, its address"));
        const QByteArray mht = readAll(dir + QLatin1String("/content.mht"));
        QString shown, url, err2;
        const bool ok = webarchive::unpack(mht, W + QLatin1String("/own-unpacked"), &shown, &url, &err2);
        check(ok && url == wp.url && readAll(shown).contains("Open on Sundays.") && mht.contains("Content-Type: image/png")
                  && !QFile::exists(dir + QLatin1String("/content.html")) && !QDir(dir + QLatin1String("/page-files")).exists()
                  && !QFile::exists(dir + QLatin1String("/content.rtf")),
              L + QStringLiteral("content.mht holds the page and its picture; no HTML file, folder or stand-in text beside it"), err2);
    }
    // saved again without a change: nothing is written
    {
        const QMap<QString, QByteArray> once = filesOf(scriv + QLatin1String("/Files"));
        check(wc->save(&err), L + QStringLiteral("saved again"), err);
        check(differences(once, filesOf(scriv + QLatin1String("/Files"))).isEmpty(), L + QStringLiteral("saved again: nothing changes"),
              differences(once, filesOf(scriv + QLatin1String("/Files"))));
    }
    // read back as Scrivener (or a new import) would
    {
        ScrivenerImportReport rep;
        auto back = ScrivenerImporter::importProject(scriv, W + QLatin1String("/own-back"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
        check(bool(back), L + QStringLiteral("read back"), err);
        if (!back)
            return;
        BinderItem *bn = nullptr, *bw = nullptr, *bp = nullptr, *bs = nullptr;
        for (BinderItem *it : back->allItems()) {
            if (it->title == QLatin1String("Night"))
                bn = it;
            else if (it->title == QLatin1String("Lighthouse hours"))
                bw = it;
            else if (it->title == QLatin1String("Lamp room"))
                bp = it;
            else if (it->title == QLatin1String("Foghorn"))
                bs = it;
        }
        QTextDocument *t = bn ? back->docs()->text(bn->id) : nullptr;
        QColor c;
        const QString annot = t ? noteText(t, NoteKind::InlineAnnotation, &c) : QString();
        check(t && countKind(t, NoteKind::InlineFootnote) == 1 && noteText(t, NoteKind::InlineFootnote) == QLatin1String("Twelve times a minute.")
                  && annot == QLatin1String("Check the keeper's log.") && qAbs(c.redF() - red.redF()) < 0.002 && qAbs(c.blueF() - red.blueF()) < 0.002
                  && countKind(t, NoteKind::Comment) == 1 && noteText(t, NoteKind::Comment) == QLatin1String("How many?")
                  && t->toPlainText() == QLatin1String("The lamp turnedTwelve times a minute. all night. Check the keeper's log. Ships passed in the dark {with braces} and a \\ backslash."),
              L + QStringLiteral("read back: the footnote, the annotation with its color, the comment, the text"), t ? t->toPlainText() : QString());
        check(bp && bs && back->docs()->notes(bp->id)->toPlainText() == QLatin1String("Notes on Lamp room.")
                  && back->docs()->notes(bs->id)->toPlainText() == QLatin1String("Notes on Foghorn.") && bs->type == ItemType::Media,
              L + QStringLiteral("read back: the notes of the picture and the audio file"));
        check(bw && bw->type == ItemType::WebPage && bw->fileExt == QLatin1String("mht") && bw->sourceUrl == wp.url,
              L + QStringLiteral("read back: the web page (.mht) with its address"));
        if (bw)
            webView(back.get(), bw, QStringLiteral("view of a saved .mht: "), wp.url, QStringLiteral("Open on Sundays."), QStringLiteral("v0813-mht-page.png"));
    }
}

// ------------------------------------------------------------------------------------------------------------------
// What versions up to 0.81.1 wrote for audio, video and web pages: text documents that name the file

static void repair(const QString &W)
{
    const QString L = QStringLiteral("repair: ");
    const QString scriv = makeStyledProject(W + QLatin1String("/old"));
    const char *kSnd = "A2000000-0000-4000-8000-000000000031", *kPage = "A2000000-0000-4000-8000-000000000032";
    {
        QDomDocument d = scrivxDom(scriv);
        QDomElement research = itemEl(d, QStringLiteral("A2000000-0000-4000-8000-000000000010")).firstChildElement(QStringLiteral("Children"));
        auto add = [&](const char *uuid, const QString &title) {
            QDomElement e = d.createElement(QStringLiteral("BinderItem"));
            e.setAttribute(QStringLiteral("UUID"), QLatin1String(uuid));
            e.setAttribute(QStringLiteral("Type"), QStringLiteral("Text"));
            e.setAttribute(QStringLiteral("Created"), QStringLiteral("2026-11-05 10:00:00 +0100"));
            e.setAttribute(QStringLiteral("Modified"), QStringLiteral("2026-11-05 10:00:00 +0100"));
            QDomElement t = d.createElement(QStringLiteral("Title"));
            t.appendChild(d.createTextNode(title));
            e.appendChild(t);
            QDomElement m = d.createElement(QStringLiteral("MetaData"));
            QDomElement inc = d.createElement(QStringLiteral("IncludeInCompile"));
            inc.appendChild(d.createTextNode(QStringLiteral("Yes")));
            m.appendChild(inc);
            e.appendChild(m);
            research.appendChild(e);
        };
        add(kSnd, QStringLiteral("Bell"));
        add(kPage, QStringLiteral("Ferry times"));
        writeAll(scrivener::scrivxIn(scriv), d.toByteArray(4));
        const QString data = scriv + QLatin1String("/Files/Data/");
        auto standIn = [](const char *kind, const char *ext, const char *uuid) {
            return plainRtf(QStringLiteral("[Quillbinder: this %1 is shown in Quillbinder. The file is content.%2 in the folder of this document "
                                           "(Files/Data/%3).]").arg(QLatin1String(kind), QLatin1String(ext), QLatin1String(uuid)));
        };
        writeAll(data + QLatin1String(kSnd) + QLatin1String("/content.rtf"), standIn("audio or video file", "wav", kSnd));
        writeAll(data + QLatin1String(kSnd) + QLatin1String("/content.wav"), QByteArray("RIFF\x24\x00\x00\x00WAVEfmt ", 16) + QByteArray(28, '\0'));
        writeAll(data + QLatin1String(kPage) + QLatin1String("/content.rtf"), standIn("web page", "html", kPage));
        writeAll(data + QLatin1String(kPage) + QLatin1String("/content.html"),
                 "<html><body><p>Ferries every hour.</p><img src=\"page-files/ferry.png\"></body></html>");
        writeAll(data + QLatin1String(kPage) + QLatin1String("/page-files/ferry.png"), pngBytes(QColor(0, 140, 140)));
    }
    QString err;
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("opened"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    const BinderItem *snd = q->item(QLatin1String(kSnd)), *page = q->item(QLatin1String(kPage));
    check(snd && snd->type == ItemType::Media && page && page->type == ItemType::WebPage,
          L + QStringLiteral("the stand-in texts are read as the audio file and the web page they name"));
    check(q->save(&err), L + QStringLiteral("saved"), err);
    const QDomDocument x = scrivxDom(scriv);
    const QDomElement se = itemEl(x, QLatin1String(kSnd)), pe = itemEl(x, QLatin1String(kPage));
    const QString data = scriv + QLatin1String("/Files/Data/");
    check(se.attribute(QStringLiteral("Type")) == QLatin1String("Media") && metaText(se, QStringLiteral("FileExtension")) == QLatin1String("wav")
              && QFile::exists(data + QLatin1String(kSnd) + QLatin1String("/content.wav")) && !QFile::exists(data + QLatin1String(kSnd) + QLatin1String("/content.rtf")),
          L + QStringLiteral("the audio file is Scrivener's audio document now; its stand-in text is gone"));
    QString shown, err2;
    const bool ok = webarchive::unpack(readAll(data + QLatin1String(kPage) + QLatin1String("/content.mht")), W + QLatin1String("/old-unpacked"), &shown, nullptr, &err2);
    check(pe.attribute(QStringLiteral("Type")) == QLatin1String("WebArchive") && metaText(pe, QStringLiteral("FileExtension")) == QLatin1String("mht") && ok
              && readAll(shown).contains("Ferries every hour.") && !QFile::exists(data + QLatin1String(kPage) + QLatin1String("/content.rtf"))
              && !QFile::exists(data + QLatin1String(kPage) + QLatin1String("/content.html"))
              && !QDir(data + QLatin1String(kPage) + QLatin1String("/page-files")).exists(),
          L + QStringLiteral("the web page is Scrivener's web page now (.mht with its picture); stand-in, HTML file and folder are gone"), err2);
    // a text that only mentions a document's folder is no stand-in
    {
        const QString uuid = QLatin1String(kT2);
        writeAll(data + uuid + QLatin1String("/content.rtf"), plainRtf(QStringLiteral("See Files/Data/") + uuid + QLatin1String(" for the old notes.")));
        scrivener::OpenResult r2 = scrivener::open(scriv, &err);
        check(r2.project && r2.project->save(&err) && QFile::exists(data + uuid + QLatin1String("/content.rtf"))
                  && r2.project->item(uuid)->type == ItemType::Text,
              L + QStringLiteral("a text that names a document's folder stays a text"), err);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// A copy of a real project (QB_SCRIV_REAL): opened and saved without a change, every text written anew

// A text as compared: per paragraph its style, then the runs of its text with their character style and notes
static QString signature(const QTextDocument *doc)
{
    QString out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString ps = b.blockFormat().stringProperty(props::Style);
        out += ps.isEmpty() ? QStringLiteral("[] ") : QStringLiteral("[S:%1] ").arg(ps);
        QString tag, run;
        auto flush = [&] {
            if (!run.isEmpty())
                out += tag.isEmpty() ? run : QStringLiteral("{%1:%2}").arg(tag, run);
            run.clear();
        };
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            QStringList parts;
            if (const QString cs = cf.stringProperty(props::CharStyle); !cs.isEmpty())
                parts << QStringLiteral("C:") + cs;
            if (const auto k = NoteKind(cf.intProperty(props::NoteKind)); k != NoteKind::None)
                parts << QStringLiteral("N%1").arg(int(k)) + (isInlineNote(k) ? QString() : QLatin1Char('=') + cf.stringProperty(props::NoteText));
            const QString t = parts.join(QLatin1Char(' '));
            if (t != tag) {
                flush();
                tag = t;
            }
            if (cf.isImageFormat()) {
                const QImage img = const_cast<QTextDocument *>(doc)->resource(QTextDocument::ImageResource, QUrl(cf.toImageFormat().name())).value<QImage>();
                run += QStringLiteral("[picture %1x%2]").arg(img.width()).arg(img.height());
            } else {
                // an object character without a picture (Scrivener for Windows leaves such where a picture's data is
                // missing) is not written back: left out here
                run += QString(it.fragment().text()).remove(QChar::ObjectReplacementCharacter);
            }
        }
        flush();
        out += QLatin1Char('\n');
    }
    return out;
}

static void realProject(const QString &source, const QString &W)
{
    const QString L = QStringLiteral("real project: ");
    const QString scriv = W + QLatin1String("/real/") + QFileInfo(source).fileName();
    copyTree(source, scriv);
    const QMap<QString, QByteArray> before = filesOf(scriv);
    QString err;
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(bool(r.project), L + QStringLiteral("opened"), err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    check(q->save(&err), L + QStringLiteral("saved without a change"), err);
    {
        QMap<QString, QByteArray> after = filesOf(scriv);
        // what this program keeps beside Scrivener's files in a .scriv
        for (const QString &k : after.keys())
            if (k.endsWith(QLatin1String("quillbinder.lock")) || (k == QLatin1String("Files/search.indexes") && !before.contains(k)))
                after.remove(k);
        check(differences(before, after).isEmpty(), L + QStringLiteral("nothing changed"), differences(before, after));
    }
    // every text written anew: read back, the same words, paragraph styles, character styles and notes as read before
    ScrivenerImportReport rep;
    auto first = ScrivenerImporter::importProject(source, W + QLatin1String("/real-first"), QStringLiteral("First"), StorageFormat::Rtf, &rep, &err);
    check(bool(first), L + QStringLiteral("read as it came"), err);
    if (!first)
        return;
    for (BinderItem *it : first->allItems()) {
        if (it->type != ItemType::WebPage)
            continue;
        // the saved page in the view: shown as a page, the address
        MediaView v;
        v.resize(820, 520);
        v.setProject(first.get());
        v.showItem(it);
        v.show();
        QTest::qWait(150);
        QTextBrowser *browser = nullptr;
        for (QTextBrowser *b : v.findChildren<QTextBrowser *>())
            if (b->isVisible())
                browser = b;
        auto *address = v.findChild<QLabel *>(QStringLiteral("webAddress"));
        const QString shown = browser ? browser->toPlainText() : QString();
        check(shown.trimmed().size() > 20 && !shown.contains(QLatin1String("Content-Type")) && !shown.contains(QLatin1String("------=_"))
                  && address && address->text().startsWith(QLatin1String("http")),
              L + QStringLiteral("a web page Scrivener saved is shown as a page, with its address"), shown.left(200));
        v.grab().save(outDir + QLatin1String("/v0813-real-web-page.png"));
    }
    QHash<QString, QString> textBefore;
    int styled = 0, inlineNotes = 0;
    for (BinderItem *it : first->allItems()) {
        QTextDocument *d = it->hasText() ? first->docs()->text(it->id) : nullptr;
        if (!d || !q->item(it->id) || !q->docs()->text(it->id))
            continue;
        const QString sig = signature(d);
        textBefore.insert(it->id, sig);
        styled += sig.contains(QLatin1String("[S:")) || sig.contains(QLatin1String("{C:"));
        inlineNotes += countKind(d, NoteKind::InlineFootnote) + countKind(d, NoteKind::InlineAnnotation);
        QTextCursor c(q->docs()->text(it->id));   // a change and its undoing: the text is written anew
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("x"));
        c.deletePreviousChar();
    }
    check(q->save(&err), L + QStringLiteral("saved with every text written anew"), err);
    auto back = ScrivenerImporter::importProject(scriv, W + QLatin1String("/real-back"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
    check(bool(back), L + QStringLiteral("read back"), err);
    if (!back)
        return;
    QStringList differ;
    for (auto it = textBefore.cbegin(); it != textBefore.cend(); ++it) {
        QTextDocument *d = back->docs()->text(it.key());
        const QString now = d ? signature(d) : QString();
        if (now != it.value()) {
            int i = 0;
            while (i < now.size() && i < it.value().size() && now.at(i) == it.value().at(i))
                ++i;
            if (const QString dump = qEnvironmentVariable("QB_V0813_DUMP"); !dump.isEmpty()) {
                writeAll(dump + QLatin1Char('/') + it.key() + QLatin1String(".before.txt"), it.value().toUtf8());
                writeAll(dump + QLatin1Char('/') + it.key() + QLatin1String(".after.txt"), now.toUtf8());
            }
            differ << QStringLiteral("%1 at %2: \"%3\" / \"%4\"").arg(first->item(it.key())->title).arg(i)
                          .arg(it.value().mid(qMax(0, i - 30), 80), now.mid(qMax(0, i - 30), 80));
        }
    }
    if (const QString dump = qEnvironmentVariable("QB_V0813_DUMP"); !dump.isEmpty() && !differ.isEmpty())
        copyTree(scriv, dump + QLatin1String("/written.scriv"));
    std::printf("        (%d texts, %d with styles, %d inline notes)\n", int(textBefore.size()), styled, inlineNotes);
    check(differ.isEmpty(), L + QStringLiteral("every text written anew reads back with the same words, styles and notes"),
          differ.mid(0, 6).join(QLatin1String("\n        ")));
    {
        // the marks as Scrivener places them: no paragraph that holds only marks where the original had none
        int extra = 0;
        for (auto it = textBefore.cbegin(); it != textBefore.cend(); ++it) {
            const QString dir = scriv + QLatin1String("/Files/Data/") + it.key().toUpper();
            const QString t = markedText(dir + QLatin1String("/content.rtf"));
            for (const QString &line : t.split(QLatin1Char('\n')))
                if (!line.isEmpty() && QString(line).remove(QRegularExpression(QStringLiteral("<!?\\$Scr_[A-Za-z]+::[0-9]+>"))).isEmpty()
                    && !QString::fromUtf8(readAll(source + QLatin1String("/Files/Data/") + it.key().toUpper() + QLatin1String("/content.rtf"))).contains(QLatin1String("$Scr")))
                    ++extra;
        }
        check(extra == 0, L + QStringLiteral("no paragraph of marks only is added"), QString::number(extra));
    }
    check(readAll(scriv + QLatin1String("/Files/styles.xml")) == before.value(QStringLiteral("Files/styles.xml")),
          L + QStringLiteral("styles.xml unchanged (every style the texts use is in it)"));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + QLatin1String("/cache")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.81.3")) >= 0,
          QStringLiteral("version 0.81.3 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    AppSettings::instance().load();
    AppSettings::instance().backupOnOpen = false;
    AppSettings::instance().backupOnClose = false;
    AppSettings::instance().save();
    QTemporaryDir work;
    const QString W = work.path();

    styledProject(W);
    webArchives(W);
    {
        // the view of the page Scrivener for Windows saved (its .mht, the address it keeps)
        QString err;
        ScrivenerImportReport rep;
        const QString scriv = makeStyledProject(W + QLatin1String("/view"));
        auto p = ScrivenerImporter::importProject(scriv, W + QLatin1String("/view-import"), QStringLiteral("View"), StorageFormat::Rtf, &rep, &err);
        BinderItem *web = p ? p->item(QLatin1String(kWeb)) : nullptr;
        check(bool(web), QStringLiteral("view: the page of the Windows project"), err);
        if (web) {
            webView(p.get(), web, QStringLiteral("view of Scrivener's page: "), QStringLiteral("https://example.com/tides/today.html"),
                    QStringLiteral("High water 06:12, low water 12:30."), QStringLiteral("v0813-scrivener-page.png"));
            web->sourceUrl.clear();
            webView(p.get(), web, QStringLiteral("view, address only in the archive: "), QStringLiteral("https://example.com/tides/today.html"),
                    QStringLiteral("High water"), QStringLiteral("v0813-archive-address.png"));
            // a page with no address at all
            QString shown;
            const QString bare = W + QLatin1String("/bare/content.html");
            writeAll(bare, "<html><body><p>No address here.</p><img src=\"f/a.png\"></body></html>");
            writeAll(W + QLatin1String("/bare/f/a.png"), pngBytes(QColor(1, 2, 3)));
            importers::WebPage wp;
            wp.title = QStringLiteral("Bare page");
            wp.asWebPage = true;
            wp.htmlFile = bare;
            BinderItem *b = importer::addWebPage(p.get(), wp, p->research(), -1, nullptr);
            if (b)
                webView(p.get(), b, QStringLiteral("view without an address: "), QString(), QStringLiteral("No address here."), QStringLiteral("v0813-no-address.png"));
        }
    }
    ownProject(W);
    repair(W);
    const QString real = qEnvironmentVariable("QB_SCRIV_REAL");
    if (!real.isEmpty())
        realProject(real, W);

    std::printf("%d failure(s)\n", failures);
    return failures;
}
