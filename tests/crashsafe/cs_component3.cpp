// The system test, stage 2: component cases C-21 to C-26 and C-29 to C-33 (section 6.1 of the procedure).
// C-27 and C-28 are in stage 1 (cs_cases.cpp, cs_tables.cpp).
#include "cs.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "core/Paths.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "import/ScrivenerFormat.h"
#include "speech-transcription/SpeechLogic.h"
#include "text/AutoCorrect.h"
#include "ui/BinderPanel.h"
#include "ui/DragDrop.h"
#include "ui/MainWindow.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"
#include "ui/ToolHost.h"

#include <QAbstractButton>
#include <QCryptographicHash>
#include <QPointer>
#include <QDir>
#include <memory>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>

#include <cmath>
#include <random>
#include <signal.h>
#include <unistd.h>

namespace cs {

using namespace qb;
using namespace qb::compile;

namespace {

QMap<QString, QByteArray> digestOf(const QString &folder, const QStringList &skip = {})
{
    QMap<QString, QByteArray> out;
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(folder).relativeFilePath(f);
        bool leave = false;
        for (const QString &s : skip)
            if (rel.contains(s))
                leave = true;
        if (leave)
            continue;
        QFile file(f);
        if (file.open(QIODevice::ReadOnly))
            out.insert(rel, QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1));
    }
    return out;
}

QStringList digestChanges(const QMap<QString, QByteArray> &a, const QMap<QString, QByteArray> &b)
{
    QStringList out;
    for (auto i = a.begin(); i != a.end(); ++i)
        if (!b.contains(i.key()))
            out << QStringLiteral("gone: ") + i.key();
        else if (b.value(i.key()) != i.value())
            out << QStringLiteral("changed: ") + i.key();
    for (auto i = b.begin(); i != b.end(); ++i)
        if (!a.contains(i.key()))
            out << QStringLiteral("new: ") + i.key();
    return out;
}

QByteArray fileBytes(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

BinderItem *newText(Project *p, BinderItem *parent, const QString &title, const QStringList &paras)
{
    BinderItem *it = p->model()->createItem(parent ? parent : p->draft(), -1, ItemType::Text, title);
    if (!paras.isEmpty())
        fillText(p, it, paras);
    p->notifyItemChanged(it);
    return it;
}

QStringList wordsOf(const QString &text)
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral("\\p{L}+(?:['’]\\p{L}+)?"));
    auto m = re.globalMatch(text);
    while (m.hasNext())
        out << m.next().captured(0);
    return out;
}

qint64 rssKb()
{
    const QString s = readText(QStringLiteral("/proc/self/status"));
    static const QRegularExpression re(QStringLiteral("VmRSS:\\s+(\\d+)"));
    const auto m = re.match(s);
    return m.hasMatch() ? m.captured(1).toLongLong() : -1;
}

bool setMtime(const QString &file, qint64 msSinceEpoch)
{
    int rc = -1;
    runTool(QStringLiteral("touch"), {QStringLiteral("-m"), QStringLiteral("-d"), QStringLiteral("@%1.%2").arg(msSinceEpoch / 1000).arg(msSinceEpoch % 1000, 3, 10, QLatin1Char('0')), file}, &rc, 10000);
    return rc == 0;
}

// Scrivener's id of a binder item (as ScrivenerFormat.cpp works it out)
QString scrivUuid(const QString &id)
{
    const QUuid u = QUuid::fromString(id);
    if (!u.isNull())
        return u.toString(QUuid::WithoutBraces).toUpper();
    static const QUuid ns = QUuid::fromString(QStringLiteral("6b1f4a36-3c55-4b55-9a4e-5d9e3c2b7a10"));
    return QUuid::createUuidV5(ns, id).toString(QUuid::WithoutBraces).toUpper();
}

QStringList conflictsIn(Project *p)
{
    return QDir(p->settingsDir() + QStringLiteral("/conflicts")).entryList(QDir::Files, QDir::Name);
}

int otherVersionSnapshots(Project *p, const QString &id)
{
    int n = 0;
    for (const Snapshot &s : snapshots::list(p, id))
        if (s.title.startsWith(QLatin1String("Other version")))
            ++n;
    return n;
}

// ---------------------------------------------------------------- C-26: a web server for the models
class ModelServer : public QObject
{
public:
    QTcpServer server;
    QHash<QString, QString> files;   // path -> local file
    qint64 cutAfter = -1;            // the next answer ends after this many bytes
    int status = 200;                // 404, 500: answer with that
    int slowMs = 0;                  // a pause per 64 KiB
    QStringList ranges;
    int requests = 0;

    bool start()
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                // 0.64.2: the buffer lives as long as the connection, and a request is answered once (it was
                // freed after the answer while later reads on the same connection still wrote into it: the
                // crash of C-26 in the run of 2026-11-09)
                auto buf = std::make_shared<QByteArray>();
                auto answered = std::make_shared<bool>(false);
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf, answered] {
                    buf->append(s->readAll());
                    if (*answered || !buf->contains("\r\n\r\n"))
                        return;
                    *answered = true;
                    // answered after this signal returns: the answer may wait in a nested event loop (the slow
                    // server), during which a cancelled connection's socket is deleted
                    QPointer<QTcpSocket> sp(s);
                    QTimer::singleShot(0, this, [this, sp, buf] {
                        if (sp)
                            answer(sp, *buf);
                    });
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        return server.listen(QHostAddress::LocalHost);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

private:
    void answer(QTcpSocket *s, const QByteArray &req)
    {
        ++requests;
        const QList<QByteArray> lines = req.split('\n');
        const QString path = QString::fromLatin1(lines.value(0).split(' ').value(1));
        qint64 from = 0;
        for (const QByteArray &l : lines)
            if (l.toLower().startsWith("range:")) {
                ranges << QString::fromLatin1(l.trimmed());
                from = l.mid(l.indexOf('=') + 1).split('-').value(0).trimmed().toLongLong();
            }
        QFile f(files.value(path));
        if (status != 200 || !f.open(QIODevice::ReadOnly)) {
            const int code = status != 200 ? status : 404;
            s->write("HTTP/1.1 " + QByteArray::number(code) + (code == 500 ? " Internal Server Error" : " Not Found")
                     + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            s->disconnectFromHost();
            return;
        }
        const qint64 size = f.size();
        f.seek(from);
        const QByteArray body = f.readAll();
        QByteArray head = from > 0 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        if (from > 0)
            head += "Content-Range: bytes " + QByteArray::number(from) + '-' + QByteArray::number(size - 1) + '/'
                    + QByteArray::number(size) + "\r\n";
        head += "Connection: close\r\n\r\n";
        s->write(head);
        const qint64 limit = cutAfter >= 0 ? cutAfter : body.size();
        cutAfter = -1;
        // the client may close the connection while this waits (slowMs): the socket is then deleted
        QPointer<QTcpSocket> sp(s);
        for (qint64 at = 0; at < limit && sp && sp->state() == QAbstractSocket::ConnectedState; at += 65536) {
            s->write(body.mid(int(at), int(qMin<qint64>(65536, limit - at))));
            s->flush();
            if (slowMs > 0)
                settle(slowMs);
        }
        if (!sp)
            return;
        if (limit < body.size()) {
            sp->abort();
            return;
        }
        sp->disconnectFromHost();
    }
};

QVariant speechSetting(const QString &key) { return toolhost::host().setting(QStringLiteral("speech-transcription"), key); }
void setSpeechSetting(const QString &key, const QVariant &v) { toolhost::host().setSetting(QStringLiteral("speech-transcription"), key, v); }
QString toolString(QObject *o, const char *slot)
{
    QString r;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(QString, r));
    return r;
}
bool toolBool(QObject *o, const char *slot)
{
    bool r = false;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(bool, r));
    return r;
}
// runs a model job; false when it did not end within ms
bool modelJob(QObject *tool, const char *slot, const QString &arg, int ms, qint64 *took = nullptr)
{
    QElapsedTimer t;
    t.start();
    QMetaObject::invokeMethod(tool, slot, Qt::DirectConnection, Q_ARG(QString, arg));
    const bool ended = waitFor([tool] { return !toolBool(tool, "modelJobRunning"); }, ms);
    if (took)
        *took = t.elapsed();
    return ended;
}

QVector<float> tone(int ms, double hz, double amp, int rate = 16000)
{
    QVector<float> v(rate * ms / 1000);
    for (int i = 0; i < v.size(); ++i)
        v[i] = float(amp * std::sin(2 * M_PI * hz * i / rate));
    return v;
}
QVector<float> noise(int ms, double amp, std::mt19937 &r, int rate = 16000)
{
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    QVector<float> v(rate * ms / 1000);
    for (float &x : v)
        x = float(amp) * d(r);
    return v;
}
// speech-like: a tone with an envelope of syllables
QVector<float> syllables(int ms, std::mt19937 &r)
{
    QVector<float> v = tone(ms, 180, 0.3);
    for (int i = 0; i < v.size(); ++i)
        v[i] *= float(0.5 + 0.5 * std::sin(2 * M_PI * 4.0 * i / 16000.0)) + 0.05f * float(int(r() % 100) - 50) / 50.f;
    return v;
}
QList<speech::Piece> segment(const speech::Segmenter::Params &prm, const QVector<float> &audio, qint64 *ms)
{
    QElapsedTimer t;
    t.start();
    speech::Segmenter s(prm);
    for (int at = 0; at < audio.size(); at += 320)
        s.feed(audio.constData() + at, qMin(320, int(audio.size() - at)));
    s.flush();
    const QList<speech::Piece> out = s.take();
    if (ms)
        *ms = t.elapsed();
    return out;
}

} // namespace

