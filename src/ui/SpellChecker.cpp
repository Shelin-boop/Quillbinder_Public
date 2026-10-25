#include "ui/SpellChecker.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QDynamicPropertyChangeEvent>
#include <QTextBlock>
#include <QTextFragment>
#include "core/Addons.h"
#include "core/Paths.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include "AppInfo.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextDocument>
#include <QPointer>

#ifdef QB_HAVE_HUNSPELL
#include <hunspell/hunspell.h>
#endif

namespace qb {

static bool s_enabled = true;
static SpellChecker::Display s_display;
static QList<QPointer<SpellHighlighter>> s_highlighters;

static QStringList dictionaryDirs()
{
    QStringList dirs = {QStringLiteral("/usr/share/hunspell"), QStringLiteral("/usr/share/myspell/dicts"),
                        QStringLiteral("/usr/share/myspell"), QStringLiteral("/usr/local/share/hunspell")};
    dirs << addons::contentDirs(QStringLiteral("dictionaries"));
    dirs << dataDir() + QLatin1String("/dictionaries");
    return dirs;
}

SpellChecker &SpellChecker::instance()
{
    static SpellChecker s;
    return s;
}

SpellChecker::SpellChecker()
{
    QFile f(personalFile());
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : f.readAll().split('\n'))
            if (!line.trimmed().isEmpty())
                m_personal.insert(QString::fromUtf8(line.trimmed()));
    }
}

SpellChecker::~SpellChecker()
{
#ifdef QB_HAVE_HUNSPELL
    if (m_handle)
        Hunspell_destroy(static_cast<Hunhandle *>(m_handle));
#endif
}

QString SpellChecker::personalFile() const
{
    return dataDir() + QLatin1String("/personal-dictionary.txt");
}

// The program's name and the words of its own (the manual and the welcome project use them on every
// page; before 0.15.0 they were all underlined as mistakes).
const QStringList &SpellChecker::programWords()
{
    static const QStringList words = {QStringLiteral(QB_APP_NAME), QStringLiteral("outliner"),
                                      QStringLiteral("outliners"), QStringLiteral("subdocument"), QStringLiteral("subdocuments"),
                                      QStringLiteral("copyholder"), QStringLiteral("copyholders"), QStringLiteral("Ctrl"),
                                      QStringLiteral("Esc")};
    return words;
}

QStringList SpellChecker::availableLanguages() const
{
    QStringList out;
    for (const QString &d : dictionaryDirs()) {
        for (const QString &f : QDir(d).entryList({QStringLiteral("*.dic")}, QDir::Files)) {
            const QString lang = f.left(f.size() - 4);
            if (QFile::exists(QDir(d).filePath(lang + QLatin1String(".aff"))) && !out.contains(lang))
                out << lang;
        }
    }
    out.sort();
    return out;
}

bool SpellChecker::setLanguage(const QString &lang)
{
#ifdef QB_HAVE_HUNSPELL
    if (lang == m_language && m_handle)
        return true;
    for (const QString &d : dictionaryDirs()) {
        const QString aff = QDir(d).filePath(lang + QLatin1String(".aff"));
        const QString dic = QDir(d).filePath(lang + QLatin1String(".dic"));
        if (QFile::exists(aff) && QFile::exists(dic)) {
            if (m_handle)
                Hunspell_destroy(static_cast<Hunhandle *>(m_handle));
            m_handle = Hunspell_create(QFile::encodeName(aff).constData(), QFile::encodeName(dic).constData());
            m_language = lang;
            m_cache.clear();
            for (const QString &w : m_personal)
                Hunspell_add(static_cast<Hunhandle *>(m_handle), w.toUtf8().constData());
            for (const QString &w : programWords())
                Hunspell_add(static_cast<Hunhandle *>(m_handle), w.toUtf8().constData());
            return true;
        }
    }
#else
    Q_UNUSED(lang);
#endif
    return false;
}

