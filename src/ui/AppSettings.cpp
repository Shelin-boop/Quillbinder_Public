#include "ui/AppSettings.h"
#include "core/Backup.h"
#include "core/Addons.h"
#include "core/ItemIcons.h"
#include "core/PageSetup.h"
#include "core/PrintSettings.h"
#include "core/ProjectTemplates.h"
#include "import/FileImporter.h"
#include "import/ScrivenerImporter.h"
#include "compile/Compiler.h"
#include "text/TextUtil.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace qb {

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

// Options that parts of the program without access to AppSettings use (the core and
// compile libraries do not know the window side): handed over after load and save.
void AppSettings::pushToModules()
{
    text::setRevisionColors(revisionColors);
    compile::setAuthorContact({{QStringLiteral("street"), street}, {QStringLiteral("city"), city}, {QStringLiteral("state"), state},
                               {QStringLiteral("zip"), zip}, {QStringLiteral("country"), country}, {QStringLiteral("phone"), phone},
                               {QStringLiteral("email"), email}});
    templates::setSharedTemplatesDir(sharedTemplatesDir);
    importer::ImportOptions io;
    io.commentsInline = importCommentsInline;
    io.footnotesInline = importFootnotesInline;
    io.ignoreStyles = importIgnoreStyles;
    io.encoding = plainTextEncoding;
    io.htmlAsText = htmlAsText;
    io.docConverter = docConverter;
    importer::setOptions(io);
    // Scrivener imports start with the formatting of new projects (0.21.2)
    Formatting nf;
    nf.fontFamily = newFontFamily;
    nf.fontSize = newFontSize;
    nf.lineSpacing = newLineSpacing;
    nf.firstLineIndent = newFirstLineIndent;
    nf.paragraphSpacing = newParagraphSpacing;
    nf.align = newAlign;
    ScrivenerImporter::setNewProjectFormatting(nf);
}

QMap<QString, QColor> AppSettings::standardOwnColors()
{
    return {{QStringLiteral("editorPaper"), QColor(0xd1, 0xd4, 0x95)}, {QStringLiteral("editorMargin"), QColor(0x99, 0x99, 0x99)}};
}