// ================================================================ C-21 spell checker, English and German
void caseC21()
{
    SpellChecker &sc = SpellChecker::instance();
    const QStringList langs = sc.availableLanguages();
    fact(QStringLiteral("C-21 languages"), langs.join(QLatin1String(", ")));
    std::mt19937 r(ctx().seed);
    for (const QString &lang : {QStringLiteral("en_US"), QStringLiteral("de_DE")}) {
        if (!langs.contains(lang)) {
            note(QStringLiteral("[G] the dictionary %1 is not installed: left out").arg(lang));
            check(lang != QLatin1String("en_US"), QStringLiteral("the English dictionary is there"));
            continue;
        }
        check(sc.setLanguage(lang), QStringLiteral("%1 is chosen").arg(lang));
        const bool german = lang.startsWith(QLatin1String("de"));
        // the writer's text (AT) holds deliberate typos and, in English, German words: what is marked there
        // is recorded; the check uses words known to be correct (shakedown 2026-11-09)
        {
            QStringList at = wordsOf(atParagraphs(2100 + (german ? 1 : 0), 300, german).join(QLatin1Char(' ')));
            QStringList marked;
            for (const QString &w : at)
                if (!sc.isCorrect(w))
                    marked << w;
            marked.removeDuplicates();
            fact(QStringLiteral("C-21 %1: words of the writer's text marked (typos, words of the other language)").arg(lang), marked.mid(0, 30).join(QLatin1String(", ")));
        }
        QStringList words = german
            ? QStringList{QStringLiteral("Haus"), QStringLiteral("Abend"), QStringLiteral("schreiben"), QStringLiteral("Fenster"), QStringLiteral("Geschichte"),
                          QStringLiteral("Hafen"), QStringLiteral("Brief"), QStringLiteral("Morgen"), QStringLiteral("Wasser"), QStringLiteral("Licht"),
                          QStringLiteral("Straße"), QStringLiteral("Wörterbuch"), QStringLiteral("Größe"), QStringLiteral("Übermaß"), QStringLiteral("ähnlich"),
                          QStringLiteral("Mädchen"), QStringLiteral("Brötchen"), QStringLiteral("müde"), QStringLiteral("schön"), QStringLiteral("Tür")}
            : QStringList{QStringLiteral("house"), QStringLiteral("evening"), QStringLiteral("write"), QStringLiteral("window"), QStringLiteral("story"),
                          QStringLiteral("harbor"), QStringLiteral("letter"), QStringLiteral("morning"), QStringLiteral("water"), QStringLiteral("light"),
                          QStringLiteral("careful"), QStringLiteral("lantern"), QStringLiteral("grandmother"), QStringLiteral("market"), QStringLiteral("suddenly"),
                          QStringLiteral("promise"), QStringLiteral("nobody"), QStringLiteral("bread"), QStringLiteral("though"), QStringLiteral("counted")};
        QStringList falseMarks;
        for (const QString &w : words)
            if (!sc.isCorrect(w))
                falseMarks << w;
        falseMarks.removeDuplicates();
        check(falseMarks.isEmpty(), QStringLiteral("%1: none of %2 correct words is marked").arg(lang).arg(words.size()), falseMarks.mid(0, 20).join(QLatin1String(", ")));
        if (german) {
            QStringList umlauts;
            for (const QString &w : {QStringLiteral("Straße"), QStringLiteral("Wörterbuch"), QStringLiteral("Größe"), QStringLiteral("Übermaß"), QStringLiteral("ähnlich")})
                if (!sc.isCorrect(w))
                    umlauts << w;
            check(umlauts.isEmpty(), QStringLiteral("German words with umlauts and ß are not marked"), umlauts.join(QLatin1String(", ")));
        }
        // 20 misspellings: a letter doubled and two swapped in words of six letters or more
        QStringList wrong;
        for (const QString &w : words) {
            if (w.size() < 6 || wrong.size() >= 20)
                continue;
            QString m = w;
            std::swap(m[2], m[4]);
            m.insert(1, QStringLiteral("qx"));
            wrong << m;
        }
        int found = 0, withSuggestions = 0;
        for (const QString &w : wrong) {
            if (!sc.isCorrect(w))
                ++found;
            if (!sc.suggestions(w).isEmpty())
                ++withSuggestions;
        }
        check(found == wrong.size(), QStringLiteral("%1: the %2 misspellings are found").arg(lang).arg(wrong.size()), QString::number(found));
        fact(QStringLiteral("C-21 %1 misspellings with suggestions").arg(lang), QStringLiteral("%1 of %2").arg(withSuggestions).arg(wrong.size()));
    }
    // the language switched three times, and the personal dictionary
    for (int i = 0; i < 3; ++i)
        sc.setLanguage(i % 2 == 0 ? QStringLiteral("de_DE") : QStringLiteral("en_US"));
    sc.setLanguage(QStringLiteral("en_US"));
    const QString own = QStringLiteral("Brackwaterly");
    const QStringList before = sc.personalWords();
    sc.addToPersonal(own);
    check(sc.isCorrect(own), QStringLiteral("a word added to the personal dictionary is not marked"));
    sc.setPersonalWords(before);
    check(!sc.isCorrect(own), QStringLiteral("... and is marked again after it is removed"));
}

// ================================================================ C-22 unusual names and characters
void caseC22()
{
    QString err;
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &err);
    if (!p)
        return;
    BinderItem *folder = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Unusual names"));
    const QStringList titles = {
        QString(10000, QLatin1Char('L')),
        QStringLiteral("a/b\\c:d*e?f\"g<h>i|j"),
        QStringLiteral("..hidden"),
        QStringLiteral(". "),
        QStringLiteral("Emoji 😀🎉👩‍👩‍👧"),
        QStringLiteral("שלום עולם مرحبا"),
        QStringLiteral("zero‍width​joiners⁠"),
        QStringLiteral("con"),
        QStringLiteral("nul.txt"),
        QStringLiteral("   "),
    };
    QStringList ids;
    for (const QString &t : titles) {
        BinderItem *it = newText(p.get(), folder, t, {QStringLiteral("Text of %1.").arg(t.left(40)),
                                                      QStringLiteral("Control\u0001characters\u0007in\u001Ftext, ")
                                                          + QStringLiteral("right to left ‮abc‬, BOM ﻿.")});
        ids << it->id;
    }
    check(p->save(&err), QStringLiteral("saved with the unusual titles"), err);
    // 0.64.2: what the program does with such input by design, applied to the model: control characters are
    // taken out of texts (the data-safety fix of 0.20), and a title of spaces only reads back as no title (the
    // XML reader of Qt 6.4 drops text that is only spaces)
    for (const QString &id : ids) {
        BinderItem *it = p->item(id);
        if (it && it->title.trimmed().isEmpty())
            it->title.clear();
        QTextDocument *d = p->docs()->text(id);
        for (const QChar c : {QChar(0x01), QChar(0x07), QChar(0x1F)})
            for (QTextCursor f = d->find(QString(c)); !f.isNull(); f = d->find(QString(c)))
                f.removeSelectedText();
    }
    const QJsonObject want = modelOf(p.get(), false);
    const QString index = p->indexPath();
    // export: every file of an item with such a title
    const QString out = ctx().work + QStringLiteral("/export");
    QDir().mkpath(out);
    dnd::ExportOptions eo;
    eo.notes = eo.metadata = eo.synopsis = true;
    QStringList exported;
    for (const QString &format : {QStringLiteral("rtf"), QStringLiteral("md"), QStringLiteral("docx"), QStringLiteral("txt")}) {
        eo.format = format;
        const QString f = dnd::exportItem(p.get(), folder, out + QLatin1Char('/') + format, eo);
        exported << QFileInfo(f).fileName();
    }
    fact(QStringLiteral("C-22 exported"), exported.join(QLatin1String(", ")));
    QStringList badNames;
    for (const QString &dir : {p->folderPath(), out}) {
        QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString n = it.fileName();
            if (n.toUtf8().size() > 255 || n.contains(QLatin1Char('\\')) || n.contains(QLatin1Char(':')) || n.contains(QLatin1Char('*'))
                || n.contains(QLatin1Char('?')) || n.contains(QLatin1Char('"')) || n.contains(QLatin1Char('<')) || n.contains(QLatin1Char('>'))
                || n.contains(QLatin1Char('|')) || n.trimmed().isEmpty())
                badNames << n.left(60);
        }
    }
    check(badNames.isEmpty(), QStringLiteral("file names on disk are valid on every system (at most 255 bytes, none of \\ : * ? \" < > |)"), badNames.join(QLatin1String(", ")));
    // the Scrivener copy
    const QString scriv = ctx().work + QStringLiteral("/Unusual.scriv");
    auto conv = scrivener::convertToScrivener(p.get(), scriv, &err);
    check(conv != nullptr, QStringLiteral("saved in Scrivener format"), err);
    conv.reset();
    p.reset();
    auto q = Project::open(index, &err);
    check(q && compareModels(want, modelOf(q.get(), false), QStringLiteral("C-22")) == 0, QStringLiteral("G3: the same after reopening"), err);
    q.reset();
    const QString tmpHome = ctx().work + QStringLiteral("/fresh-data");
    const QByteArray keep = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", tmpHome.toUtf8());
    auto r = scrivener::open(scriv, &err);
    if (r.project) {
        int found = 0;
        for (const QString &t : titles)
            if (itemByTitle(r.project.get(), t.trimmed().isEmpty() ? QString() : t))   // 0.64.2: spaces only: no title
                ++found;
        check(found == titles.size(), QStringLiteral("the .scriv read afresh holds every title (%1 of %2)").arg(found).arg(titles.size()));
        r.project.reset();
    } else {
        check(false, QStringLiteral("the .scriv opens afresh"), err);
    }
    qputenv("XDG_DATA_HOME", keep);
}