bool SpellChecker::isCorrect(const QString &word)
{
#ifdef QB_HAVE_HUNSPELL
    if (!m_handle || word.isEmpty())
        return true;
    if (m_ignored.contains(word) || m_personal.contains(word))
        return true;
    auto it = m_cache.constFind(word);
    if (it != m_cache.constEnd())
        return *it;
    QString w = word;
    w.replace(QChar(0x2019), QLatin1Char('\''));
    const bool ok = Hunspell_spell(static_cast<Hunhandle *>(m_handle), w.toUtf8().constData()) != 0;
    if (m_cache.size() > 50000)
        m_cache.clear();
    m_cache.insert(word, ok);
    return ok;
#else
    Q_UNUSED(word);
    return true;
#endif
}

QStringList SpellChecker::suggestions(const QString &word)
{
    QStringList out;
#ifdef QB_HAVE_HUNSPELL
    if (!m_handle)
        return out;
    char **list = nullptr;
    const int n = Hunspell_suggest(static_cast<Hunhandle *>(m_handle), &list, word.toUtf8().constData());
    for (int i = 0; i < n && i < 8; ++i)
        out << QString::fromUtf8(list[i]);
    Hunspell_free_list(static_cast<Hunhandle *>(m_handle), &list, n);
#else
    Q_UNUSED(word);
#endif
    return out;
}

QStringList SpellChecker::personalWords() const
{
    QStringList out(m_personal.cbegin(), m_personal.cend());
    out.sort(Qt::CaseInsensitive);
    return out;
}

// Writes the list anew; the dictionary is loaded again so that removed words
// count as mistakes again (Hunspell cannot forget single added words reliably).
void SpellChecker::setPersonalWords(const QStringList &words)
{
    m_personal = QSet<QString>(words.cbegin(), words.cend());
    m_cache.clear();
    QDir().mkpath(QFileInfo(personalFile()).absolutePath());
    QFile f(personalFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        for (const QString &w : personalWords())
            f.write(w.toUtf8() + '\n');
    const QString lang = m_language;
#ifdef QB_HAVE_HUNSPELL
    if (m_handle) {
        Hunspell_destroy(static_cast<Hunhandle *>(m_handle));
        m_handle = nullptr;
    }
#endif
    m_language.clear();
    if (!lang.isEmpty())
        setLanguage(lang);
    rehighlightAll();
}

void SpellChecker::addToPersonal(const QString &word)
{
    m_personal.insert(word);
    m_cache.remove(word);
#ifdef QB_HAVE_HUNSPELL
    if (m_handle)
        Hunspell_add(static_cast<Hunhandle *>(m_handle), word.toUtf8().constData());
#endif
    QDir().mkpath(QFileInfo(personalFile()).absolutePath());
    QFile f(personalFile());
    if (f.open(QIODevice::Append))
        f.write(word.toUtf8() + '\n');
    rehighlightAll();
}

void SpellChecker::ignore(const QString &word)
{
    m_ignored.insert(word);
    rehighlightAll();
}

void SpellChecker::attach(QTextDocument *doc)
{
    if (!doc || doc->findChild<SpellHighlighter *>())
        return;
    auto *h = new SpellHighlighter(doc);
    s_highlighters << h;
}

void SpellChecker::setEnabled(bool on)
{
    if (s_enabled == on)
        return;
    s_enabled = on;
    rehighlightAll();
}

bool SpellChecker::isEnabled() { return s_enabled; }

void SpellChecker::setDisplay(const Display &d)
{
    s_display = d;
}

SpellChecker::Display SpellChecker::display() { return s_display; }

void SpellChecker::rehighlightAll()
{
    for (auto &h : s_highlighters)
        if (h)
            h->rehighlight();
}

SpellHighlighter::SpellHighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc)
{
    doc->installEventFilter(this);
}