void AppSettings::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("app")); // not "general": QSettings reserves [General] in INI files
    forename = s.value(QStringLiteral("forename")).toString();
    surname = s.value(QStringLiteral("surname")).toString();
    autosaveSeconds = s.value(QStringLiteral("autosave"), 2).toInt();
    openLastProject = s.value(QStringLiteral("openLast"), true).toBool();
    recentProjects = s.value(QStringLiteral("recent")).toStringList();
    welcomeFolder = s.value(QStringLiteral("welcomeFolder")).toString();
    welcomeShown = s.value(QStringLiteral("welcomeShown"), false).toBool();
    lastProjectDir = s.value(QStringLiteral("lastDir")).toString();
    useDesktopPortal = s.value(QStringLiteral("portal"), true).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("appearance"));
    theme = s.value(QStringLiteral("theme"), theme).toString();
    fixedWidth = s.value(QStringLiteral("fixedWidth"), fixedWidth).toBool();
    editorWidth = s.value(QStringLiteral("editorWidth"), editorWidth).toInt();
    textBorder = qBound(0, s.value(QStringLiteral("textBorder"), textBorder).toInt(), 200);
    // "combinedTitles" from 0.40 on; "scriveningsTitles" is the name older versions wrote
    showTitlesInCombined = s.value(QStringLiteral("combinedTitles"),
                                   s.value(QStringLiteral("scriveningsTitles"), showTitlesInCombined)).toBool();
    typewriterScrolling = s.value(QStringLiteral("typewriter"), typewriterScrolling).toBool();
    compositionPaperWidth = s.value(QStringLiteral("compPaperWidth"), compositionPaperWidth).toDouble();
    compositionFade = s.value(QStringLiteral("compFade"), compositionFade).toDouble();
    compositionZoomPercent = s.value(QStringLiteral("compZoom"), compositionZoomPercent).toInt();
    showStatusStamps = s.value(QStringLiteral("stamps"), showStatusStamps).toBool();
    tintCardsWithLabel = s.value(QStringLiteral("tintCards"), tintCardsWithLabel).toBool();
    labelColorInBinder = s.value(QStringLiteral("labelInBinder"), labelColorInBinder).toBool();
    labelColorInIcons = s.value(QStringLiteral("labelInIcons"), labelColorInIcons).toBool();
    binderIndent = qBound(4, s.value(QStringLiteral("binderIndent"), binderIndent).toInt(), 40);
    recentIcons = s.value(QStringLiteral("recentIcons")).toStringList();
    iconBarSet = s.contains(QStringLiteral("iconBar"));
    iconBar = s.value(QStringLiteral("iconBar")).toStringList();
    compareByChapter = s.value(QStringLiteral("compareByChapter"), compareByChapter).toBool();
    compareStepByStep = s.value(QStringLiteral("compareStepByStep"), compareStepByStep).toBool();
    // an empty entry: "Automatic" / "No Highlight" was chosen last
    for (auto [key, color] : {std::pair<const char *, QColor *>{"barTextColor", &barTextColor}, {"barHighlightColor", &barHighlightColor}}) {
        if (!s.contains(QLatin1String(key)))
            continue;
        const QString v = s.value(QLatin1String(key)).toString();
        if (v.isEmpty())
            *color = QColor();
        else if (const QColor c(v); c.isValid())
            *color = c;
    }
    cardWidth = s.value(QStringLiteral("cardWidth"), cardWidth).toInt();
    cardsPerRow = s.value(QStringLiteral("cardsPerRow"), cardsPerRow).toInt();
    ownColors.clear();
    ownFonts.clear();
    s.beginGroup(QStringLiteral("colors"));
    for (const QString &k : s.childKeys()) {
        const QColor c(s.value(k).toString());
        theme::Role r;
        if (c.isValid() && theme::roleFromKey(k, &r))
            ownColors.insert(k, c);
    }
    s.endGroup();
    s.beginGroup(QStringLiteral("fonts"));
    for (const QString &k : s.childKeys()) {
        theme::FontRole r;
        if (theme::fontRoleFromKey(k, &r) && theme::FontSpec::fromString(s.value(k).toString()).isSet())
            ownFonts.insert(QLatin1String(theme::key(r)), s.value(k).toString()); // old names become the current ones
    }
    s.endGroup();
    // Versions before 0.5 kept four colors as settings of their own; values
    // the writer changed become own colors.
    const struct {
        const char *oldKey, *newKey;
        QColor oldDefault;
    } legacy[] = {{"compText", "compositionText", QColor(230, 230, 230)},
                  {"compBackground", "compositionBackground", QColor(28, 32, 40)},
                  {"compPaper", "compositionPaper", QColor(40, 45, 56)},
                  {"corkColor", "corkboardBackground", QColor(0xC9, 0xA8, 0x77)}};
    for (const auto &l : legacy) {
        if (!s.contains(QLatin1String(l.oldKey)))
            continue;
        const QVariant v = s.value(QLatin1String(l.oldKey));
        const QColor c = v.userType() == QMetaType::QString ? QColor(v.toString()) : v.value<QColor>();
        if (c.isValid() && c.rgb() != l.oldDefault.rgb() && !ownColors.contains(QLatin1String(l.newKey)))
            ownColors.insert(QLatin1String(l.newKey), QColor(c.red(), c.green(), c.blue()));
    }
    s.endGroup();

    s.beginGroup(QStringLiteral("editing"));
    countCharacters = s.value(QStringLiteral("countChars"), countCharacters).toBool();
    spellCheck = s.value(QStringLiteral("spellCheck"), spellCheck).toBool();
    spellLanguage = s.value(QStringLiteral("spellLanguage"), spellLanguage).toString();
    smartQuotes = s.value(QStringLiteral("smartQuotes"), smartQuotes).toBool();
    smartDashes = s.value(QStringLiteral("smartDashes"), smartDashes).toBool();
    showRuler = s.value(QStringLiteral("showRuler"), showRuler).toBool();
    rulerUnit = s.value(QStringLiteral("rulerUnit"), rulerUnit).toString();
    newFontFamily = s.value(QStringLiteral("newFontFamily"), newFontFamily).toString();
    newPageSetup = s.value(QStringLiteral("newPageSetup"), newPageSetup).toString();
    PageSetup::setStandard(PageSetup::fromVariant(QJsonDocument::fromJson(newPageSetup.toUtf8()).object().toVariantMap(),
                                                  PageSetup::forLocale()));
    newPrintSettings = s.value(QStringLiteral("newPrintSettings"), newPrintSettings).toString();
    PrintSettings::setStandardMap(QJsonDocument::fromJson(newPrintSettings.toUtf8()).object().toVariantMap());
    newFontSize = s.value(QStringLiteral("newFontSize"), newFontSize).toDouble();
    newLineSpacing = s.value(QStringLiteral("newLineSpacing"), newLineSpacing).toDouble();
    newFirstLineIndent = s.value(QStringLiteral("newFirstLineIndent"), newFirstLineIndent).toDouble();
    newParagraphSpacing = s.value(QStringLiteral("newParagraphSpacing"), newParagraphSpacing).toDouble();
    // "newAlign" since 0.40.3; settings of older versions have "newJustify" only
    newAlign = alignmentFromName(s.value(QStringLiteral("newAlign")).toString(),
                                 s.value(QStringLiteral("newJustify"), newAlign == Qt::AlignJustify).toBool() ? Qt::AlignJustify
                                                                                                           : Qt::AlignLeft);
    newStylesXml = s.value(QStringLiteral("newStyles"), newStylesXml).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("backup"));
    backupOnClose = s.value(QStringLiteral("onClose"), backupOnClose).toBool();
    backupOnOpen = s.value(QStringLiteral("onOpen"), backupOnOpen).toBool();
    backupKeep = s.value(QStringLiteral("keep"), backupKeep).toInt();
    backupDir = s.value(QStringLiteral("dir"), backup::defaultDirectory()).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("behaviour"));
    linkOpens = s.value(QStringLiteral("linkOpens"), linkOpens).toString();
    startWithCorkboard = s.value(QStringLiteral("folderCorkboard"), startWithCorkboard).toBool();
    floatQuickRef = s.value(QStringLiteral("floatQuickRef"), floatQuickRef).toBool();
    reopenQuickRefs = s.value(QStringLiteral("reopenQuickRefs"), reopenQuickRefs).toBool();
    dropOnTextDocuments = s.value(QStringLiteral("dropOnTextDocuments"), dropOnTextDocuments).toBool();
    dragExportFormat = s.value(QStringLiteral("dragExportFormat"), dragExportFormat).toString();
    if (!QStringList{QStringLiteral("rtf"), QStringLiteral("docx"), QStringLiteral("md"), QStringLiteral("txt")}.contains(dragExportFormat))
        dragExportFormat = QStringLiteral("rtf");
    dragExportChildren = s.value(QStringLiteral("dragExportChildren"), dragExportChildren).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("playback"));
    mediaPauseRewind = s.value(QStringLiteral("pauseRewind"), mediaPauseRewind).toBool();
    mediaRewindSeconds = qBound(0.5, s.value(QStringLiteral("rewindSeconds"), mediaRewindSeconds).toDouble(), 30.0);
    mediaSkipSeconds = qBound(1, s.value(QStringLiteral("skipSeconds"), mediaSkipSeconds).toInt(), 300);
    mediaBackend = s.value(QStringLiteral("backend"), mediaBackend).toString();
    mediaRememberPosition = s.value(QStringLiteral("rememberPosition"), mediaRememberPosition).toBool();
    mediaVolume = qBound(0.0, s.value(QStringLiteral("volume"), mediaVolume).toDouble(), 1.0);
    s.endGroup();

    // ---- 0.21
    s.beginGroup(QStringLiteral("app"));
    recentCount = qBound(3, s.value(QStringLiteral("recentCount"), recentCount).toInt(), 50);
    snapshotOnManualSave = s.value(QStringLiteral("snapshotOnSave"), snapshotOnManualSave).toBool();
    street = s.value(QStringLiteral("street"), street).toString();
    city = s.value(QStringLiteral("city"), city).toString();
    state = s.value(QStringLiteral("state"), state).toString();
    zip = s.value(QStringLiteral("zip"), zip).toString();
    country = s.value(QStringLiteral("country"), country).toString();
    phone = s.value(QStringLiteral("phone"), phone).toString();
    email = s.value(QStringLiteral("email"), email).toString();
    sharedTemplatesDir = s.value(QStringLiteral("sharedTemplates"), sharedTemplatesDir).toString();
    citationsApp = s.value(QStringLiteral("citationsApp"), citationsApp).toString();
    sepMerge = s.value(QStringLiteral("sepMerge"), sepMerge).toString();
    sepMergeCustom = s.value(QStringLiteral("sepMergeCustom"), sepMergeCustom).toString();
    sepScratch = s.value(QStringLiteral("sepScratch"), sepScratch).toString();
    sepScratchCustom = s.value(QStringLiteral("sepScratchCustom"), sepScratchCustom).toString();
    autoQuit = s.value(QStringLiteral("autoQuit"), autoQuit).toBool();
    autoQuitMinutes = qBound(1, s.value(QStringLiteral("autoQuitMinutes"), autoQuitMinutes).toInt(), 600);
    freezeSeconds = qBound(10, s.value(QStringLiteral("freezeSeconds"), freezeSeconds).toInt(), 600);
    keepReports = qBound(1, s.value(QStringLiteral("keepReports"), keepReports).toInt(), 200);
    s.endGroup();
    s.beginGroup(QStringLiteral("editing"));
    reloadZoom = s.value(QStringLiteral("reloadZoom"), reloadZoom).toBool();
    rulerSnapMm = qBound(0.0, s.value(QStringLiteral("rulerSnapMm"), rulerSnapMm).toDouble(), 20.0);
    overtypeKey = s.value(QStringLiteral("overtypeKey"), overtypeKey).toBool();
    customDateFormat = s.value(QStringLiteral("dateFormat"), customDateFormat).toString();
    noColorInlineAnnotations = s.value(QStringLiteral("noColorAnnotations"), noColorInlineAnnotations).toBool();
    notesBeforePunctuation = s.value(QStringLiteral("notesBeforePunctuation"), notesBeforePunctuation).toBool();
    blockCursor = s.value(QStringLiteral("blockCursor"), blockCursor).toBool();
    blockCursorWidth = qBound(2, s.value(QStringLiteral("blockCursorWidth"), blockCursorWidth).toInt(), 20);
    revisionColors.clear();
    for (const QString &c : s.value(QStringLiteral("revisionColors")).toStringList())
        if (QColor(c).isValid())
            revisionColors << QColor(c);
    if (revisionColors.size() != 5)
        revisionColors.clear();
    capitalizeSentences = s.value(QStringLiteral("capitalizeSentences"), capitalizeSentences).toBool();
    capitalizeI = s.value(QStringLiteral("capitalizeI"), capitalizeI).toBool();
    quoteStyle = s.value(QStringLiteral("quoteStyle"), quoteStyle).toString();
    ellipsis = s.value(QStringLiteral("ellipsis"), ellipsis).toBool();
    scriptNoSmart = s.value(QStringLiteral("scriptNoSmart"), scriptNoSmart).toBool();
    detectUrls = s.value(QStringLiteral("detectUrls"), detectUrls).toBool();
    detectDocLinks = s.value(QStringLiteral("detectDocLinks"), detectDocLinks).toBool();
    completions = s.value(QStringLiteral("completions"), completions).toBool();
    substitutionsOn = s.value(QStringLiteral("substitutionsOn"), substitutionsOn).toBool();
    substitutions.clear();
    if (s.contains(QStringLiteral("substitutions"))) {
        const QStringList l = s.value(QStringLiteral("substitutions")).toStringList();
        for (int i = 0; i + 1 < l.size(); i += 2)
            if (!l[i].isEmpty())
                substitutions.append({l[i], l[i + 1]});
    } else {
        substitutions = standardSubstitutions();
    }
    spellIgnoreNumbers = s.value(QStringLiteral("spellIgnoreNumbers"), spellIgnoreNumbers).toBool();
    spellUnderline = s.value(QStringLiteral("spellUnderline"), spellUnderline).toString();
    s.endGroup();
    s.beginGroup(QStringLiteral("behaviour"));
    compositionScreen = s.value(QStringLiteral("compScreen"), compositionScreen).toString();
    compositionHideMain = s.value(QStringLiteral("compHideMain"), compositionHideMain).toBool();
    compositionEscCloses = s.value(QStringLiteral("compEsc"), compositionEscCloses).toBool();
    backLinks = s.value(QStringLiteral("backLinks"), backLinks).toBool();
    corkDoubleClickNew = s.value(QStringLiteral("corkDoubleClickNew"), corkDoubleClickNew).toBool();
    subdocsAsFolders = s.value(QStringLiteral("subdocsAsFolders"), subdocsAsFolders).toBool();
    returnEndsSynopsis = s.value(QStringLiteral("returnEndsSynopsis"), returnEndsSynopsis).toBool();
    returnCreatesItem = s.value(QStringLiteral("returnCreatesItem"), returnCreatesItem).toBool();
    snapshotNotify = s.value(QStringLiteral("snapshotNotify"), snapshotNotify).toBool();
    s.endGroup();
    s.beginGroup(QStringLiteral("appearance"));
    showFullPath = s.value(QStringLiteral("showFullPath"), showFullPath).toBool();
    s.endGroup();
    s.beginGroup(QStringLiteral("sharing"));
    importCommentsInline = s.value(QStringLiteral("commentsInline"), importCommentsInline).toBool();
    importFootnotesInline = s.value(QStringLiteral("footnotesInline"), importFootnotesInline).toBool();
    importIgnoreStyles = s.value(QStringLiteral("ignoreStyles"), importIgnoreStyles).toBool();
    plainTextEncoding = s.value(QStringLiteral("encoding"), plainTextEncoding).toString();
    htmlAsText = s.value(QStringLiteral("htmlAsText"), htmlAsText).toBool();
    docConverter = s.value(QStringLiteral("docConverter"), docConverter).toString();
    s.endGroup();
    s.beginGroup(QStringLiteral("backup"));
    backupOnManualSave = s.value(QStringLiteral("onManualSave"), backupOnManualSave).toBool();
    s.endGroup();
    // the writer's standard of 0.21.2, once (also in settings an older version wrote)
    const bool putStandard = !s.contains(QLatin1String(kStandardKey));
    if (putStandard) {
        const AppSettings standard;   // the members' standard values
        newFontSize = standard.newFontSize;
        newLineSpacing = standard.newLineSpacing;
        newFirstLineIndent = standard.newFirstLineIndent;
        newParagraphSpacing = standard.newParagraphSpacing;
        editorWidth = standard.editorWidth;
        const QMap<QString, QColor> colors = standardOwnColors();
        for (auto it = colors.cbegin(); it != colors.cend(); ++it)
            ownColors.insert(it.key(), it.value());
    }
    pushToModules();

    s.beginGroup(QStringLiteral("toolbars"));
    toolbarMain = s.value(QStringLiteral("main")).toStringList();
    toolbarFormat = s.value(QStringLiteral("format")).toStringList();
    toolbarIconSize = qBound(16, s.value(QStringLiteral("iconSize"), toolbarIconSize).toInt(), 48);
    toolbarStyle = s.value(QStringLiteral("style"), toolbarStyle).toString();
    s.endGroup();
    if (putStandard)
        save(); // writes the standard and the key that says it was put
}