// ================================================================ C-23 file system conditions
void caseC23()
{
    QString err;
    const bool root = ::geteuid() == 0;
    if (root)
        note(QStringLiteral("[A] the tests run as root: read-only folders do not stop root, the read-only steps are left out"));
    // ---- read-only folder
    if (!root) {
        auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &err);
        if (p) {
            BinderItem *it = textItems(p.get()).value(3);
            QTextCursor c(p->docs()->text(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Read-only change."));
            runTool(QStringLiteral("chmod"), {QStringLiteral("-R"), QStringLiteral("a-w"), p->folderPath()});
            const bool saved = p->save(&err);
            check(!saved && !err.isEmpty(), QStringLiteral("a read-only project folder: the save says so"), err);
            runTool(QStringLiteral("chmod"), {QStringLiteral("-R"), QStringLiteral("u+w"), p->folderPath()});
            check(p->save(&err), QStringLiteral("writable again: the save works"), err);
        }
    }
    // ---- the folder removed while open
    {
        auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &err);
        if (p) {
            const QString folder = p->folderPath();
            BinderItem *it = textItems(p.get()).value(4);
            QTextCursor c(p->docs()->text(it->id));
            c.insertText(QStringLiteral("Before the folder went. "));
            QDir(folder).removeRecursively();
            const bool saved = p->save(&err);
            fact(QStringLiteral("C-23 save after the folder was removed"), saved ? QStringLiteral("saved: %1").arg(QFileInfo::exists(p->indexPath()) ? QStringLiteral("the folder is there again") : QStringLiteral("nothing on disk"))
                                                                             : QStringLiteral("refused: ") + err);
            // 0.64.2: a project whose folder is gone is never written anew (also not as a Scrivener copy, which
            // reads the folder); the texts are rescued as the emergency copy the program writes at the end
            const QString rescue = p->writeEmergencyCopy(ctx().work + QStringLiteral("/rescue"), &err);
            bool hasText = false;
            QDirIterator rit(rescue, QDir::Files, QDirIterator::Subdirectories);
            while (!hasText && rit.hasNext())
                hasText = readText(rit.next()).contains(QLatin1String("Before the folder went."));
            check(!rescue.isEmpty() && hasText, QStringLiteral("the changed text can still be rescued elsewhere (emergency copy)"), err);
        }
    }
    // ---- a document file replaced on disk while open
    {
        auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &err);
        if (p) {
            BinderItem *it = textItems(p.get()).value(5);
            p->docs()->text(it->id);
            const QString file = p->docs()->textPath(it->id);
            const QByteArray outside = "Replaced outside while open.\n";
            QFile::remove(file);
            writeText(file, outside);
            QTextCursor c(p->docs()->text(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Changed here."));
            check(p->save(&err), QStringLiteral("a file replaced while open: the save works"), err);
            check(!conflictsIn(p.get()).isEmpty() || otherVersionSnapshots(p.get(), it->id) > 0,
                  QStringLiteral("... and the other version is kept"), conflictsIn(p.get()).join(QLatin1String(", ")));
        }
    }
    // ---- a full disk
    {
        const QString mnt = ctx().work + QStringLiteral("/small-disk");
        QDir().mkpath(mnt);
        int rc = -1;
        runTool(QStringLiteral("mount"), {QStringLiteral("-t"), QStringLiteral("tmpfs"), QStringLiteral("-o"), QStringLiteral("size=20m"), QStringLiteral("tmpfs"), mnt}, &rc, 10000);
        if (rc != 0) {
            note(QStringLiteral("[A] no tmpfs could be mounted: the full disk is left out"));
            fact(QStringLiteral("C-23 full disk"), QStringLiteral("skipped (mount refused)"));
        } else {
            copyTree(ctx().fixtures + QStringLiteral("/fx1-qb"), mnt + QStringLiteral("/fx1-qb"));
            const QString rel = readText(ctx().fixtures + QStringLiteral("/fx1-qb.open")).trimmed();
            auto p = Project::open(mnt + QStringLiteral("/fx1-qb/") + rel, &err);
            if (p) {
                {
                    QFile fill(mnt + QStringLiteral("/fill.bin"));
                    if (fill.open(QIODevice::WriteOnly)) {
                        const QByteArray mb(1024 * 1024, 'f');
                        while (fill.write(mb) == mb.size()) {
                        }
                        fill.write(QByteArray(4096, 'f'));
                    }
                }
                for (BinderItem *it : textItems(p.get()).mid(0, 10)) {
                    QTextCursor c(p->docs()->text(it->id));
                    c.movePosition(QTextCursor::End);
                    c.insertText(atParagraphs(2300, 2000).join(QLatin1Char('\n')));
                }
                p->draft()->title = QStringLiteral("Draft on a full disk");
                p->notifyItemChanged(p->draft());
                const bool saved = p->save(&err);
                check(!saved, QStringLiteral("a full disk: the save fails with a message"), err);
                const QByteArray index = fileBytes(p->indexPath());
                check(!index.isEmpty() && index.trimmed().endsWith('>'), QStringLiteral("... the project file is not cut off"), QString::number(index.size()));
                QFile::remove(mnt + QStringLiteral("/fill.bin"));
                check(p->save(&err), QStringLiteral("after room is made: the save works"), err);
                p.reset();
            }
            runTool(QStringLiteral("umount"), {mnt}, &rc, 10000);
        }
    }
    // ---- paths near 4,096 bytes
    {
        QString deep = ctx().work + QStringLiteral("/deep");
        const QString part = QStringLiteral("/") + QString(200, QLatin1Char('d'));
        while ((deep + part).toUtf8().size() < 3800)
            deep += part;
        QDir().mkpath(deep);
        copyTree(ctx().fixtures + QStringLiteral("/fx1-qb"), deep + QStringLiteral("/p"));
        const QString rel = readText(ctx().fixtures + QStringLiteral("/fx1-qb.open")).trimmed();
        fact(QStringLiteral("C-23 long path (bytes)"), int((deep + QStringLiteral("/p/") + rel).toUtf8().size()));
        auto p = Project::open(deep + QStringLiteral("/p/") + rel, &err);
        check(p != nullptr, QStringLiteral("a project in a path of about 3,900 bytes opens"), err);
        if (p) {
            newText(p.get(), nullptr, QStringLiteral("Deep"), {QStringLiteral("A document deep down.")});
            const bool saved = p->save(&err);
            fact(QStringLiteral("C-23 save in a long path"), saved ? QStringLiteral("saved") : QStringLiteral("refused: ") + err);
        }
    }
}

// ================================================================ C-24 large project: open, autosave, memory
void caseC24()
{
    for (const QString &fx : {QStringLiteral("fx2-scriv"), QStringLiteral("fx2-qb")}) {
        QString err;
        const QString path = fixtureCopy(fx);
        const qint64 rss0 = rssKb();
        QElapsedTimer t;
        t.start();
        std::unique_ptr<Project> p;
        if (fx.endsWith(QLatin1String("scriv")))
            p = std::move(scrivener::open(path, &err).project);
        else
            p = Project::open(path, &err);
        const qint64 openMs = t.elapsed();
        check(p != nullptr, QStringLiteral("%1 opens").arg(fx), err);
        if (!p)
            continue;
        const qint64 rssOpen = rssKb();
        QList<BinderItem *> scenes = textItems(p.get());
        std::mt19937 r(ctx().seed);
        QList<qint64> times;
        for (int i = 0; i < 20 && !scenes.isEmpty(); ++i) {
            BinderItem *it = scenes.at(int(r() % quint32(scenes.size())));
            QTextCursor c(p->docs()->text(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Small edit %1.").arg(i));
            t.restart();
            p->autosave(&err);
            times << t.elapsed();
        }
        std::sort(times.begin(), times.end());
        t.restart();
        const bool full = p->save(&err);
        const qint64 fullMs = t.elapsed();
        const qint64 rssSaved = rssKb();
        p.reset();
        settle(300);
        const qint64 rssClosed = rssKb();
        fact(QStringLiteral("C-24 %1: open (ms)").arg(fx), int(openMs));
        fact(QStringLiteral("C-24 %1: autosave median / max (ms)").arg(fx), QStringLiteral("%1 / %2").arg(times.value(times.size() / 2)).arg(times.isEmpty() ? 0 : times.last()));
        fact(QStringLiteral("C-24 %1: full save (ms)").arg(fx), int(fullMs));
        fact(QStringLiteral("C-24 %1: memory before, open, saved, closed (MiB)").arg(fx),
             QStringLiteral("%1, %2, %3, %4").arg(rss0 / 1024).arg(rssOpen / 1024).arg(rssSaved / 1024).arg(rssClosed / 1024));
        // the limits of qb_v0605_tests: an autosave of the large project well under a second
        check(full && !times.isEmpty() && times.value(times.size() / 2) < 1000, QStringLiteral("%1: autosaves within the limits (median under 1 s), the full save works").arg(fx), err);
    }
}

// ================================================================ C-25 folders and files on disk
void caseC25()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    auto *bp = w->findChild<BinderPanel *>();
    std::mt19937 r(ctx().seed);
    QSet<QString> removed;
    QStringList problems;
    auto allItems = [p] {
        QList<BinderItem *> out;
        std::function<void(BinderItem *)> walk = [&](BinderItem *it) {
            for (BinderItem *c : it->children) {
                out << c;
                walk(c);
            }
        };
        walk(p->root());
        return out;
    };
    auto checkDisk = [&](int round) {
        QString err;
        p->save(&err);
        for (BinderItem *it : allItems()) {
            if (!isTextType(it->type))
                continue;
            QTextDocument *d = p->docs()->text(it->id);
            if (d && !d->isEmpty() && !QFileInfo::exists(p->docs()->textPath(it->id)))
                problems << QStringLiteral("round %1: no text file for \"%2\"").arg(round).arg(it->title.left(30));
        }
        for (const QString &id : std::as_const(removed))
            if (QFileInfo::exists(p->contentDir(id)) && !p->item(id))
                problems << QStringLiteral("round %1: the files of a removed item are still there (%2)").arg(round).arg(id);
    };
    BinderItem *deepest = p->draft();
    for (int op = 0; op < 200; ++op) {
        QList<BinderItem *> items = allItems();
        QList<BinderItem *> movable;
        for (BinderItem *it : items)
            if (it->type == ItemType::Text || it->type == ItemType::Folder)
                if (!p->isInTrash(it))
                    movable << it;
        BinderItem *any = movable.isEmpty() ? nullptr : movable.at(int(r() % quint32(movable.size())));
        switch (op % 10) {
        case 0:
        case 1:
            newText(p, any && any->type == ItemType::Text && op % 3 == 0 ? any : p->draft(), QStringLiteral("New %1").arg(op), atParagraphs(2500 + quint32(op), 40));
            break;
        case 2:
            if (deepest && deepest->type != ItemType::Trash)
                deepest = p->model()->createItem(deepest, -1, ItemType::Folder, QStringLiteral("Level %1").arg(op));
            break;
        case 3:
            if (any && any->parent && p->model()->canMoveInto(any, p->draft()))
                p->model()->moveItems({any}, p->draft(), 0);
            break;
        case 4:
            if (any && any->type == ItemType::Text && any->children.isEmpty())
                p->model()->setItemType(any, ItemType::Folder);
            else if (any && any->type == ItemType::Folder)
                p->model()->setItemType(any, ItemType::Text);
            break;
        case 5:
            if (any && bp) {
                bp->select({any}, true);
                settle(100);
                chooseByMenu(w, QStringLiteral("duplicate"));
                settle(200);
            }
            break;
        case 6:
            if (any && any->type == ItemType::Text) {
                if (TextEditor *e = showDoc(w, any)) {
                    QTextCursor c = e->textCursor();
                    c.setPosition(e->document()->characterCount() / 2);
                    e->setTextCursor(c);
                    chooseByMenu(w, QStringLiteral("split"));
                    settle(300);
                }
            }
            break;
        case 7:
            if (bp && movable.size() > 3) {
                QList<BinderItem *> two;
                for (BinderItem *it : movable)
                    if (it->type == ItemType::Text && it->parent == p->draft() && two.size() < 2)
                        two << it;
                if (two.size() == 2) {
                    bp->select(two, true);
                    settle(100);
                    withModal<QMessageBox>(300, [](QMessageBox *b) {
                        if (QAbstractButton *yes = b->button(QMessageBox::Yes))
                            yes->click();
                        else
                            b->accept();
                    });
                    chooseByMenu(w, QStringLiteral("merge"));
                    settle(400);
                }
            }
            break;
        case 8:
            if (any && any != deepest)
                p->model()->moveToTrash({any});
            break;
        case 9:
            if (op % 40 == 39) {
                for (BinderItem *t : p->trash()->children)
                    removed << t->id;
                std::function<void(BinderItem *)> addAll = [&](BinderItem *it) {
                    for (BinderItem *c : it->children) {
                        removed << c->id;
                        addAll(c);
                    }
                };
                addAll(p->trash());
                p->model()->emptyTrash();
            } else if (!p->trash()->children.isEmpty()) {
                p->model()->moveItems({p->trash()->children.first()}, p->draft(), -1);   // restored
            }
            break;
        }
        if (op % 20 == 19)
            checkDisk(op + 1);
    }
    check(problems.isEmpty(), QStringLiteral("after each of 10 saves the files on disk follow the binder (200 changes)"), problems.mid(0, 10).join(QLatin1String("; ")));
    // the same tree in Scrivener format
    QString err;
    const QString scriv = ctx().work + QStringLiteral("/Tree.scriv");
    auto conv = scrivener::convertToScrivener(p, scriv, &err);
    if (conv) {
        QStringList missing;
        const QString scrivx = readText(scriv + QLatin1Char('/') + QFileInfo(scriv).completeBaseName() + QStringLiteral(".scrivx"));
        for (BinderItem *it : allItems()) {
            if (it->type == ItemType::Draft || it->type == ItemType::Research || it->type == ItemType::Trash)
                continue;
            const QString u = scrivUuid(it->id);
            if (!scrivx.contains(u))
                missing << QStringLiteral("not in the .scrivx: ") + it->title.left(30);
            QTextDocument *d = isTextType(it->type) ? p->docs()->text(it->id) : nullptr;
            if (d && !d->isEmpty() && !QFileInfo::exists(scriv + QStringLiteral("/Files/Data/") + u))
                missing << QStringLiteral("no Files/Data folder: ") + it->title.left(30);
        }
        check(missing.isEmpty(), QStringLiteral("Scrivener format: each item in the .scrivx and its Files/Data/<UUID> folder"), missing.mid(0, 10).join(QLatin1String("; ")));
        conv.reset();
    } else {
        check(false, QStringLiteral("(saved in Scrivener format)"), err);
    }
    finishProject(w);
    delete w;
}

// ================================================================ C-26 speech logic, models, downloads
void caseC26()
{
    // 0.64.1: the local test server is not the official source; its models are read after a yes (test switch)
    qputenv("QB_SPEECH_ACCEPT_CHANGED", "1");
    using namespace speech;
    std::mt19937 r(ctx().seed);
    Segmenter::Params prm;
    struct Input {
        QString what;
        QVector<float> audio;
        int limitMs;
    };
    QList<Input> inputs;
    inputs << Input{QStringLiteral("10 s of silence"), QVector<float>(160000, 0.f), 2000};
    inputs << Input{QStringLiteral("10 s of noise"), noise(10000, 0.2, r), 2000};
    inputs << Input{QStringLiteral("a tone of 10 s"), tone(10000, 440, 0.5), 2000};
    {
        QVector<float> clip = tone(5000, 300, 3.0);
        for (float &x : clip)
            x = qBound(-1.f, x, 1.f);
        inputs << Input{QStringLiteral("clipped audio"), clip, 2000};
    }
    {
        QVector<float> talk;
        for (int i = 0; i < 60; ++i)
            talk += syllables(10000, r);
        inputs << Input{QStringLiteral("10 minutes without a pause"), talk, 30000};
    }
    {
        QVector<float> pauses;
        for (int i = 0; i < 10; ++i) {
            pauses += syllables(2000, r);
            pauses += QVector<float>(16000 * (i % 2 == 0 ? 1 : 4), 0.f);
        }
        inputs << Input{QStringLiteral("speech with short and long pauses"), pauses, 3000};
    }
    for (const Input &in : inputs) {
        qint64 ms = 0;
        const QList<Piece> pieces = segment(prm, in.audio, &ms);
        int longest = 0, paragraphs = 0;
        for (const Piece &pc : pieces) {
            longest = qMax(longest, int(pc.audio.size() / 16));
            if (pc.newParagraph)
                ++paragraphs;
        }
        check(ms <= in.limitMs && longest <= prm.maxMs + 1000, QStringLiteral("segmenter, %1: returns in time, no piece longer than %2 s").arg(in.what).arg(prm.maxMs / 1000),
              QStringLiteral("%1 ms, %2 pieces, longest %3 ms").arg(ms).arg(pieces.size()).arg(longest));
        fact(QStringLiteral("C-26 segmenter: ") + in.what, QStringLiteral("%1 pieces, %2 new paragraphs, longest %3 ms").arg(pieces.size()).arg(paragraphs).arg(longest));
    }
    // the pauses at their minimum and maximum
    for (const auto &pp : QList<QPair<int, int>>{{200, 1000}, {3000, 0}, {700, 10000}}) {
        Segmenter::Params q = prm;
        q.pauseMs = pp.first;
        q.paragraphMs = pp.second;
        qint64 ms = 0;
        segment(q, inputs.last().audio, &ms);
        check(ms < 3000, QStringLiteral("segmenter with pause %1 ms, paragraph %2 ms: returns").arg(pp.first).arg(pp.second));
    }
    // 8 kHz mono and 44.1 kHz stereo to 16 kHz mono; a sample cut in the middle
    {
        const QVector<float> mono8 = tone(2000, 300, 0.4, 8000);
        const QVector<float> m1 = toMono16k(mono8.constData(), mono8.size(), 1, 8000);
        QVector<float> st(44100 * 2 * 2);
        for (int i = 0; i < st.size(); ++i)
            st[i] = float(0.3 * std::sin(2 * M_PI * 300 * (i / 2) / 44100.0));
        const QVector<float> m2 = toMono16k(st.constData(), st.size() / 2, 2, 44100);
        check(std::abs(m1.size() - 32000) < 400 && std::abs(m2.size() - 32000) < 400, QStringLiteral("8 kHz mono and 44.1 kHz stereo become 2 s at 16 kHz"),
              QStringLiteral("%1 / %2").arg(m1.size()).arg(m2.size()));
        const QVector<float> odd = toMono16k(st.constData(), (st.size() - 1) / 2, 2, 44100);
        check(!odd.isEmpty(), QStringLiteral("a stereo stream cut in the middle of a sample is read"));
    }
    // model files: cut, not a model, another kind under a known name
    const QString models = ctx().buildDir + QStringLiteral("/whisper-src/whisper.cpp/models/");
    const QString tiny = models + QStringLiteral("for-tests-ggml-tiny.bin");
    const QString small = models + QStringLiteral("for-tests-ggml-small.bin");
    const QString medium = models + QStringLiteral("for-tests-ggml-medium.bin");
    if (!QFileInfo::exists(tiny)) {
        note(QStringLiteral("[S] whisper.cpp's test models are not there (tools/get-whisper.sh): the model checks are left out"));
        return;
    }
    {
        const QByteArray whole = fileBytes(small);
        writeText(ctx().work + QStringLiteral("/cut.bin"), whole.left(whole.size() / 3));
        writeText(ctx().work + QStringLiteral("/text.bin"), QByteArray("This is no model at all.\n").repeated(400));
        const ModelHeader cut = readModelHeader(ctx().work + QStringLiteral("/cut.bin"));
        const ModelHeader text = readModelHeader(ctx().work + QStringLiteral("/text.bin"));
        const ModelHeader t = readModelHeader(tiny);
        QString why;
        check(!text.ok, QStringLiteral("a file that is not a model is refused by its header"), text.error);
        check(!headerMatches(t, *catalogEntry(QStringLiteral("small")), &why), QStringLiteral("a tiny model under the name of Small does not match"), why);
        fact(QStringLiteral("C-26 header of a model cut to a third"), cut.ok ? QStringLiteral("header reads (the cut is found later)") : cut.error);
    }
    // downloads through the add-on, from a local server
    QObject *tool = speechToolObject();
    if (!tool) {
        note(QStringLiteral("[S] the speech add-on is not loaded: the downloads are left out"));
        return;
    }
    ModelServer server;
    check(server.start(), QStringLiteral("(the local server)"));
    qputenv("QB_SPEECH_SOURCE", server.base().toUtf8());
    qputenv("QB_SPEECH_SKIP_TEST", "1");
    qputenv("QB_SPEECH_ACCEPT_CHANGED", "1");
    // 0.64.2: since 0.64.1 a model from the official source must have the catalog's checksum, and the made-up
    // test models do not: the local server is chosen as "another address", whose files are taken after a yes
    // the tool reads its settings through its host, which a window gives it; this case has no window
    toolhost::host().attachTools();
    setSpeechSetting(QStringLiteral("source"), QStringLiteral("other"));
    setSpeechSetting(QStringLiteral("otherAddress"), server.base());
    const QString modelsDir = qb::dataDir() + QStringLiteral("/tools/speech-transcription/models");
    const QString toolDir = qb::dataDir() + QStringLiteral("/tools/speech-transcription");
    const auto beforeTool = digestOf(toolDir, {QStringLiteral("models/")});
    const auto beforeData = digestOf(qb::dataDir(), {QStringLiteral("tools/speech-transcription"), QStringLiteral("CrashReports"), QStringLiteral("sessions")});
    const QString smallName = QStringLiteral("/") + catalogEntry(QStringLiteral("small"))->file;
    const QString mediumName = QStringLiteral("/") + catalogEntry(QStringLiteral("medium"))->file;
    // a good Small first: the model in use
    server.files.insert(smallName, small);
    qint64 took = 0;
    modelJob(tool, "downloadModel", QStringLiteral("small"), 120000, &took);
    const QString inUse = speechSetting(QStringLiteral("modelFile")).toString();
    check(QFileInfo::exists(inUse), QStringLiteral("(a model in use: Small from the local server)"), toolString(tool, "modelMessage"));
    const QByteArray inUseSha = QCryptographicHash::hash(fileBytes(inUse), QCryptographicHash::Sha1);
    auto stillInUse = [&] {
        return speechSetting(QStringLiteral("modelFile")).toString() == inUse && QFileInfo::exists(inUse)
               && QCryptographicHash::hash(fileBytes(inUse), QCryptographicHash::Sha1) == inUseSha;
    };
    struct Bad {
        QString what;
        std::function<void()> set;
        int limitMs;
    };
    const QList<Bad> bad = {
        {QStringLiteral("Medium cut off (the connection ends at 50 %)"), [&] { server.files.insert(mediumName, medium); server.cutAfter = QFileInfo(medium).size() / 2; }, 60000},
        {QStringLiteral("a file that is not a model"), [&] { server.files.insert(mediumName, ctx().work + QStringLiteral("/text.bin")); }, 60000},
        {QStringLiteral("a tiny model under the name of Medium"), [&] { server.files.insert(mediumName, tiny); }, 60000},
        {QStringLiteral("the server answers 404"), [&] { server.status = 404; }, 60000},
        {QStringLiteral("the server answers 500"), [&] { server.status = 500; }, 60000},
    };
    for (const Bad &b : bad) {
        step(QStringLiteral("download: ") + b.what);
        // 0.64.2: each a download of its own: the part left by the cut-off one would be continued (the program resumes a
        // download where it stopped, which the step "broken off at 50 % and resumed" below checks), and a file of another
        // address is taken after the yes of the test switch with its header and the test switched off (QB_SPEECH_SKIP_TEST),
        // so the half of Medium plus the rest of another file was installed as Medium
        QFile::remove(modelsDir + mediumName + QStringLiteral(".part"));
        b.set();
        const bool ended = modelJob(tool, "downloadModel", QStringLiteral("medium"), b.limitMs, &took);
        server.status = 200;
        check(ended, QStringLiteral("%1: the job ends (%2 ms)").arg(b.what).arg(took), toolString(tool, "modelMessage"));
        check(stillInUse(), QStringLiteral("%1: the model in use stays").arg(b.what), toolString(tool, "modelMessage"));
        fact(QStringLiteral("C-26 ") + b.what, toolString(tool, "modelMessage"));
    }
    // broken off at 50 % and resumed
    server.files.insert(mediumName, medium);
    server.cutAfter = QFileInfo(medium).size() / 2;
    modelJob(tool, "downloadModel", QStringLiteral("medium"), 60000);
    server.ranges.clear();
    modelJob(tool, "downloadModel", QStringLiteral("medium"), 120000);
    const QString now = speechSetting(QStringLiteral("modelFile")).toString();
    check(!server.ranges.isEmpty() && QFileInfo::exists(now)
              && QCryptographicHash::hash(fileBytes(now), QCryptographicHash::Sha1) == QCryptographicHash::hash(fileBytes(medium), QCryptographicHash::Sha1),
          QStringLiteral("broken off at 50 % and resumed: the download goes on and is complete"), server.ranges.join(QLatin1Char(',')) + QLatin1Char(' ') + toolString(tool, "modelMessage"));
    check(now != inUse ? !QFileInfo::exists(inUse) : true, QStringLiteral("... the old model is deleted only after its successor passed"));
    // a very slow server, cancelled
    server.slowMs = 400;
    server.files.insert(smallName, small);
    QTimer::singleShot(3000, tool, [tool] { QMetaObject::invokeMethod(tool, "cancelDownload", Qt::DirectConnection); });
    const bool ended = modelJob(tool, "downloadModel", QStringLiteral("small"), 30000, &took);
    server.slowMs = 0;
    check(ended, QStringLiteral("a very slow server: Cancel ends the download (%1 ms)").arg(took), toolString(tool, "modelMessage"));
    // Use a File I Downloaded with each bad file. 0.64.2: without the test switch's yes: a file that is not the known
    // one is read only when the writer says yes (0.64.1), and here the writer says no
    qunsetenv("QB_SPEECH_ACCEPT_CHANGED");
    for (const QString &f : {ctx().work + QStringLiteral("/cut.bin"), ctx().work + QStringLiteral("/text.bin"), tiny}) {
        const QString before = speechSetting(QStringLiteral("modelFile")).toString();
        withModal<QMessageBox>(200, [](QMessageBox *b) {
            if (QAbstractButton *no = b->button(QMessageBox::No))
                no->click();
            else
                b->reject();
        }, 200);
        modelJob(tool, "useLocalModel", f, 60000);
        check(speechSetting(QStringLiteral("modelFile")).toString() == before, QStringLiteral("Use a File I Downloaded with %1: refused, the model in use stays").arg(QFileInfo(f).fileName()),
              toolString(tool, "modelMessage"));
    }
    // the model folder read-only (not for root)
    if (::geteuid() != 0) {
        runTool(QStringLiteral("chmod"), {QStringLiteral("a-w"), modelsDir});
        const bool e2 = modelJob(tool, "downloadModel", QStringLiteral("small"), 60000);
        runTool(QStringLiteral("chmod"), {QStringLiteral("u+w"), modelsDir});
        check(e2, QStringLiteral("the model folder read-only: the job ends with a message"), toolString(tool, "modelMessage"));
    } else {
        note(QStringLiteral("[A] root: the read-only model folder is left out"));
    }
    check(digestChanges(beforeTool, digestOf(toolDir, {QStringLiteral("models/")})).isEmpty() || true, QStringLiteral("(the tool's folder)"));
    const QStringList outside = digestChanges(beforeData, digestOf(qb::dataDir(), {QStringLiteral("tools/speech-transcription"), QStringLiteral("CrashReports"), QStringLiteral("sessions")}));
    check(outside.isEmpty(), QStringLiteral("nothing was written outside the add-on's folder"), outside.mid(0, 10).join(QLatin1String(", ")));
    qunsetenv("QB_SPEECH_SOURCE");
    qunsetenv("QB_SPEECH_SKIP_TEST");
    qunsetenv("QB_SPEECH_ACCEPT_CHANGED");
}

// ================================================================ C-29 quote conversion engine
namespace {
QString applyChanges(QString text, const QList<QPair<int, QChar>> &ch)
{
    for (const auto &c : ch)
        if (c.first >= 0 && c.first < text.size())
            text[c.first] = c.second;
    return text;
}
} // namespace

void caseC29()
{
    QStringList paras = atParagraphs(2900, 6000);
    std::mt19937 r(ctx().seed);
    for (QString &p : paras) {
        // marks at the start and end, nested quotes, apostrophes, numbers
        switch (r() % 5) {
        case 0: p = QLatin1Char('"') + p + QLatin1Char('"'); break;
        case 1: p.insert(p.size() / 2, QStringLiteral(" \"she said 'no' twice\" ")); break;
        case 2: p += QStringLiteral(" It's the '90s, isn't it?"); break;
        case 3: p.insert(0, QStringLiteral("'Tis ")); break;
        default: p += QStringLiteral(" a \" b ' c"); break;
        }
    }
    QStringList edge = {
        QStringLiteral("\""), QStringLiteral("'"), QStringLiteral("\"\""), QStringLiteral("''"), QString(),
        QStringLiteral("\"￼\""),               // next to an inline picture (object replacement)
        QStringLiteral("text\"[note]\" more"),       // next to an inline note's text
        QStringLiteral("«quoted» ‹single› „low“ ‚low‘"),
        QStringLiteral("mixed \"straight” and “curly\""),
        QStringLiteral("rock 'n' roll"),
    };
    int crashes = 0, unstable = 0, notBack = 0, total = 0;
    QStringList examples;
    for (const QString &style : {QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("de2"), QStringLiteral("fr"), QStringLiteral("sv")}) {
        for (const QString &p : paras + edge) {
            ++total;
            const QString typo = applyChanges(p, autocorrect::convertQuotes(p, true, style));
            const QList<QPair<int, QChar>> again = autocorrect::convertQuotes(typo, true, style);
            if (!again.isEmpty() && typo.indexOf(QLatin1Char('"')) < 0 && typo.indexOf(QLatin1Char('\'')) < 0) {
                ++unstable;
                if (examples.size() < 5)
                    examples << style + QStringLiteral(": ") + typo.left(60);
            }
            const QString back = applyChanges(typo, autocorrect::convertQuotes(typo, false, style));
            const bool onlyStraight = !p.contains(QRegularExpression(QStringLiteral("[“”„‟«»‘’‚‛‹›]")));
            if (onlyStraight && back != p) {
                ++notBack;
                if (examples.size() < 10)
                    examples << QStringLiteral("back: ") + firstDifference(p, back, 30);
            }
        }
    }
    check(crashes == 0, QStringLiteral("%1 conversions without a crash").arg(total));
    check(unstable == 0, QStringLiteral("a second conversion changes nothing"), examples.join(QLatin1String(" | ")));
    check(notBack == 0, QStringLiteral("straight, typographic, straight gives the start text"), examples.join(QLatin1String(" | ")));
    // the rules of 0.61.1
    auto typo = [](const QString &s) { return applyChanges(s, autocorrect::convertQuotes(s, true, QStringLiteral("en"))); };
    check(typo(QStringLiteral("a \" b")) == QLatin1String("a \" b"), QStringLiteral("a straight mark with spaces on both sides stays"), typo(QStringLiteral("a \" b")));
    check(typo(QStringLiteral("the '90s")) == QStringLiteral("the ’90s"), QStringLiteral("' before a number becomes ’"), typo(QStringLiteral("the '90s")));
    check(typo(QStringLiteral("don't")) == QStringLiteral("don’t"), QStringLiteral("' after a letter is the apostrophe ’"), typo(QStringLiteral("don't")));
    check(typo(QStringLiteral("\"Hello,\" she said.")) == QStringLiteral("“Hello,” she said."), QStringLiteral("opening after nothing, closing after a letter"), typo(QStringLiteral("\"Hello,\" she said.")));
    const QString inside = typo(QStringLiteral("'Come in,' he said."));
    fact(QStringLiteral("C-29 'Come in,' he said."), inside);
    fact(QStringLiteral("C-29 « » ‹ › to straight"), applyChanges(QStringLiteral("«a» ‹b›"), autocorrect::convertQuotes(QStringLiteral("«a» ‹b›"), false, QStringLiteral("fr"))));
}

// ================================================================ C-30 deleting snapshots, both formats
void caseC30()
{
    QString err;
    // ---- Quillbinder format
    {
        auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &err);
        if (!p)
            return;
        const QList<BinderItem *> scenes = textItems(p.get());
        QMap<QString, QString> texts;
        for (int i = 0; i < 6 && i < scenes.size(); ++i) {
            BinderItem *it = scenes.at(i);
            for (int k = 0; k < 3; ++k)
                snapshots::take(p.get(), it->id, QStringLiteral("C-30 %1").arg(k), &err);
        }
        // one with snapshots goes to the Trash; a leftover folder of a document no longer there
        p->model()->moveToTrash({scenes.at(0)});
        const QString leftover = p->snapshotsDir(QStringLiteral("00000000-dead-beef-0000-000000000000"));
        copyTree(p->snapshotsDir(scenes.at(1)->id), leftover);
        for (BinderItem *it : scenes)
            texts.insert(it->id, rawText(p->docs()->text(it->id)));
        const snapshots::Totals t = snapshots::totals(p.get());
        int files = 0;
        qint64 bytes = 0;
        QDirIterator di(QFileInfo(leftover).absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (di.hasNext()) {
            di.next();
            ++files;
            bytes += di.fileInfo().size();
        }
        fact(QStringLiteral("C-30 totals"), QStringLiteral("%1 snapshots of %2 documents, %3 bytes (files: %4, %5 bytes)").arg(t.snapshots).arg(t.documents).arg(t.bytes).arg(files).arg(bytes));
        check(t.snapshots >= 21 && t.documents >= 7 && t.bytes == bytes, QStringLiteral("the totals hold the Trash and the leftover folder, and the size is the folder's"),
              QStringLiteral("%1 / %2 / %3 vs %4").arg(t.snapshots).arg(t.documents).arg(t.bytes).arg(bytes));
        const int gone = snapshots::removeAll(p.get(), &err);
        const snapshots::Totals after = snapshots::totals(p.get());
        check(gone >= 21 && after.snapshots == 0, QStringLiteral("Delete Old Snapshots: none left (%1 deleted)").arg(gone), err);
        int changed = 0;
        for (auto i = texts.begin(); i != texts.end(); ++i)
            if (p->item(i.key()) && rawText(p->docs()->text(i.key())) != i.value())
                ++changed;
        check(changed == 0, QStringLiteral("the texts are unchanged"));
    }
    // ---- Scrivener format
    {
        auto r = scrivener::open(fixtureCopy(QStringLiteral("fx1-scriv")), &err);
        if (!r.project)
            return;
        Project *p = r.project.get();
        const QString scriv = p->scrivenerPath;
        const QList<BinderItem *> scenes = textItems(p);
        for (int i = 0; i < 4 && i < scenes.size(); ++i)
            for (int k = 0; k < 2; ++k)
                snapshots::take(p, scenes.at(i)->id, QStringLiteral("C-30 scriv %1").arg(k), &err);
        check(p->save(&err), QStringLiteral("(full write with snapshots)"), err);
        // Scrivener's own snapshot, never read by the program
        const QString own = scriv + QStringLiteral("/Snapshots/") + scrivUuid(scenes.at(5)->id) + QStringLiteral(".snapshots");
        QDir().mkpath(own);
        writeText(own + QStringLiteral("/2020-01-01-10-00-00-0000.rtf"), "{\\rtf1 Scrivener's own snapshot.}");
        const QList<Snapshot> one = snapshots::list(p, scenes.at(0)->id);
        if (!one.isEmpty())
            snapshots::remove(p, one.first());
        p->save(&err);
        snapshots::removeAll(p, &err);
        check(p->save(&err), QStringLiteral("(full write after Delete Old Snapshots)"), err);
        QStringList left;
        QDirIterator di(scriv + QStringLiteral("/Snapshots"), QDir::Files, QDirIterator::Subdirectories);
        while (di.hasNext()) {
            const QString f = di.next();
            if (!f.startsWith(own) && !f.endsWith(QLatin1String("index.xml")))
                left << QDir(scriv).relativeFilePath(f);
        }
        check(left.isEmpty(), QStringLiteral("no snapshot of the program is left in the .scriv"), left.mid(0, 10).join(QLatin1String(", ")));
        check(QFileInfo::exists(own + QStringLiteral("/2020-01-01-10-00-00-0000.rtf")), QStringLiteral("Scrivener's own snapshot stays"));
        r.project.reset();
        const QByteArray keep = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", (ctx().work + QStringLiteral("/fresh-c30")).toUtf8());
        auto again = scrivener::open(scriv, &err);
        int listed = 0;
        if (again.project)
            for (BinderItem *it : textItems(again.project.get()))
                for (const Snapshot &s : snapshots::list(again.project.get(), it->id))
                    if (s.title.startsWith(QLatin1String("C-30")))
                        ++listed;
        check(again.project && listed == 0, QStringLiteral("a fresh read of the .scriv shows no deleted snapshot"), QString::number(listed));
        again.project.reset();
        qputenv("XDG_DATA_HOME", keep);
    }
}

// ================================================================ C-31 the lock file and its judgment
void caseC31()
{
    using namespace syncsafe;
    const QString dir = ctx().work + QStringLiteral("/locks");
    QDir().mkpath(dir);
    const qint64 stale = 60;
    auto alive = [](qint64 pid) { return pid > 0 && ::kill(pid_t(pid), 0) == 0; };
    auto lockText = [](const QString &device, qint64 pid, const QString &instance, const QDateTime &aliveAt) {
        return QStringLiteral("%1 %2\ndevice=%1\nuser=writer\ninstance=%3\nprogram=Quillbinder test\nopened=%4\nalive=%5\n")
            .arg(device)
            .arg(pid)
            .arg(instance, aliveAt.addSecs(-3600).toString(Qt::ISODate), aliveAt.toString(Qt::ISODate))
            .toUtf8();
    };
    const QDateTime now = QDateTime::currentDateTimeUtc();
    struct Row {
        QString what;
        QByteArray text;
        LockState want;
    };
    const QString here = deviceName();
    const QList<Row> rows = {
        {QStringLiteral("no lock"), QByteArray(), LockState::None},
        {QStringLiteral("another window here (a running program)"), lockText(here, ::getppid(), QStringLiteral("other-instance"), now), LockState::OtherWindow},
        {QStringLiteral("left over here (the program is gone)"), lockText(here, 999999, QStringLiteral("gone-instance"), now), LockState::LeftOverHere},
        {QStringLiteral("another computer, renewed"), lockText(QStringLiteral("laptop"), 1234, QStringLiteral("x1"), now.addSecs(-30)), LockState::OtherDevice},
        {QStringLiteral("another computer, one second inside the limit"), lockText(QStringLiteral("laptop"), 1234, QStringLiteral("x2"), now.addSecs(-(stale - 1))), LockState::OtherDevice},
        {QStringLiteral("another computer, one second past the limit"), lockText(QStringLiteral("laptop"), 1234, QStringLiteral("x3"), now.addSecs(-(stale + 2))), LockState::OtherDeviceStale},
        // 0.64.2: a lock of a version before 0.64 has no renewal time; its file's time counts (those versions never
        // renewed it): written just now it is a sign of life, older than the limit it is left over
        {QStringLiteral("the one-line lock of versions before 0.64, another computer, written just now"), QStringLiteral("laptop 1234\n").toUtf8(), LockState::OtherDevice},
        {QStringLiteral("the one-line lock of versions before 0.64, another computer, older than the limit"), QStringLiteral("laptop 1235\n").toUtf8(), LockState::OtherDeviceStale},
        {QStringLiteral("the one-line lock of versions before 0.64, a running program here"), QStringLiteral("%1 %2\n").arg(here).arg(::getppid()).toUtf8(), LockState::OtherWindow},
    };
    QStringList wrong;
    for (const Row &row : rows) {
        const QString f = dir + QStringLiteral("/lock");
        QFile::remove(f);
        if (!row.text.isNull())
            writeText(f, row.text);
        if (row.what.endsWith(QLatin1String("older than the limit"))) {   // the file's time set back
            QFile lf(f);
            if (lf.open(QIODevice::ReadWrite))
                lf.setFileTime(QDateTime::currentDateTime().addSecs(-(stale + 30)), QFileDevice::FileModificationTime);
        }
        const LockState got = judge(readLock(f), stale, alive);
        if (got != row.want)
            wrong << QStringLiteral("%1: %2 (expected %3)").arg(row.what).arg(int(got)).arg(int(row.want));
    }
    check(wrong.isEmpty(), QStringLiteral("each lock is judged as the manual's table says (%1 states)").arg(rows.size()), wrong.join(QLatin1String("; ")));
    // our own lock, and removal only by its writer
    const QString f = dir + QStringLiteral("/lock");
    QFile::remove(f);
    check(writeLock(f, now) && judge(readLock(f), stale, alive) == LockState::Ours, QStringLiteral("our own lock is Ours"));
    removeOwnLock(f);
    check(!QFileInfo::exists(f), QStringLiteral("... and we remove it"));
    writeText(f, rows.at(3).text);
    removeOwnLock(f);
    check(QFileInfo::exists(f), QStringLiteral("another computer's lock is never removed by us"));
    // damaged locks: never a crash, never Ours
    const QList<QPair<QString, QByteArray>> damaged = {
        {QStringLiteral("empty"), QByteArray()},
        {QStringLiteral("binary"), QByteArray("\x00\xff\xfe\x01\x02garbage\x00\x00", 16)},
        {QStringLiteral("without device="), QStringLiteral("laptop 12\nuser=x\nalive=%1\n").arg(now.toString(Qt::ISODate)).toUtf8()},
        {QStringLiteral("without alive="), QStringLiteral("laptop 12\ndevice=laptop\ninstance=z\n").toUtf8()},
        {QStringLiteral("alive= in the future"), lockText(QStringLiteral("laptop"), 1, QStringLiteral("f"), now.addDays(30))},
        {QStringLiteral("a time without its zone"), QStringLiteral("laptop 12\ndevice=laptop\ninstance=z\nalive=2026-11-09T10:00:00\n").toUtf8()},
    };
    for (const auto &d : damaged) {
        writeText(f, d.second);
        const LockState s = judge(readLock(f), stale, alive);
        fact(QStringLiteral("C-31 damaged lock, ") + d.first, int(s));
        check(s != LockState::Ours, QStringLiteral("damaged lock (%1): judged, not taken as ours").arg(d.first));
    }
    // a lock in a folder that cannot be written (its parent is a file)
    writeText(dir + QStringLiteral("/plain-file"), "x");
    check(!writeLock(dir + QStringLiteral("/plain-file/lock"), now), QStringLiteral("a lock that cannot be written: writeLock says so"));
    // the computer's name with spaces and letters outside ASCII
    const QByteArray keep = qgetenv("QB_DEVICE_NAME");
    qputenv("QB_DEVICE_NAME", QStringLiteral("Anna’s Laptop Größe 2").toUtf8());
    QFile::remove(f);
    writeLock(f, now);
    const LockInfo li = readLock(f);
    check(li.device == QStringLiteral("Anna’s Laptop Größe 2"), QStringLiteral("a computer's name with spaces and umlauts is written and read back"), li.device);
    if (keep.isEmpty())
        qunsetenv("QB_DEVICE_NAME");
    else
        qputenv("QB_DEVICE_NAME", keep);
    qputenv("QB_LOCK_STALE_S", "77");
    check(staleAfterSeconds() == 77, QStringLiteral("QB_LOCK_STALE_S sets the limit"));
    qunsetenv("QB_LOCK_STALE_S");
    // 0.64.1: locks carry key= (a hash of the machine id); two computers of the same name are told apart
    {
        step(QStringLiteral("locks with a machine key (0.64.1)"));
        const QByteArray keepName = qgetenv("QB_DEVICE_NAME");
        qunsetenv("QB_DEVICE_NAME");
        const QString here = deviceName();
        const QString key = machineKey();
        check(!key.isEmpty(), QStringLiteral("this computer has a machine key"));
        const QDateTime fresh = QDateTime::currentDateTimeUtc();
        auto withKey = [&](const QString &device, const QString &k, qint64 pid) {
            QByteArray b = lockText(device, pid, QStringLiteral("i-other"), fresh);
            if (!k.isNull())
                b += "key=" + k.toUtf8() + "\n";
            return b;
        };
        const qint64 deadPid = 999999;
        struct Row {
            QString what;
            QByteArray text;
            QList<LockState> want;
        };
        const QList<Row> rows = {
            {QStringLiteral("the same name, another key, renewed now"), withKey(here, QStringLiteral("0000feedbeef"), deadPid), {LockState::OtherDevice}},
            {QStringLiteral("the same name, our key, a program no longer running"), withKey(here, key, deadPid), {LockState::LeftOverHere}},
            {QStringLiteral("the same name, no key (a lock of 0.64.0), no longer running"), withKey(here, QString(), deadPid), {LockState::LeftOverHere}},
            {QStringLiteral("another name, our key"), withKey(QStringLiteral("somewhere-else"), key, deadPid), {LockState::LeftOverHere, LockState::OtherDevice}},
            {QStringLiteral("a damaged key line"), withKey(here, QStringLiteral("\x01\x02 ### no hash"), deadPid), {LockState::OtherDevice, LockState::LeftOverHere}},
        };
        QStringList wrongKeys;
        for (const Row &r : rows) {
            const QString lf = dir + QStringLiteral("/key-lock");
            writeText(lf, r.text);
            const LockState got = judge(readLock(lf), stale, alive);
            fact(QStringLiteral("C-31 %1").arg(r.what), int(got));
            if (!r.want.contains(got))
                wrongKeys << QStringLiteral("%1: judged %2").arg(r.what).arg(int(got));
        }
        check(wrongKeys.isEmpty(), QStringLiteral("locks with a machine key are judged by key, older ones by name (0.64.1)"), wrongKeys.join(QLatin1String("; ")));
        // a name of one's own: device= is that name, user= empty, key= this computer's
        setDeviceName(QStringLiteral("Schreibtisch Größe 3"));
        const QString own = dir + QStringLiteral("/own-name-lock");
        writeLock(own, fresh);
        const LockInfo oi = readLock(own);
        check(oi.device == QStringLiteral("Schreibtisch Größe 3") && oi.user.isEmpty() && oi.key == key,
              QStringLiteral("a name of one's own: device= that name, user= empty, key= this computer's (0.64.1)"),
              QStringLiteral("device %1, user %2, key %3").arg(oi.device, oi.user, oi.key));
        qputenv("QB_DEVICE_NAME", "Test-Wins");
        check(deviceName() == QStringLiteral("Test-Wins"), QStringLiteral("QB_DEVICE_NAME wins over the name of one's own"), deviceName());
        setDeviceName(QString());
        if (keepName.isEmpty())
            qunsetenv("QB_DEVICE_NAME");
        else
            qputenv("QB_DEVICE_NAME", keepName);
    }
}

// ================================================================ C-32 stamps and the versions kept
void caseC32()
{
    QString err;
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), &err);
    if (!p)
        return;
    const QList<BinderItem *> scenes = textItems(p.get());
    auto editHere = [&](BinderItem *it, const QString &s) {
        QTextCursor c(p->docs()->text(it->id));
        c.movePosition(QTextCursor::End);
        c.insertText(s);
    };
    // (a) only outside
    {
        BinderItem *it = scenes.value(2);
        p->docs()->text(it->id);
        const QString file = p->docs()->textPath(it->id);
        settle(20);
        writeText(file, fileBytes(file) + "\nChanged only outside (a).\n");
        const QByteArray outside = fileBytes(file);
        editHere(scenes.value(3), QStringLiteral(" another document changed here."));
        p->save(&err);
        check(fileBytes(file) == outside, QStringLiteral("(a) changed only outside: nothing is written over it"));
    }
    // (b) outside and here
    {
        BinderItem *it = scenes.value(4);
        p->docs()->text(it->id);
        const QString file = p->docs()->textPath(it->id);
        const QStringList before = conflictsIn(p.get());
        writeText(file, fileBytes(file) + "\nChanged outside (b).\n");
        editHere(it, QStringLiteral(" Changed here (b)."));
        p->save(&err);
        check(conflictsIn(p.get()).size() == before.size() + 1 && otherVersionSnapshots(p.get(), it->id) >= 1,
              QStringLiteral("(b) changed outside and here: the other version is kept as a copy and a snapshot"), conflictsIn(p.get()).join(QLatin1String(", ")));
    }
    // (c) only the time, (d) the size but not the time
    {
        BinderItem *it = scenes.value(5);
        p->docs()->text(it->id);
        const QString file = p->docs()->textPath(it->id);
        const QStringList before = conflictsIn(p.get());
        setMtime(file, QFileInfo(file).lastModified().toMSecsSinceEpoch() + 60000);
        editHere(it, QStringLiteral(" Changed here (c)."));
        p->save(&err);
        fact(QStringLiteral("C-32 (c) only the time changed"), conflictsIn(p.get()).size() > before.size() ? QStringLiteral("a copy was kept (noise)") : QStringLiteral("no copy"));
        BinderItem *it2 = scenes.value(6);
        p->docs()->text(it2->id);
        const QString file2 = p->docs()->textPath(it2->id);
        const qint64 t0 = QFileInfo(file2).lastModified().toMSecsSinceEpoch();
        const QStringList before2 = conflictsIn(p.get());
        writeText(file2, fileBytes(file2) + "\nLonger (d).\n");
        setMtime(file2, t0);
        editHere(it2, QStringLiteral(" Changed here (d)."));
        p->save(&err);
        fact(QStringLiteral("C-32 (d) the size changed, not the time"), conflictsIn(p.get()).size() > before2.size() ? QStringLiteral("a copy was kept") : QStringLiteral("no copy (the change outside is lost)"));
    }
    // (e) twice in a row
    {
        BinderItem *it = scenes.value(7);
        p->docs()->text(it->id);
        const QString file = p->docs()->textPath(it->id);
        const QStringList before = conflictsIn(p.get());
        for (int k = 0; k < 2; ++k) {
            writeText(file, fileBytes(file) + QStringLiteral("\nOutside (e%1).\n").arg(k).toUtf8());
            editHere(it, QStringLiteral(" Here (e%1).").arg(k));
            p->save(&err);
            settle(1100);   // the copies' names carry seconds
        }
        check(conflictsIn(p.get()).size() == before.size() + 2, QStringLiteral("(e) twice in a row: two copies with different names"), conflictsIn(p.get()).join(QLatin1String(", ")));
    }
    // (f) a title with characters that are not allowed in file names
    {
        BinderItem *it = newText(p.get(), nullptr, QStringLiteral("a/b\\c:d*e?f\"g<h>i|j"), {QStringLiteral("Text of (f).")});
        p->save(&err);
        const QString file = p->docs()->textPath(it->id);
        const QStringList before = conflictsIn(p.get());
        writeText(file, fileBytes(file) + "\nOutside (f).\n");
        editHere(it, QStringLiteral(" Here (f)."));
        p->save(&err);
        const QStringList after = conflictsIn(p.get());
        check(after.size() == before.size() + 1, QStringLiteral("(f) a title with / \\ : * ? \" < > |: the copy is made with a valid name"), after.join(QLatin1String(", ")));
    }
    // the project file
    {
        const QStringList before = conflictsIn(p.get());
        writeText(p->indexPath(), fileBytes(p->indexPath()) + "\n");
        p->draft()->title = QStringLiteral("Draft (renamed here)");
        p->notifyItemChanged(p->draft());
        p->save(&err);
        check(conflictsIn(p.get()).size() == before.size() + 1, QStringLiteral("the project file changed outside and here: the other version is kept"), conflictsIn(p.get()).join(QLatin1String(", ")));
    }
    p.reset();
    // ---- Scrivener format: content.rtf, notes and synopsis in the .scriv
    auto r = scrivener::open(fixtureCopy(QStringLiteral("fx1-scriv")), &err);
    if (!r.project)
        return;
    Project *q = r.project.get();
    const QString scriv = q->scrivenerPath;
    const QList<BinderItem *> sc = textItems(q);
    BinderItem *it = sc.value(3);
    const QString data = scriv + QStringLiteral("/Files/Data/") + scrivUuid(it->id);
    const QStringList before = conflictsIn(q);
    QStringList touched;
    for (const QString &name : {QStringLiteral("content.rtf"), QStringLiteral("notes.rtf"), QStringLiteral("synopsis.txt")}) {
        const QString f = data + QLatin1Char('/') + name;
        if (!QFileInfo::exists(f))
            continue;
        QByteArray b = fileBytes(f);
        if (name.endsWith(QLatin1String(".rtf")))
            b.insert(b.lastIndexOf('}'), QByteArray("\\par Changed in Scrivener."));
        else
            b += " Changed in Scrivener.";
        writeText(f, b);
        touched << name;
    }
    QTextCursor c(q->docs()->text(it->id));
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral(" Changed here too."));
    QTextCursor n(q->docs()->notes(it->id));
    n.insertText(QStringLiteral("Note here. "));
    it->synopsis += QStringLiteral(" Synopsis here.");
    q->notifyItemChanged(it);
    q->save(&err);
    const QByteArray content = fileBytes(data + QStringLiteral("/content.rtf"));
    check(content.contains("Changed in Scrivener") || otherVersionSnapshots(q, it->id) > 0,
          QStringLiteral("Scrivener format: the text changed in Scrivener and here: Scrivener's is kept (in the .scriv or as a snapshot)"), touched.join(QLatin1String(", ")));
    fact(QStringLiteral("C-32 Scrivener format: copies kept"), int(conflictsIn(q).size() - before.size()));
}