// The document store announces the zoom as a dynamic property of the document.
bool SpellHighlighter::eventFilter(QObject *o, QEvent *e)
{
    if (o == document() && e->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent *>(e)->propertyName() == "qbZoom")
        QMetaObject::invokeMethod(this, "rehighlight", Qt::QueuedConnection);
    return QSyntaxHighlighter::eventFilter(o, e);
}

void SpellHighlighter::highlightBlock(const QString &text)
{
    const QTextBlock block = currentBlock();
    // Zoom: the document's default font is scaled by the document store.
    // Text with a font size of its own is scaled here, for display only, so
    // that the stored size stays what the writer chose.
    const double zoom = document()->property("qbZoom").toDouble();
    const bool scale = zoom > 0 && !qFuzzyCompare(zoom, 1.0);
    // Links are stored with one blue; on screen they get the theme's link
    // color (a dark paper needs a lighter blue). Display only as well.
    static const QColor storedLink(30, 90, 200);
    const QColor link = theme::color(theme::Role::EditorLink);
    const bool recolor = link.isValid() && link.rgb() != storedLink.rgb();
    const bool plainNotes = s_display.plainAnnotations;
    if (scale || recolor || plainNotes) {
        for (auto fit = block.begin(); !fit.atEnd(); ++fit) {
            const QTextFragment f = fit.fragment();
            if (!f.isValid())
                continue;
            const QTextCharFormat cf = f.charFormat();
            QTextCharFormat shown;
            if (scale && cf.hasProperty(QTextFormat::FontPointSize) && cf.fontPointSize() > 0)
                shown.setFontPointSize(cf.fontPointSize() * zoom);
            if (recolor && cf.isAnchor() && cf.foreground().style() == Qt::SolidPattern
                && cf.foreground().color().rgb() == storedLink.rgb())
                shown.setForeground(link);
            if (plainNotes && NoteKind(cf.intProperty(props::NoteKind)) == NoteKind::InlineAnnotation)
                shown.setForeground(theme::color(theme::Role::EditorText));
            if (!shown.isEmpty())
                setFormat(f.position() - block.position(), f.length(), shown);
        }
    }
    if (!s_enabled || !SpellChecker::instance().isAvailable())
        return;
    // words with digits are one word ("Route66"); numbers alone are never checked
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}\\p{N}][\\p{L}\\p{M}\\p{N}'\\x{2019}]*"));
    QTextCharFormat err;
    err.setUnderlineStyle(s_display.underline == 1   ? QTextCharFormat::DotLine
                          : s_display.underline == 2 ? QTextCharFormat::SingleUnderline
                                                     : QTextCharFormat::SpellCheckUnderline);
    err.setUnderlineColor(theme::color(theme::Role::SpellingError));
    auto it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        QString w = m.captured();
        while (w.endsWith(QLatin1Char('\'')) || w.endsWith(QChar(0x2019)))
            w.chop(1);
        if (w.size() < 2)
            continue;
        bool digits = false, letters = false;
        for (const QChar ch : w)
            (ch.isDigit() ? digits : letters) = true;
        if (!letters || (digits && s_display.ignoreNumbers))
            continue;
        // skip words inside links
        bool skip = false;
        for (auto fit = block.begin(); !fit.atEnd(); ++fit) {
            const QTextFragment f = fit.fragment();
            const int rel = f.position() - block.position();
            if (m.capturedStart() >= rel && m.capturedStart() < rel + f.length()) {
                if (f.charFormat().isAnchor())
                    skip = true;
                break;
            }
        }
        if (skip)
            continue;
        if (!SpellChecker::instance().isCorrect(w)) {
            // keep a zoomed font size under the underline
            const int start = int(m.capturedStart());
            for (int k = 0; k < w.size(); ++k) {
                QTextCharFormat f = format(start + k);
                f.merge(err);
                setFormat(start + k, 1, f);
            }
        }
    }
}

} // namespace qb