void AppSettings::save()
{
    QSettings s;
    s.beginGroup(QStringLiteral("app")); // not "general": QSettings reserves [General] in INI files
    s.setValue(QStringLiteral("forename"), forename);
    s.setValue(QStringLiteral("surname"), surname);
    s.setValue(QStringLiteral("autosave"), autosaveSeconds);
    s.setValue(QStringLiteral("openLast"), openLastProject);
    s.setValue(QStringLiteral("recent"), recentProjects);
    s.setValue(QStringLiteral("welcomeFolder"), welcomeFolder);
    s.setValue(QStringLiteral("welcomeShown"), welcomeShown);
    s.remove(QStringLiteral("tutorialFolder")); // the interactive tutorial of 0.15 to 0.30 is gone
    s.setValue(QStringLiteral("lastDir"), lastProjectDir);
    s.setValue(QStringLiteral("portal"), useDesktopPortal);
    s.setValue(QStringLiteral("standard0212"), true); // see kStandardKey
    s.endGroup();

    s.beginGroup(QStringLiteral("appearance"));
    s.setValue(QStringLiteral("theme"), theme);
    s.setValue(QStringLiteral("fixedWidth"), fixedWidth);
    s.setValue(QStringLiteral("editorWidth"), editorWidth);
    s.setValue(QStringLiteral("textBorder"), textBorder);
    s.setValue(QStringLiteral("combinedTitles"), showTitlesInCombined);
    s.remove(QStringLiteral("scriveningsTitles"));
    s.setValue(QStringLiteral("typewriter"), typewriterScrolling);
    s.setValue(QStringLiteral("compPaperWidth"), compositionPaperWidth);
    s.setValue(QStringLiteral("compFade"), compositionFade);
    s.setValue(QStringLiteral("compZoom"), compositionZoomPercent);
    s.setValue(QStringLiteral("stamps"), showStatusStamps);
    s.setValue(QStringLiteral("tintCards"), tintCardsWithLabel);
    s.setValue(QStringLiteral("labelInBinder"), labelColorInBinder);
    s.setValue(QStringLiteral("labelInIcons"), labelColorInIcons);
    s.setValue(QStringLiteral("binderIndent"), binderIndent);
    s.setValue(QStringLiteral("recentIcons"), recentIcons);
    if (iconBarSet)
        s.setValue(QStringLiteral("iconBar"), iconBar);
    else
        s.remove(QStringLiteral("iconBar"));
    s.setValue(QStringLiteral("compareByChapter"), compareByChapter);
    s.setValue(QStringLiteral("compareStepByStep"), compareStepByStep);
    s.setValue(QStringLiteral("barTextColor"), barTextColor.isValid() ? barTextColor.name(QColor::HexRgb) : QString());
    s.setValue(QStringLiteral("barHighlightColor"), barHighlightColor.isValid() ? barHighlightColor.name(QColor::HexRgb) : QString());
    s.setValue(QStringLiteral("cardWidth"), cardWidth);
    s.setValue(QStringLiteral("cardsPerRow"), cardsPerRow);
    for (const char *old : {"compText", "compBackground", "compPaper", "corkColor"})
        s.remove(QLatin1String(old)); // now part of "colors"
    s.remove(QStringLiteral("colors"));
    s.beginGroup(QStringLiteral("colors"));
    for (auto it = ownColors.constBegin(); it != ownColors.constEnd(); ++it)
        if (it.value().isValid())
            s.setValue(it.key(), it.value().name(QColor::HexRgb));
    s.endGroup();
    s.remove(QStringLiteral("fonts"));
    s.beginGroup(QStringLiteral("fonts"));
    for (auto it = ownFonts.constBegin(); it != ownFonts.constEnd(); ++it)
        if (!it.value().isEmpty())
            s.setValue(it.key(), it.value());
    s.endGroup();
    s.endGroup();

    s.beginGroup(QStringLiteral("editing"));
    s.setValue(QStringLiteral("countChars"), countCharacters);
    s.setValue(QStringLiteral("spellCheck"), spellCheck);
    s.setValue(QStringLiteral("spellLanguage"), spellLanguage);
    s.setValue(QStringLiteral("smartQuotes"), smartQuotes);
    s.setValue(QStringLiteral("smartDashes"), smartDashes);
    s.setValue(QStringLiteral("showRuler"), showRuler);
    s.setValue(QStringLiteral("rulerUnit"), rulerUnit);
    s.setValue(QStringLiteral("newFontFamily"), newFontFamily);
    s.setValue(QStringLiteral("newPageSetup"), newPageSetup);
    s.setValue(QStringLiteral("newPrintSettings"), newPrintSettings);
    s.setValue(QStringLiteral("newFontSize"), newFontSize);
    s.setValue(QStringLiteral("newLineSpacing"), newLineSpacing);
    s.setValue(QStringLiteral("newFirstLineIndent"), newFirstLineIndent);
    s.setValue(QStringLiteral("newParagraphSpacing"), newParagraphSpacing);
    s.setValue(QStringLiteral("newAlign"), alignmentName(newAlign));
    s.setValue(QStringLiteral("newJustify"), newAlign == Qt::AlignJustify); // for versions before 0.40.3
    s.setValue(QStringLiteral("newStyles"), newStylesXml);
    s.endGroup();

    s.beginGroup(QStringLiteral("backup"));
    s.setValue(QStringLiteral("onClose"), backupOnClose);
    s.setValue(QStringLiteral("onOpen"), backupOnOpen);
    s.setValue(QStringLiteral("keep"), backupKeep);
    s.setValue(QStringLiteral("dir"), backupDir);
    s.endGroup();

    s.beginGroup(QStringLiteral("behaviour"));
    s.setValue(QStringLiteral("linkOpens"), linkOpens);
    s.setValue(QStringLiteral("folderCorkboard"), startWithCorkboard);
    s.setValue(QStringLiteral("floatQuickRef"), floatQuickRef);
    s.setValue(QStringLiteral("reopenQuickRefs"), reopenQuickRefs);
    s.setValue(QStringLiteral("dropOnTextDocuments"), dropOnTextDocuments);
    s.setValue(QStringLiteral("dragExportFormat"), dragExportFormat);
    s.setValue(QStringLiteral("dragExportChildren"), dragExportChildren);
    s.endGroup();

    s.beginGroup(QStringLiteral("playback"));
    s.setValue(QStringLiteral("pauseRewind"), mediaPauseRewind);
    s.setValue(QStringLiteral("rewindSeconds"), mediaRewindSeconds);
    s.setValue(QStringLiteral("skipSeconds"), mediaSkipSeconds);
    s.setValue(QStringLiteral("backend"), mediaBackend);
    s.setValue(QStringLiteral("rememberPosition"), mediaRememberPosition);
    s.setValue(QStringLiteral("volume"), mediaVolume);
    s.endGroup();

    // ---- 0.21
    s.beginGroup(QStringLiteral("app"));
    s.setValue(QStringLiteral("recentCount"), recentCount);
    s.setValue(QStringLiteral("snapshotOnSave"), snapshotOnManualSave);
    s.setValue(QStringLiteral("street"), street);
    s.setValue(QStringLiteral("city"), city);
    s.setValue(QStringLiteral("state"), state);
    s.setValue(QStringLiteral("zip"), zip);
    s.setValue(QStringLiteral("country"), country);
    s.setValue(QStringLiteral("phone"), phone);
    s.setValue(QStringLiteral("email"), email);
    s.setValue(QStringLiteral("sharedTemplates"), sharedTemplatesDir);
    s.setValue(QStringLiteral("citationsApp"), citationsApp);
    s.setValue(QStringLiteral("sepMerge"), sepMerge);
    s.setValue(QStringLiteral("sepMergeCustom"), sepMergeCustom);
    s.setValue(QStringLiteral("sepScratch"), sepScratch);
    s.setValue(QStringLiteral("sepScratchCustom"), sepScratchCustom);
    s.setValue(QStringLiteral("autoQuit"), autoQuit);
    s.setValue(QStringLiteral("autoQuitMinutes"), autoQuitMinutes);
    s.setValue(QStringLiteral("freezeSeconds"), freezeSeconds);
    s.setValue(QStringLiteral("keepReports"), keepReports);
    s.endGroup();
    s.beginGroup(QStringLiteral("editing"));
    s.setValue(QStringLiteral("reloadZoom"), reloadZoom);
    s.setValue(QStringLiteral("rulerSnapMm"), rulerSnapMm);
    s.setValue(QStringLiteral("overtypeKey"), overtypeKey);
    s.setValue(QStringLiteral("dateFormat"), customDateFormat);
    s.setValue(QStringLiteral("noColorAnnotations"), noColorInlineAnnotations);
    s.setValue(QStringLiteral("notesBeforePunctuation"), notesBeforePunctuation);
    s.setValue(QStringLiteral("blockCursor"), blockCursor);
    s.setValue(QStringLiteral("blockCursorWidth"), blockCursorWidth);
    QStringList rev;
    for (const QColor &c : revisionColors)
        rev << c.name(QColor::HexRgb);
    s.setValue(QStringLiteral("revisionColors"), rev);
    s.setValue(QStringLiteral("capitalizeSentences"), capitalizeSentences);
    s.setValue(QStringLiteral("capitalizeI"), capitalizeI);
    s.setValue(QStringLiteral("quoteStyle"), quoteStyle);
    s.setValue(QStringLiteral("ellipsis"), ellipsis);
    s.setValue(QStringLiteral("scriptNoSmart"), scriptNoSmart);
    s.setValue(QStringLiteral("detectUrls"), detectUrls);
    s.setValue(QStringLiteral("detectDocLinks"), detectDocLinks);
    s.setValue(QStringLiteral("completions"), completions);
    s.setValue(QStringLiteral("substitutionsOn"), substitutionsOn);
    QStringList subs;
    for (const auto &p : substitutions)
        subs << p.first << p.second;
    s.setValue(QStringLiteral("substitutions"), subs);
    s.setValue(QStringLiteral("spellIgnoreNumbers"), spellIgnoreNumbers);
    s.setValue(QStringLiteral("spellUnderline"), spellUnderline);
    s.endGroup();
    s.beginGroup(QStringLiteral("behaviour"));
    s.setValue(QStringLiteral("compScreen"), compositionScreen);
    s.setValue(QStringLiteral("compHideMain"), compositionHideMain);
    s.setValue(QStringLiteral("compEsc"), compositionEscCloses);
    s.setValue(QStringLiteral("backLinks"), backLinks);
    s.setValue(QStringLiteral("corkDoubleClickNew"), corkDoubleClickNew);
    s.setValue(QStringLiteral("subdocsAsFolders"), subdocsAsFolders);
    s.setValue(QStringLiteral("returnEndsSynopsis"), returnEndsSynopsis);
    s.setValue(QStringLiteral("returnCreatesItem"), returnCreatesItem);
    s.setValue(QStringLiteral("snapshotNotify"), snapshotNotify);
    s.endGroup();
    s.beginGroup(QStringLiteral("appearance"));
    s.setValue(QStringLiteral("showFullPath"), showFullPath);
    s.endGroup();
    s.beginGroup(QStringLiteral("sharing"));
    s.setValue(QStringLiteral("commentsInline"), importCommentsInline);
    s.setValue(QStringLiteral("footnotesInline"), importFootnotesInline);
    s.setValue(QStringLiteral("ignoreStyles"), importIgnoreStyles);
    s.setValue(QStringLiteral("encoding"), plainTextEncoding);
    s.setValue(QStringLiteral("htmlAsText"), htmlAsText);
    s.setValue(QStringLiteral("docConverter"), docConverter);
    s.endGroup();
    s.beginGroup(QStringLiteral("backup"));
    s.setValue(QStringLiteral("onManualSave"), backupOnManualSave);
    s.endGroup();
    pushToModules();

    s.beginGroup(QStringLiteral("toolbars"));
    s.setValue(QStringLiteral("main"), toolbarMain);
    s.setValue(QStringLiteral("format"), toolbarFormat);
    s.setValue(QStringLiteral("iconSize"), toolbarIconSize);
    s.setValue(QStringLiteral("style"), toolbarStyle);
    s.endGroup();
}