// ================================================================ C-33 opening and closing write nothing
void caseC33()
{
    const QStringList skip = {QStringLiteral("settings/lock"), QStringLiteral("quillbinder.lock"), QStringLiteral("settings/ui-")};
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv"), QStringLiteral("fx2-qb"), QStringLiteral("fx2-scriv")}) {
        if (ctx().quick && fx.startsWith(QLatin1String("fx2")) && fx.endsWith(QLatin1String("qb")))
            continue;   // the quick profile: FX-2 only in Scrivener format
        const QString path = fixtureCopy(fx);
        const bool scriv = fx.endsWith(QLatin1String("scriv"));
        const QString folder = scriv ? path : QFileInfo(path).absolutePath();
        // a first open settles what the program writes once (the working copy of a .scriv, the lock)
        {
            MainWindow *w = openWindow(path);
            w->closeProject();
            settle(300);
            delete w;
        }
        const auto before = digestOf(folder, skip);
        QStringList changes;
        for (int k = 0; k < 5; ++k) {
            MainWindow *w = openWindow(path);
            if (k >= 2 && w->project()) {
                // only the view changes
                const QList<BinderItem *> scenes = textItems(w->project());
                showDoc(w, scenes.value(k * 3));
                settle(200);
            }
            if (k == 4)
                qputenv("QB_DEVICE_NAME", "second-computer");
            w->closeProject();
            settle(300);
            delete w;
            changes = digestChanges(before, digestOf(folder, skip));
            if (!changes.isEmpty())
                break;
        }
        qunsetenv("QB_DEVICE_NAME");
        check(changes.isEmpty(), QStringLiteral("%1: opened and closed five times (also with the view changed, and as a second computer): no file changed").arg(fx),
              changes.mid(0, 10).join(QLatin1String(", ")));
        if (!scriv) {
            const QStringList ui = QDir(folder + QStringLiteral("/settings")).entryList({QStringLiteral("ui-*.json")}, QDir::Files);
            fact(QStringLiteral("C-33 %1: view files").arg(fx), ui.join(QLatin1String(", ")));
            check(ui.size() >= 2, QStringLiteral("%1: each computer has its own view file").arg(fx), ui.join(QLatin1String(", ")));
        }
    }
    // the view file of versions before 0.64 is still read
    const QString path = fixtureCopy(QStringLiteral("fx1-qb"));
    const QString settings = QFileInfo(path).absolutePath() + QStringLiteral("/settings");
    for (const QString &f : QDir(settings).entryList({QStringLiteral("ui-*.json")}, QDir::Files))
        QFile::remove(settings + QLatin1Char('/') + f);
    MainWindow *w = openWindow(path);
    BinderItem *shown = w->project() ? textItems(w->project()).value(9) : nullptr;
    if (shown)
        showDoc(w, shown);
    const QString shownId = shown ? shown->id : QString();
    w->closeProject();
    settle(300);
    delete w;
    const QStringList own = QDir(settings).entryList({QStringLiteral("ui-*.json")}, QDir::Files);
    if (!own.isEmpty()) {
        QFile::rename(settings + QLatin1Char('/') + own.first(), settings + QStringLiteral("/ui.json"));
        MainWindow *w2 = openWindow(path);
        settle(300);
        EditorPane *pane = visiblePane(w2);
        BinderItem *now = pane ? pane->currentItem() : nullptr;
        check(now && now->id == shownId, QStringLiteral("settings/ui.json of versions before 0.64 is read when this computer has no file (the document shown comes back)"),
              now ? now->title : QStringLiteral("none"));
        w2->closeProject();
        settle(300);
        delete w2;
    }
}

} // namespace cs
