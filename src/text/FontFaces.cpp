#include "text/FontFaces.h"

#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QList>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <cmath>

// Qt's own font list (Qt 6.4.2, the version of the AppImage's runtime; see FontFaces.h)
#include <private/qfont_p.h>
#include <private/qfontdatabase_p.h>
#include <private/qfreetypefontdatabase_p.h>
#include <private/qguiapplication_p.h>
#include <qpa/qplatformfontdatabase.h>
#include <qpa/qplatformintegration.h>

namespace qb {
namespace fontfaces {

namespace {

// families made by separate() since Qt last read its fonts: new family -> family it came from
QHash<QString, QString> s_made;
// install() itself tells the font lists about the new families: not a new reading of the fonts
bool s_telling = false;

bool oneOf(const QString &lowerWord, std::initializer_list<const char *> words)
{
    for (const char *w : words)
        if (lowerWord == QLatin1String(w))
            return true;
    return false;
}

// ---- the named faces of a variable font, read from the font file (nothing is written)
//
// Qt 6.4 lists a variable font file once per named face (fontconfig's index (n + 1) * 65536
// for the n-th face of the font's 'fvar' table) and once as a whole (index 0, no style
// name). Depending on the order in which fontconfig reports them, the whole one takes the
// place of a named face of the same weight, or all named faces of that weight are filed
// under the whole one, which has no name (seen with Source Serif 4: "SmText" drew the face
// of "Regular"; or "Regular", "Caption", "SmText", "Subhead" and "Display" became one face).
// The faces of a variable font are therefore taken from the font itself.

struct NamedFace {
    QString style;          // the face's name in the font ("Banner Bold")
    int index = 0;          // fontconfig's index of the face
    int weight = 400;       // 'wght', else from the name
    int stretch = 0;        // 'wdth' in percent, 0 = not given
    QFont::Style slant = QFont::StyleNormal;
};

quint16 be16(const QByteArray &b, qsizetype at)
{
    if (at < 0 || at + 2 > b.size())
        return 0;
    return quint16((uchar(b[at]) << 8) | uchar(b[at + 1]));
}

quint32 be32(const QByteArray &b, qsizetype at)
{
    if (at < 0 || at + 4 > b.size())
        return 0;
    return (quint32(be16(b, at)) << 16) | be16(b, at + 2);
}

double fixed(const QByteArray &b, qsizetype at) { return double(qint32(be32(b, at))) / 65536.0; }

int weightOfName(const QString &style)
{
    const StyleParts p = parseStyle(style);
    const QString w = p.weight.toLower().remove(QLatin1Char(' '));
    if (w.isEmpty())
        return 400;
    if (oneOf(w, {"thin", "hairline"}))
        return 100;
    if (oneOf(w, {"extralight", "ultralight"}))
        return 200;
    if (w == QLatin1String("light"))
        return 300;
    if (oneOf(w, {"semilight", "demilight"}))
        return 350;
    if (w == QLatin1String("medium"))
        return 500;
    if (oneOf(w, {"semibold", "demibold", "demi"}))
        return 600;
    if (w == QLatin1String("bold"))
        return 700;
    if (oneOf(w, {"extrabold", "ultrabold"}))
        return 800;
    return 900; // heavy, black ...
}

// the named faces of one face of a font file (empty: not a variable font with named faces)
QList<NamedFace> namedFaces(const QString &fileName, int faceIndex)
{
    static QHash<QString, QList<NamedFace>> cache;
    const QString cacheKey = fileName + QLatin1Char('#') + QString::number(faceIndex & 0xffff);
    if (auto it = cache.constFind(cacheKey); it != cache.constEnd())
        return it.value();
    QList<NamedFace> result;
    QFile f(fileName);
    if (f.size() > 0 && f.size() < 256 * 1024 * 1024 && f.open(QIODevice::ReadOnly)) {
        const QByteArray b = f.readAll();
        qsizetype sfnt = 0;
        if (b.startsWith("ttcf")) // a collection: the offset table of this face
            sfnt = be32(b, 12 + 4 * (faceIndex & 0xffff));
        const int tables = be16(b, sfnt + 4);
        qsizetype fvar = -1, name = -1, os2 = -1;
        for (int t = 0; t < tables; ++t) {
            const qsizetype rec = sfnt + 12 + 16 * t;
            const QByteArray tag = b.mid(rec, 4);
            if (tag == "fvar")
                fvar = be32(b, rec + 8);
            else if (tag == "name")
                name = be32(b, rec + 8);
            else if (tag == "OS/2")
                os2 = be32(b, rec + 8);
        }
        if (fvar > 0 && name > 0) {
            // the strings of the name table, Windows English first
            auto nameString = [&](int id) {
                const int n = be16(b, name + 2);
                const qsizetype strings = name + be16(b, name + 4);
                QString best;
                int bestRank = 0;
                for (int r = 0; r < n; ++r) {
                    const qsizetype rec = name + 6 + 12 * r;
                    if (be16(b, rec + 6) != id)
                        continue;
                    const int platform = be16(b, rec), encoding = be16(b, rec + 2), language = be16(b, rec + 4);
                    const qsizetype at = strings + be16(b, rec + 10);
                    const int len = be16(b, rec + 8);
                    if (at + len > b.size())
                        continue;
                    int rank = 0;
                    QString s;
                    if (platform == 3 && (encoding == 1 || encoding == 10)) {
                        for (int i = 0; i + 1 < len; i += 2)
                            s += QChar(be16(b, at + i));
                        rank = language == 0x409 ? 3 : 2;
                    } else if (platform == 1 && encoding == 0) {
                        s = QString::fromLatin1(b.constData() + at, len);
                        rank = 1;
                    }
                    if (rank > bestRank) {
                        bestRank = rank;
                        best = s;
                    }
                }
                return best;
            };
            // the font as a whole is italic (the italic file of a pair)
            const bool fileItalic = os2 > 0 && (be16(b, os2 + 62) & 1);
            const qsizetype axesOffset = be16(b, fvar + 4);
            const int axisCount = be16(b, fvar + 8), axisSize = be16(b, fvar + 10);
            const int instanceCount = be16(b, fvar + 12), instanceSize = be16(b, fvar + 14);
            int wght = -1, wdth = -1, ital = -1, slnt = -1;
            for (int a = 0; a < axisCount; ++a) {
                const QByteArray tag = b.mid(fvar + axesOffset + qsizetype(a) * axisSize, 4);
                if (tag == "wght")
                    wght = a;
                else if (tag == "wdth")
                    wdth = a;
                else if (tag == "ital")
                    ital = a;
                else if (tag == "slnt")
                    slnt = a;
            }
            QSet<QString> seen;
            for (int i = 0; i < instanceCount; ++i) {
                const qsizetype rec = fvar + axesOffset + qsizetype(axisCount) * axisSize + qsizetype(i) * instanceSize;
                if (rec + 4 + 4 * axisCount > b.size())
                    break;
                NamedFace nf;
                nf.style = nameString(be16(b, rec)).trimmed();
                if (nf.style.isEmpty() || seen.contains(nf.style.toLower()))
                    continue;
                seen.insert(nf.style.toLower());
                auto coord = [&](int axis) { return fixed(b, rec + 4 + 4 * axis); };
                nf.index = ((i + 1) << 16) | (faceIndex & 0xffff);
                nf.weight = wght >= 0 ? qBound(1, int(std::lround(coord(wght))), 1000) : weightOfName(nf.style);
                nf.stretch = wdth >= 0 ? qBound(1, int(std::lround(coord(wdth))), 4000) : 0;
                if ((ital >= 0 && coord(ital) >= 0.5) || (ital < 0 && slnt < 0 && fileItalic)
                    || parseStyle(nf.style).italic)
                    nf.slant = QFont::StyleItalic;
                else if (slnt >= 0 && std::fabs(coord(slnt)) > 0.01)
                    nf.slant = QFont::StyleOblique;
                result.append(nf);
            }
        }
    }
    cache.insert(cacheKey, result);
    return result;
}

} // namespace

QString StyleParts::styleInDesign() const
{
    QString s = weight;
    if (italic)
        s = s.isEmpty() ? QStringLiteral("Italic") : s + QStringLiteral(" Italic");
    return s.isEmpty() ? QStringLiteral("Regular") : s;
}

StyleParts parseStyle(const QString &styleName)
{
    StyleParts p;
    QString s = styleName;
    s.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    const QStringList raw = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // "Semi Bold", "Extra Light", "Ultra Condensed" ... count as one word
    QStringList words;
    for (int i = 0; i < raw.size(); ++i) {
        const QString lw = raw[i].toLower();
        if (oneOf(lw, {"semi", "demi", "extra", "ultra"}) && i + 1 < raw.size()) {
            words << raw[i] + raw[i + 1];
            ++i;
        } else {
            words << raw[i];
        }
    }
    QStringList design, weight;
    for (const QString &w : words) {
        const QString lw = w.toLower();
        if (oneOf(lw, {"italic", "oblique", "slanted", "inclined", "kursiv"}))
            p.italic = true;
        else if (oneOf(lw, {"regular", "normal", "roman", "upright", "plain", "book", "standard"}))
            ; // the regular weight
        else if (oneOf(lw, {"thin", "hairline", "extralight", "ultralight", "light", "semilight", "demilight", "medium",
                            "semibold", "demibold", "demi", "bold", "extrabold", "ultrabold", "heavy", "black",
                            "extrablack", "ultrablack"}))
            weight << w;
        else
            design << w;
    }
    p.design = design.join(QLatin1Char(' '));
    p.weight = weight.join(QLatin1Char(' '));
    return p;
}

int separate()
{
    if (!qGuiApp)
        return 0;
    QPlatformIntegration *integration = QGuiApplicationPrivate::platformIntegration();
    // Only the FreeType font lists (Linux, also the offscreen platform of the tests) keep the
    // file and face of a font as a FontFile; nothing is done anywhere else.
    if (!integration || !dynamic_cast<QFreeTypeFontDatabase *>(integration->fontDatabase()))
        return 0;
    (void)QFontDatabase::families(); // makes Qt read its fonts
    QFontDatabasePrivate *d = QFontDatabasePrivate::instance();
    if (!d)
        return 0;

    // one face as it will be registered again
    struct Face {
        QString style;      // name in the font
        StyleParts parts;
        QString foundry;
        int weight;
        QFont::Style slant;
        int stretch;
        bool antialiased;
        QString file;
        int index;
    };
    struct Plan {
        QString family;
        QList<Face> faces;
        QSet<QString> stays;              // lower-case designs the family keeps
        QHash<QString, QString> newName;  // lower-case design -> its new family
        bool fixedPitch;
        QSupportedWritingSystems ws;
    };

    QSet<QString> existing;
    for (int i = 0; i < d->count; ++i)
        existing.insert(d->families[i]->name.toLower());

    QList<Plan> plans;
    for (int i = 0; i < d->count; ++i) {
        QtFontFamily *fam = d->families[i];
        if (s_made.contains(fam->name) || fam->count <= 0)
            continue;
        QList<Face> faces;
        QSet<QString> variableFiles; // file#face of the variable fonts already taken from the file
        bool usable = true;
        for (int j = 0; usable && j < fam->count; ++j) {
            QtFontFoundry *fo = fam->foundries[j];
            for (int k = 0; k < fo->count; ++k) {
                QtFontStyle *st = fo->styles[k];
                // only scalable fonts with a file behind every face
                if (!st->smoothScalable || st->count != 1 || !st->pixelSizes || !st->pixelSizes[0].handle) {
                    usable = false;
                    break;
                }
                const auto *file = static_cast<const FontFile *>(st->pixelSizes[0].handle);
                const QString fileKey = file->fileName + QLatin1Char('#') + QString::number(file->indexValue & 0xffff);
                const QList<NamedFace> named = namedFaces(file->fileName, file->indexValue);
                if (!named.isEmpty()) {
                    // a variable font: all its named faces, once per file
                    if (variableFiles.contains(fileKey))
                        continue;
                    variableFiles.insert(fileKey);
                    for (const NamedFace &nf : named)
                        faces.append({nf.style, parseStyle(nf.style), fo->name, nf.weight, nf.slant,
                                      nf.stretch > 0 ? nf.stretch : int(st->key.stretch), st->antialiased,
                                      file->fileName, nf.index});
                    continue;
                }
                if (st->styleName.isEmpty()) {
                    usable = false; // a face without a name in a plain font: nothing to go by
                    break;
                }
                faces.append({st->styleName, parseStyle(st->styleName), fo->name, int(st->key.weight),
                              QFont::Style(st->key.style), int(st->key.stretch), st->antialiased, file->fileName,
                              file->indexValue});
            }
        }
        if (!usable || faces.isEmpty())
            continue;
        QMap<QString, int> groupSize; // lower-case design -> faces
        QHash<QString, QString> designOf;
        for (const Face &f : std::as_const(faces)) {
            groupSize[f.parts.design.toLower()] += 1;
            designOf.insert(f.parts.design.toLower(), f.parts.design);
        }
        if (groupSize.size() < 2)
            continue;
        // At least two designs with two faces or more: a family whose style names are
        // just words of another language ("Standard", "Fett", "Kursiv") has one face per word.
        int full = 0;
        for (auto it = groupSize.cbegin(); it != groupSize.cend(); ++it)
            if (it.value() >= 2)
                ++full;
        if (full < 2)
            continue;

        // Which design stays in the family: the one its name ends with ("Sitka Text": Text),
        // else the faces without a design word, else a design for running text.
        QString base = fam->name;
        QString home;
        bool hasHome = false;
        int longest = 0;
        for (auto it = designOf.cbegin(); it != designOf.cend(); ++it) {
            const QString &design = it.value();
            if (design.isEmpty() || design.size() <= longest)
                continue;
            if (fam->name.endsWith(QLatin1Char(' ') + design, Qt::CaseInsensitive)) {
                longest = design.size();
                home = it.key();
                hasHome = true;
                base = fam->name.left(fam->name.size() - design.size() - 1).trimmed();
            }
        }
        if (!hasHome && groupSize.contains(QString())) {
            home.clear();
            hasHome = true;
        }
        if (!hasHome)
            for (const char *pref : {"text", "regular", "book", "normal", "roman"})
                if (groupSize.contains(QLatin1String(pref))) {
                    home = QLatin1String(pref);
                    hasHome = true;
                    break;
                }

        Plan plan;
        plan.family = fam->name;
        plan.faces = faces;
        plan.fixedPitch = fam->fixedPitch;
        for (int w = 0; w < QFontDatabase::WritingSystemsCount; ++w)
            if (fam->writingSystems[w] & QtFontFamily::Supported)
                plan.ws.setSupported(QFontDatabase::WritingSystem(w));
        for (auto it = groupSize.cbegin(); it != groupSize.cend(); ++it) {
            const QString design = designOf.value(it.key());
            const QString name = design.isEmpty() ? base : base + QLatin1Char(' ') + design;
            const bool ownName = name.compare(fam->name, Qt::CaseInsensitive) == 0;
            const bool taken = !ownName && existing.contains(name.toLower()); // another font has that name
            // the family keeps its own design (texts saved with its name keep their look),
            // all designs when none can stand for it, and a design whose name is taken
            if (!hasHome || it.key() == home || ownName || taken)
                plan.stays.insert(it.key());
            if (!ownName && !taken) {
                existing.insert(name.toLower());
                plan.newName.insert(it.key(), name);
            }
        }
        if (!plan.newName.isEmpty())
            plans.append(plan);
    }

    int made = 0;
    for (const Plan &plan : std::as_const(plans)) {
        // every face of the family is taken away and registered again from the list above:
        // Qt's own entries of a variable font may be wrong (see namedFaces)
        QtFontFamily *fam = d->family(plan.family, QFontDatabasePrivate::RequestFamily);
        if (!fam)
            continue;
        for (int j = 0; j < fam->count; ++j) {
            QtFontFoundry *fo = fam->foundries[j];
            for (int k = 0; k < fo->count; ++k)
                delete fo->styles[k]; // gives its handle back to Qt
            fo->count = 0;
        }
        // style names inside a new family: "Bold", "Italic" ...; the full name where two would clash
        QHash<QString, int> uses;
        for (const Face &f : plan.faces)
            uses[f.parts.design.toLower() + QLatin1Char('|') + f.parts.styleInDesign().toLower()] += 1;
        for (const Face &f : plan.faces) {
            const QString design = f.parts.design.toLower();
            if (plan.stays.contains(design))
                QPlatformFontDatabase::registerFont(plan.family, f.style, f.foundry, QFont::Weight(f.weight), f.slant,
                                                    QFont::Stretch(f.stretch), f.antialiased, true, 0, plan.fixedPitch, plan.ws,
                                                    new FontFile{f.file, f.index});
            const QString target = plan.newName.value(design);
            if (target.isEmpty())
                continue;
            const QString inDesign = f.parts.styleInDesign();
            const QString styleName = uses.value(design + QLatin1Char('|') + inDesign.toLower()) > 1 ? f.style : inDesign;
            QPlatformFontDatabase::registerFont(target, styleName, f.foundry, QFont::Weight(f.weight), f.slant,
                                                QFont::Stretch(f.stretch), f.antialiased, true, 0, plan.fixedPitch, plan.ws,
                                                new FontFile{f.file, f.index});
        }
        for (const QString &target : plan.newName) {
            s_made.insert(target, plan.family);
            ++made;
        }
        // Qt's matching expects at least one face per foundry
        fam = d->family(plan.family, QFontDatabasePrivate::RequestFamily);
        if (!fam)
            continue;
        int keepFoundries = 0;
        for (int j = 0; j < fam->count; ++j) {
            if (fam->foundries[j]->count == 0)
                delete fam->foundries[j];
            else
                fam->foundries[keepFoundries++] = fam->foundries[j];
        }
        fam->count = keepFoundries;
    }
    if (made > 0) {
        d->fallbacksCache.clear();
        QFontCache::instance()->clear();
    }
    return made;
}

void install()
{
    // QB_KEEP_FONT_FAMILIES=1 leaves Qt's list of fonts as it is (to compare, or if
    // a font ever looks wrong because of the separation)
    if (!qGuiApp || qEnvironmentVariableIntValue("QB_KEEP_FONT_FAMILIES") == 1)
        return;
    separate();
    static bool connected = false;
    if (connected)
        return;
    connected = true;
    // The fonts changed while the program runs: a font added (Qt 6.4 adds it to its list)
    // or removed (Qt reads all fonts anew, without the families made here). Separate again
    // once the list is there, then tell the font lists, which updated themselves before.
    QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp, [] {
        if (s_telling)
            return;
        QTimer::singleShot(0, qGuiApp, [] {
            const QStringList now = QFontDatabase::families();
            for (auto it = s_made.begin(); it != s_made.end();) {
                if (now.contains(it.key()))
                    ++it;
                else
                    it = s_made.erase(it); // gone with the old list
            }
            if (separate() > 0) {
                s_telling = true;
                emit qGuiApp->fontDatabaseChanged();
                s_telling = false;
            }
        });
    });
}

QHash<QString, QString> separatedFamilies()
{
    return s_made;
}

} // namespace fontfaces
} // namespace qb