void AppSettings::addRecent(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    recentProjects.removeAll(abs);
    recentProjects.prepend(abs);
    while (recentProjects.size() > qMax(3, recentCount))
        recentProjects.removeLast();
    save();
}

void AppSettings::removeRecent(const QString &path)
{
    recentProjects.removeAll(path);
    save();
}

QList<QPair<QString, QString>> AppSettings::standardSubstitutions()
{
    return {{QStringLiteral("(c)"), QStringLiteral("©")}, {QStringLiteral("(r)"), QStringLiteral("®")},
            {QStringLiteral("(tm)"), QStringLiteral("™")}, {QStringLiteral("1/2"), QStringLiteral("½")},
            {QStringLiteral("1/4"), QStringLiteral("¼")}, {QStringLiteral("3/4"), QStringLiteral("¾")},
            {QStringLiteral("->"), QStringLiteral("→")}, {QStringLiteral("<-"), QStringLiteral("←")}};
}

void AppSettings::resetToDefaults()
{
    // Everything File > Options shows goes back to the standard: the groups of
    // the settings file are removed except what is not an option (recent
    // projects, last folder, the welcome project's folder and whether it was shown, the icon bar and recent
    // icons, toolbars, window places, keyboard shortcuts), then a fresh object
    // reads the empty settings (= its standard values) and writes them.
    QSettings s;
    const QStringList keepApp = {QStringLiteral("recent"), QStringLiteral("lastDir"), QStringLiteral("welcomeFolder"), QStringLiteral("welcomeShown"),
                                 QStringLiteral("integrated")};
    QMap<QString, QVariant> kept;
    for (const QString &k : keepApp)
        if (s.contains(QStringLiteral("app/") + k))
            kept.insert(QStringLiteral("app/") + k, s.value(QStringLiteral("app/") + k));
    for (const char *k : {"appearance/recentIcons", "appearance/iconBar"})
        if (s.contains(QLatin1String(k)))
            kept.insert(QLatin1String(k), s.value(QLatin1String(k)));
    for (const char *g : {"app", "appearance", "editing", "backup", "behaviour", "playback", "sharing", "mindmap", "zoom", "zoomDefault"})
        s.remove(QLatin1String(g));
    s.remove(QStringLiteral("zoomInspectorPerTab"));
    for (auto it = kept.cbegin(); it != kept.cend(); ++it)
        s.setValue(it.key(), it.value());
    s.sync();
    {
        AppSettings fresh;   // the members' standard values
        fresh.load();        // ... with what was kept
        fresh.save();
    }
    load();
    emit changed();
}

namespace units {

bool metric()
{
    const QString u = AppSettings::instance().rulerUnit;
    if (u == QLatin1String("in"))
        return false;
    if (u == QLatin1String("cm"))
        return true;
    return QLocale().measurementSystem() == QLocale::MetricSystem;
}

QString suffix() { return metric() ? QStringLiteral(" cm") : QStringLiteral(" in"); }
double fromPx(double px) { return metric() ? px * 2.54 / 96.0 : px / 96.0; }
double toPx(double value) { return metric() ? value * 96.0 / 2.54 : value * 96.0; }
double step()
{
    // File > Options > Editing > Ruler snap (0.21); 0 = 2.5 mm / 1/16 in
    const double mm = AppSettings::instance().rulerSnapMm;
    if (mm > 0)
        return mm * 96.0 / 25.4;
    return metric() ? toPx(0.25) : 96.0 / 16.0;
}
QString format(double px) { return QLocale().toString(fromPx(px), 'f', 2) + suffix(); }

} // namespace units

} // namespace qb
