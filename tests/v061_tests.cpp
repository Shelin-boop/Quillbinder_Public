// Scripted run for version 0.61: the tool interface 1.1 (tabs of writing tools in the inspector,
// buttons of the main toolbar with a menu beside them, a section of their own in File > Options;
// a tool built for 1.0 still works), Undo and Redo in the standard toolbar, and the add-on
// "Speech Transcription": its parts (models and their file headers, the choice of language,
// pauses and pieces, text, files), the package, the button and its menu, the inspector tab, the
// options, the model download with its checks (from a local test server: an interrupted download
// that goes on, a wrong model behind the name, a changed file, a file of the writer's own, the old
// model removed only after the new one is in place), a graphics card through Vulkan (Mesa's
// lavapipe stands in for one), recordings from a WAV file and from a microphone (a PulseAudio
// pipe source) with pieces, paragraphs and the transcription file.
// Real recognition needs a real model: QB_SPEECH_MODEL_FILE=<ggml model> (else those checks are
// reported as not run). Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v061_tests /tmp/shots
#include "AppInfo.h"
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/Paths.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/ToolHost.h"
#include "ui/ToolbarConfig.h"

#include "speech-transcription/SpeechLogic.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QCheckBox>
#include <QRadioButton>
#include <QClipboard>
#include <QScrollBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QStyleFactory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QThread>
#include <QToolBar>
#include <QToolButton>
#include <QtEndian>
#include <atomic>
#include <cmath>
#include <cstdio>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}
static void note(const QString &what)
{
    std::printf("NOTE  %s\n", qPrintable(what));
    std::fflush(stdout);
}

static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

template <typename F>
static bool waitFor(F cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(20);
    }
    return cond();
}

static QString readFile(const QString &f)
{
    QFile file(f);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

static QString sha1(const QString &f)
{
    QFile file(f);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&file);
    return QString::fromLatin1(h.result().toHex());
}

// ------------------------------------------------------------------ test audio

// 16 kHz mono: "speech" (noise and a tone, loud) and pauses (very quiet noise), in milliseconds
static QVector<float> makeAudio(const QList<QPair<bool, int>> &parts)
{
    QVector<float> out;
    quint32 seed = 12345;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return (int((seed >> 9) & 0xffff) - 32768) / 32768.f;
    };
    for (const auto &p : parts) {
        const int n = p.second * 16;
        for (int i = 0; i < n; ++i) {
            const float t = float(out.size()) / 16000.f;
            out << (p.first ? 0.25f * std::sin(2 * float(M_PI) * 220.f * t) + 0.15f * rnd() : 0.0008f * rnd());
        }
    }
    return out;
}

static bool writeWav(const QString &file, const QVector<float> &s)
{
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const quint32 data = quint32(s.size()) * 2;
    QByteArray h("RIFF");
    auto u32 = [&h](quint32 v) { char b[4]; qToLittleEndian(v, b); h.append(b, 4); };
    auto u16 = [&h](quint16 v) { char b[2]; qToLittleEndian(v, b); h.append(b, 2); };
    u32(36 + data);
    h.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(16000);
    u32(32000);
    u16(2);
    u16(16);
    h.append("data");
    u32(data);
    f.write(h);
    QByteArray pcm(int(data), Qt::Uninitialized);
    for (int i = 0; i < s.size(); ++i)
        qToLittleEndian(qint16(qBound(-32767, int(s[i] * 32767), 32767)), pcm.data() + 2 * i);
    f.write(pcm);
    return true;
}

// ------------------------------------------------------------------ a small web server for the downloads

class TestServer : public QObject
{
public:
    QTcpServer server;
    QHash<QString, QString> files;       // path -> local file
    qint64 cutAfter = -1;                // the next answer ends after this many bytes (an interrupted download)
    QStringList ranges;                  // the Range headers asked for
    int requests = 0;

    bool start()
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto *buf = new QByteArray;
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
                    buf->append(s->readAll());
                    if (!buf->contains("\r\n\r\n"))
                        return;
                    answer(s, *buf);
                    delete buf;
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
        if (!f.open(QIODevice::ReadOnly)) {
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
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
        if (cutAfter >= 0) {
            s->write(body.left(int(cutAfter)));
            cutAfter = -1;
            s->flush();
            s->abort();
            return;
        }
        s->write(body);
        s->disconnectFromHost();
    }
};

// ------------------------------------------------------------------ the tool through its slots

static QObject *speechTool()
{
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        if (t->id() == QLatin1String("speech-transcription"))
            return dynamic_cast<QObject *>(t);
    return nullptr;
}

static bool callBool(QObject *o, const char *slot)
{
    bool r = false;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(bool, r));
    return r;
}
static QString callString(QObject *o, const char *slot)
{
    QString r;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(QString, r));
    return r;
}

static QVariant speechSetting(const QString &key)
{
    return toolhost::host().setting(QStringLiteral("speech-transcription"), key);
}
static void setSpeechSetting(const QString &key, const QVariant &v)
{
    toolhost::host().setSetting(QStringLiteral("speech-transcription"), key, v);
}

static bool runModelJob(QObject *tool, const char *slot, const QString &arg, int ms = 120000)
{
    QMetaObject::invokeMethod(tool, slot, Qt::DirectConnection, Q_ARG(QString, arg));
    return waitFor([tool] { return !callBool(tool, "modelJobRunning"); }, ms);
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8()); // the default folder of the transcriptions is ~/Documents/...
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("QB_NO_DIALOGS", "1");
    qputenv("GGML_VK_VISIBLE_DEVICES", "0");   // Mesa's lavapipe (a Vulkan driver on the processor) counts as a card
    qputenv("QB_SPEECH_FAST", "1");            // recordings from a file run faster than real time
    const QString buildDir = QStringLiteral(QB_TEST_BUILD_DIR);
    // tools for the developers' plugin folder: the 1.0 tool of this test
    qputenv("QB_PLUGIN_PATH", (buildDir + QStringLiteral("/tests/tool10-plugins")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV061Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV061Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.61.0")) >= 0,
          QStringLiteral("version 0.61.0 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));

    // ================================================================ the parts of the add-on, directly
    using namespace speech;
    {
        const QList<CatalogEntry> cat = catalog();
        bool sums = cat.size() == 4;
        for (const CatalogEntry &e : cat)
            sums = sums && e.sha1.size() == 40 && e.sha256.size() == 64 && e.file.startsWith(QLatin1String("ggml-")) && !e.impact.isEmpty();
        check(sums && recommendedModel() == QLatin1String("medium") && catalogEntry(QStringLiteral("medium"))
                  && !catalogEntry(QStringLiteral("large-v3-turbo"))->translates && catalogEntry(QStringLiteral("large-v3"))->translates,
              QStringLiteral("models: Small, Medium (recommended), Large v3, Large v3 Turbo (cannot translate), each with "
                             "file, SHA-1 and a note on its impact"));
        const CatalogEntry *m = catalogEntry(QStringLiteral("medium"));
        check(downloadUrl(*m) == QLatin1String("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium-q5_0.bin")
                  && downloadUrl(*m, QStringLiteral("https://mirror.example/models/")) == QLatin1String("https://mirror.example/models/ggml-medium-q5_0.bin")
                  && downloadUrl(*m, QStringLiteral("https://x.example/a/model.bin")) == QLatin1String("https://x.example/a/model.bin"),
              QStringLiteral("download address: whisper.cpp's files on Hugging Face; another folder; another file"));
        // 0.64.1 (audit X-8): https only (http only on this computer); SHA-256 decides
        check(addressAllowed(QUrl(QStringLiteral("https://mirror.example/m.bin"))) && !addressAllowed(QUrl(QStringLiteral("http://mirror.example/m.bin")))
                  && addressAllowed(QUrl(QStringLiteral("http://127.0.0.1:8080/m.bin"))) && addressAllowed(QUrl(QStringLiteral("http://localhost/m")))
                  && !addressAllowed(QUrl(QStringLiteral("ftp://mirror.example/m.bin"))) && !addressAllowed(QUrl(QStringLiteral("file:///tmp/m.bin")))
                  && !addressAllowed(QUrl(QStringLiteral("https:///nohost"))),
              QStringLiteral("0.64.1: models come from https:// addresses only (http only from this computer)"));
        const CatalogEntry *sm = catalogEntry(QStringLiteral("small"));
        check(sumsMatch(*sm, sm->sha1, sm->sha256) && !sumsMatch(*sm, sm->sha1, QString(64, QLatin1Char('0')))
                  && catalogEntryBySums(QString(), sm->sha256) && catalogEntryBySums(QString(), sm->sha256)->id == sm->id
                  && !catalogEntryBySums(sm->sha1, QString(64, QLatin1Char('1'))),
              QStringLiteral("0.64.1: a model is known by its SHA-256 (the SHA-1 alone no longer counts)"));
    }
    const QString wsrc = buildDir + QStringLiteral("/../build/whisper-src/whisper.cpp/models/");
    const QString testModelDir = QFileInfo::exists(wsrc) ? wsrc : buildDir + QStringLiteral("/whisper-src/whisper.cpp/models/");
    const QString tinyFile = testModelDir + QStringLiteral("for-tests-ggml-tiny.bin");
    const QString mediumFile = testModelDir + QStringLiteral("for-tests-ggml-medium.bin");
    const QString smallFile = testModelDir + QStringLiteral("for-tests-ggml-small.bin");
    {
        const ModelHeader t = readModelHeader(tinyFile), md = readModelHeader(mediumFile);
        QString why;
        const bool wrong = !headerMatches(t, *catalogEntry(QStringLiteral("medium")), &why);
        check(t.ok && t.kind() == QLatin1String("tiny") && md.ok && md.kind() == QLatin1String("medium")
                  && headerMatches(md, *catalogEntry(QStringLiteral("medium"))) && wrong && why.contains(QLatin1String("tiny"))
                  && md.translates(),
              QStringLiteral("model header: whisper.cpp's empty test models read as \"tiny\" and \"medium\"; a tiny model "
                             "behind the name of Medium is refused"),
              QStringLiteral("%1 %2 %3").arg(t.kind(), md.kind(), why));
        const ModelHeader none = readModelHeader(QStringLiteral(QB_TEST_SOURCE_DIR "/../README.md"));
        check(!none.ok && !none.error.isEmpty(), QStringLiteral("model header: a text file is no model"), none.error);
    }
    {
        LanguageChoice a = chooseLanguage(QStringLiteral("en"), QStringLiteral("de"), 0.7f, 0.2f, true);
        LanguageChoice b = chooseLanguage(QStringLiteral("en"), QStringLiteral("de"), 0.1f, 0.8f, true);
        LanguageChoice c = chooseLanguage(QStringLiteral("en"), QStringLiteral("de"), 0.1f, 0.8f, false);
        LanguageChoice d = chooseLanguage(QStringLiteral("de"), QStringLiteral("en"), 0.2f, 0.7f, true);
        LanguageChoice e = chooseLanguage(QStringLiteral("en"), QString(), 0.1f, 0.9f, true);
        check(a.language == QLatin1String("en") && a.translate && b.language == QLatin1String("de") && b.translate
                  && c.language == QLatin1String("de") && !c.translate && d.language == QLatin1String("en") && !d.translate
                  && e.language == QLatin1String("en") && !e.translate,
              QStringLiteral("languages: main English with fallback German translates German pieces into English; a model "
                             "without translation keeps German; main German keeps English pieces English; no fallback: "
                             "plain English"));
        check(canTranslate(QStringLiteral("en"), QStringLiteral("de"), true) && !canTranslate(QStringLiteral("de"), QStringLiteral("en"), true)
                  && !canTranslate(QStringLiteral("en"), QStringLiteral("de"), false) && !canTranslate(QStringLiteral("en"), QStringLiteral("en"), true),
              QStringLiteral("translation only into English, only with a model that can, only with another fallback language"));
        check(languages().size() == 100 && languageName(QStringLiteral("de")) == QLatin1String("German")
                  && languageName(QStringLiteral("en")) == QLatin1String("English"),
              QStringLiteral("whisper's 100 languages with English names"));
    }
    {
        check(cleanText(QStringLiteral(" [BLANK_AUDIO] "), true).isEmpty() && cleanText(QStringLiteral("(Musik)"), true).isEmpty()
                  && cleanText(QStringLiteral("  The   lamp was lit.  [Music] "), true) == QLatin1String("The lamp was lit.")
                  && cleanText(QStringLiteral("Untertitel im Auftrag des ZDF, 2021"), true).isEmpty()
                  && !cleanText(QStringLiteral("Untertitel im Auftrag des ZDF, 2021"), false).isEmpty()
                  && cleanText(QStringLiteral("Thank you for watching!"), true).isEmpty(),
              QStringLiteral("text: non-speech marks out, spaces tidied; with the guard, credits whisper invents in silence out"));
        check(joinPiece(QString(), QStringLiteral("One."), false) == QLatin1String("One.")
                  && joinPiece(QStringLiteral("One."), QStringLiteral("Two."), false) == QLatin1String(" Two.")
                  && joinPiece(QStringLiteral("One."), QStringLiteral("Three."), true) == QLatin1String("\n\nThree.")
                  && joinPiece(QStringLiteral("One. "), QStringLiteral("Two."), false) == QLatin1String("Two."),
              QStringLiteral("pieces join with a space, a long pause starts a new paragraph"));
        QTemporaryDir td;
        const QDateTime when(QDate(2026, 11, 8), QTime(5, 7));
        const QString f1 = newTranscriptFile(td.path(), when);
        QFile(f1).open(QIODevice::WriteOnly);
        const QString f2 = newTranscriptFile(td.path(), when);
        QFile(f2).open(QIODevice::WriteOnly);
        QFile(newTranscriptFile(td.path(), when.addSecs(-3600))).open(QIODevice::WriteOnly);
        const QList<Transcript> list = transcriptsIn(td.path());
        check(QFileInfo(f1).fileName() == QLatin1String("2026-11-08 05-07.txt") && QFileInfo(f2).fileName() == QLatin1String("2026-11-08 05-07 (2).txt")
                  && list.size() == 3 && list.last().title == QLatin1String("2026-11-08 04-07"),
              QStringLiteral("transcription files named by date and time (yyyy-mm-dd hh-mm), \" (2)\" when taken; listed newest first"),
              QStringLiteral("%1 %2 %3").arg(QFileInfo(f1).fileName(), QFileInfo(f2).fileName(), list.isEmpty() ? QString() : list.last().title));
    }
    // pauses and pieces: 1 s speech, 1 s pause, 1.5 s speech, 4 s pause, 1 s speech, a click, 30 s without a pause
    const QVector<float> audio = makeAudio({{false, 500}, {true, 1000}, {false, 1000}, {true, 1500}, {false, 4000}, {true, 1000},
                                            {false, 1500}, {true, 60}, {false, 1500}});
    {
        Segmenter::Params p;
        Segmenter seg(p);
        for (int i = 0; i < audio.size(); i += 1000)
            seg.feed(audio.constData() + i, int(qMin<qsizetype>(1000, audio.size() - i)));
        seg.flush();
        const QList<Piece> pieces = seg.take();
        QStringList d;
        for (const Piece &pc : pieces)
            d << QStringLiteral("%1 s%2").arg(pc.audio.size() / 16000.0, 0, 'f', 2).arg(pc.newParagraph ? QStringLiteral(" ¶") : QString());
        check(pieces.size() == 3 && !pieces[0].newParagraph && !pieces[1].newParagraph && pieces[2].newParagraph
                  && qAbs(pieces[0].audio.size() / 16000.0 - 1.4) < 0.15 && qAbs(pieces[1].audio.size() / 16000.0 - 1.9) < 0.15,
              QStringLiteral("pieces: a pause of 0.7 s ends a piece (200 ms kept on both sides), 3 s start a new paragraph, "
                             "a click of 60 ms is no piece"),
              d.join(QStringLiteral(", ")));
        Segmenter::Params p2;
        p2.paragraphMs = 0;
        Segmenter seg2(p2);
        const QVector<float> longSpeech = makeAudio({{true, 30000}, {false, 1000}});
        seg2.feed(longSpeech.constData(), int(longSpeech.size()));
        seg2.flush();
        const QList<Piece> two = seg2.take();
        check(two.size() == 2 && two[0].audio.size() <= 25 * 16000 && !two[1].newParagraph,
              QStringLiteral("30 s without a pause are cut into pieces of at most 25 s (whisper works on 30 s); paragraphs "
                             "switched off"),
              QString::number(two.size()));
        // 48 kHz stereo: the same sine, a third of the samples
        QVector<float> st(48000 * 2);
        for (int i = 0; i < 48000; ++i)
            st[2 * i] = st[2 * i + 1] = 0.5f * std::sin(2 * float(M_PI) * 440.f * i / 48000.f);
        const QVector<float> m16 = toMono16k(st.constData(), 48000, 2, 48000);
        QVector<float> st44(44100);
        const QVector<float> m44 = toMono16k(st44.constData(), 44100, 1, 44100);
        check(m16.size() == 16000 && qAbs(m16[40] - 0.5f * std::sin(2 * float(M_PI) * 440.f * 40 / 16000.f)) < 0.05f
                  && qAbs(int(m44.size()) - 16000) <= 1,
              QStringLiteral("microphone formats: 48 kHz stereo and 44.1 kHz become 16 kHz mono"));
    }

    // ================================================================ the add-on package
    const QString pkg = buildDir + QStringLiteral("/addons-dist/speech-transcription.qbpkg");
    {
        const addons::PackageInfo info = addons::inspect(pkg);
        QStringList log;
        QString err;
        bool restart = false;
        const bool ok = info.isValid() && addons::install(pkg, &log, &err, &restart);
        check(ok && info.manifest.minApp == QLatin1String(QB_RUNTIME_SINCE),
              QStringLiteral("the add-on package installs and needs " QB_RUNTIME_SINCE " (its runtime's first version)"),
              info.error + err + QLatin1String(" minApp ") + info.manifest.minApp);
        const QString dir = addons::contentDirs(QStringLiteral("speech")).value(0);
        QStringList missing;
        for (const char *f : {"libqb-speech-impl.so", "lib/libwhisper.so.1", "lib/libggml.so.0", "lib/libggml-base.so.0",
                              "lib/libggml-cpu-haswell.so", "lib/libggml-cpu-x64.so", "lib/libggml-vulkan.so",
                              "lib/libQt6Multimedia.so.6", "qtplugins/tls/libqopensslbackend.so", "samples/jfk.wav",
                              "licenses/README.txt", "licenses/whisper.cpp-MIT.txt", "licenses/Qt-LGPL-3.0.txt"})
            if (!QFileInfo::exists(dir + QLatin1Char('/') + QLatin1String(f)))
                missing << QLatin1String(f);
        check(!dir.isEmpty() && missing.isEmpty(),
              QStringLiteral("its files: the engine, whisper.cpp with a compute module per processor generation and Vulkan, Qt "
                             "Multimedia, Qt's TLS plugin, the test recording, the licenses"),
              missing.join(QLatin1String(", ")));
        qint64 bytes = QFileInfo(pkg).size();
        note(QStringLiteral("the add-on package: %1 MB").arg(bytes / 1e6, 0, 'f', 1));
    }

    // ================================================================ the program side (interface 1.1)
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    // a Tools bar the writer arranged (0.91.0: the tool buttons stand there; the main toolbar before): gets the speech
    // button once
    const QStringList own = {QStringLiteral("compile"), QStringLiteral("showInspector")};
    as.toolbarTools = own;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    QObject *tool = speechTool();
    check(tool != nullptr, QStringLiteral("the writing tool \"Speech Transcription\" is loaded"),
          compile::PluginManager::instance().messages().join(QLatin1String("; ")));
    check(toolhost::host().apiVersion() == 110 && tool && toolhost::Host::extension(dynamic_cast<tools::ToolInterface *>(tool)),
          QStringLiteral("the program offers tool interface 1.1; the speech tool has the 1.1 part (ToolExtension)"));
    tools::ToolInterface *old = compile::PluginManager::instance().tool(QStringLiteral("old-tool"));
    check(old && !toolhost::Host::extension(old), QStringLiteral("a tool built for interface 1.0 loads (and has no 1.1 part)"));
    const QStringList standard = toolbars::standard(toolbars::Bar::Main);
    const int trash = int(standard.indexOf(QStringLiteral("trash")));
    check(trash > 0 && standard.value(trash + 1) == QLatin1String("undo") && standard.value(trash + 2) == QLatin1String("redo"),
          QStringLiteral("standard main toolbar: Undo and Redo right after Move to Trash"), standard.join(QLatin1Char(' ')));

    QTemporaryDir work;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Dictation"), &err);
    if (!proj) {
        check(false, QStringLiteral("(project made)"), err);
        return failures;
    }
    Project *p = proj.get();
    BinderItem *doc = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Chapter"));
    QTextCursor(p->docs()->text(doc->id)).insertText(QStringLiteral("The lamp. "));
    p->save(&err);
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1500, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(600);

    const QStringList placed = AppSettings::instance().toolbarTools;
    check(placed.indexOf(QStringLiteral("tool:speech-transcription.record")) == placed.indexOf(QStringLiteral("showInspector")) - 1
              && placed.count(QStringLiteral("tool:speech-transcription.record")) == 1,
          QStringLiteral("a Tools bar the writer arranged gets the speech button once, before the inspector's button"),
          placed.join(QLatin1Char(' ')));
    auto *bar = w.findChild<QToolBar *>(QStringLiteral("toolsBar"));
    if (!bar)
        for (QToolBar *b : w.findChildren<QToolBar *>())
            if (b->actions().contains(w.findChild<QAction *>(QStringLiteral("showInspector"))))
                bar = b;
    QAction *rec = w.findChild<QAction *>(QStringLiteral("tool:speech-transcription.record"));
    auto *recButton = bar && rec ? qobject_cast<QToolButton *>(bar->widgetForAction(rec)) : nullptr;
    check(recButton && recButton->menu() && recButton->popupMode() == QToolButton::MenuButtonPopup,
          QStringLiteral("the speech button is on the Tools toolbar, with an arrow that opens its menu"));
    QAction *undo = w.findChild<QAction *>(QStringLiteral("undo"));
    auto *mainBar = w.findChild<QToolBar *>(QStringLiteral("mainToolbar")); // 0.91.0: Undo stands on the Main bar
    check(undo && mainBar && mainBar->widgetForAction(undo), QStringLiteral("Undo is on the Main toolbar"));
    w.grab().save(outDir + QStringLiteral("/01-window.png"));
    if (bar)
        bar->grab().save(outDir + QStringLiteral("/02-toolbar.png"));

    // the 1.0 tool runs: writes at the cursor and sees the host's version
    {
        EditorPane *pane = nullptr;
        for (EditorPane *ep : w.findChildren<EditorPane *>())
            if (ep->isVisible())
                pane = ep;
        if (pane) {
            pane->showItems({doc}, false, int(ViewMode::Single));
            settle(300);
            if (TextEditor *e = pane->currentEditor()) {
                e->setFocus();
                QTextCursor c = e->textCursor();
                c.movePosition(QTextCursor::End);
                e->setTextCursor(c);
                toolhost::host().runCommand(QStringLiteral("old-tool.write"));
                settle(100);
                check(e->toPlainText().contains(QLatin1String("[old tool, host 110]"))
                          && toolhost::host().setting(QStringLiteral("old-tool"), QStringLiteral("apiVersion")).toInt() == 110,
                      QStringLiteral("the 1.0 tool's command runs: it writes at the cursor and keeps a setting"),
                      e->toPlainText());
            }
            // The crash of 2026-11-08 (0.60.5, report of the writer): Split at Selection chosen in the text's
            // right-click menu. The split loads the new document into the editor, which deletes the editor and
            // with it its menu while the menu still handles the click (Qt then used freed memory). A run
            // without the fix ends here with a segmentation fault.
            BinderItem *splitDoc = doc;
            QTextCursor(p->docs()->text(splitDoc->id)).insertText(QStringLiteral(" Second part."));
            pane->showItems({splitDoc}, false, int(ViewMode::Single));
            settle(300);
            TextEditor *e = pane->currentEditor();
            QMenu *ctx = nullptr;
            QAction *splitEntry = nullptr;
            if (e) {
                QTextCursor c(e->document());
                c.setPosition(e->toPlainText().indexOf(QLatin1String("Second")));
                e->setTextCursor(c);
                const QPoint at = e->cursorRect().center();
                QContextMenuEvent ev(QContextMenuEvent::Mouse, at, e->viewport()->mapToGlobal(at));
                QApplication::sendEvent(e->viewport(), &ev);
                settle(300);
                ctx = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (ctx)
                    for (QAction *a : ctx->actions())
                        if (a->objectName() == QLatin1String("split"))
                            splitEntry = a;
            }
            const int before = p->draft()->children.size();
            if (ctx && splitEntry) {
                const QPoint where = ctx->actionGeometry(splitEntry).center();
                QTest::mouseMove(ctx, where);
                settle(100);
                QTest::mouseClick(ctx, Qt::LeftButton, Qt::NoModifier, where);
                settle(600);
            }
            const int row = p->draft()->children.indexOf(splitDoc);
            BinderItem *next = p->draft()->children.value(row + 1);
            check(ctx && splitEntry && p->draft()->children.size() == before + 1 && next
                      && p->docs()->plainText(next->id).startsWith(QLatin1String("Second part.")),
                  QStringLiteral("Split at Selection from the text's right-click menu: the new document, and no crash "
                                 "(the crash of 2026-11-08)"),
                  QStringLiteral("menu %1 entry %2 documents %3 -> %4").arg(ctx != nullptr).arg(splitEntry != nullptr)
                      .arg(before).arg(p->draft()->children.size()));
        }
    }

    // ---- the inspector tab
    Inspector *insp = w.findChild<Inspector *>();
    auto *tabButton = w.findChild<QToolButton *>(QStringLiteral("inspectorTab:speech-transcription.list"));
    auto *notesButton = insp ? insp->findChildren<QToolButton *>().value(0) : nullptr;
    QWidget *panel = w.findChild<QWidget *>(QStringLiteral("speechPanel"));
    check(insp && tabButton && panel && insp->extraTabs() == QStringList{QStringLiteral("speech-transcription.list")}
              && notesButton && tabButton->x() < insp->findChildren<QToolButton *>().value(1)->x(),
          QStringLiteral("the inspector has the tab \"Transcriptions\", its button at the left end of the row"));
    toolhost::host().runCommand(QStringLiteral("speech-transcription.show"));
    settle(200);
    check(insp && insp->currentExtraTab() == QLatin1String("speech-transcription.list") && panel && panel->isVisible(),
          QStringLiteral("Edit > Writing Tools > Show Transcriptions shows the tab"));
    {
        const QVariantMap st = insp->saveState();
        insp->showTab(Inspector::Notes);
        insp->restoreState(st);
        check(st.value(QStringLiteral("toolTab")).toString() == QLatin1String("speech-transcription.list")
                  && insp->currentExtraTab() == QLatin1String("speech-transcription.list"),
              QStringLiteral("the tab shown is kept with the project by its name (not its number)"));
    }

    // ---- the engine: microphones, graphics cards
    QString pulseError;
    QString fifo = home.path() + QStringLiteral("/mic.fifo");
    QString moduleId;
    {
        QProcess pa;
        pa.start(QStringLiteral("pactl"), {QStringLiteral("load-module"), QStringLiteral("module-pipe-source"),
                                           QStringLiteral("source_name=qbspeechmic"), QStringLiteral("file=") + fifo,
                                           QStringLiteral("format=s16le"), QStringLiteral("rate=16000"), QStringLiteral("channels=1")});
        pa.waitForFinished(10000);
        moduleId = QString::fromUtf8(pa.readAllStandardOutput()).trimmed();
        if (pa.exitCode() != 0 || moduleId.isEmpty())
            pulseError = QString::fromUtf8(pa.readAllStandardError()) + QStringLiteral(" (pactl)");
    }
    QMenu *menu = recButton ? recButton->menu() : nullptr;
    if (menu) {
        // A new PulseAudio source reaches Qt's device list a moment after pactl returns when this process
        // already has the list (2026-11-08, 0.63.0: the menu was built too early and missed it). The menu is
        // built anew each time it opens, so open it until the source is there, for at most 5 s.
        QStringList entries;
        QElapsedTimer waited;
        waited.start();
        for (;;) {
            emit menu->aboutToShow();
            entries.clear();
            for (QAction *a : menu->actions())
                entries << a->text();
            if (!pulseError.isEmpty() || entries.join(QLatin1Char('|')).contains(QLatin1String("FIFO"))
                || waited.elapsed() > 5000)
                break;
            settle(200);
        }
        const bool mic = pulseError.isEmpty() ? entries.join(QLatin1Char('|')).contains(QLatin1String("FIFO")) : true;
        check(entries.contains(QStringLiteral("The System's Default Input")) && mic
                  && entries.contains(QStringLiteral("Speech Transcription Options…")),
              QStringLiteral("the arrow's menu: the system's default input, the microphones found, Show Transcriptions, the options"),
              entries.join(QLatin1String(" | ")) + pulseError);
        menu->popup(recButton->mapToGlobal(QPoint(0, recButton->height())));
        settle(300);
        menu->grab().save(outDir + QStringLiteral("/03-menu.png"));
        menu->hide();
    }

    // ---- File > Options > Speech
    {
        OptionsDialog dlg(&w);
        dlg.resize(1000, 760);
        check(dlg.toolSections() == QStringList{QStringLiteral("speech-transcription")} && dlg.showToolSection(QStringLiteral("speech-transcription")),
              QStringLiteral("File > Options has a section \"Speech\" of its own after Extras"));
        auto *list = dlg.findChild<QListWidget *>(QStringLiteral("optionPages"));
        check(list && list->item(list->count() - 1)->text() == QLatin1String("Speech")
                  && list->currentRow() == list->count() - 1,
              QStringLiteral("... its icon is the last of the row and opens the section"));
        auto *model = dlg.findChild<QComboBox *>(QStringLiteral("speechModel"));
        auto *impact = dlg.findChild<QLabel *>(QStringLiteral("speechImpact"));
        auto *device = dlg.findChild<QComboBox *>(QStringLiteral("speechDevice"));
        auto *langNote = dlg.findChild<QLabel *>(QStringLiteral("speechLanguageNote"));
        check(model && model->count() == 4 && model->currentData().toString() == QLatin1String("medium") && impact
                  && impact->text().contains(QLatin1String("539 MB")),
              QStringLiteral("model list with Medium chosen and its impact: download size, memory, speed, translation"));
        check(langNote && langNote->text().contains(QLatin1String("German")) && langNote->text().contains(QLatin1String("English")),
              QStringLiteral("languages: main English, fallback German; the note says German is written in English"),
              langNote ? langNote->text() : QString());
        if (model)
            model->setCurrentIndex(model->findData(QStringLiteral("large-v3-turbo")));
        check(langNote && langNote->text().contains(QLatin1String("cannot translate")),
              QStringLiteral("with Large v3 Turbo the note says it cannot translate"), langNote ? langNote->text() : QString());
        if (model)
            model->setCurrentIndex(model->findData(QStringLiteral("medium")));
        QStringList devs;
        if (device)
            for (int i = 0; i < device->count(); ++i)
                devs << device->itemText(i);
        check(device && device->count() == 2 && device->itemData(0).toInt() == -1 && device->itemData(1).toInt() == 0
                  && devs.value(1).contains(QLatin1String("llvmpipe")),
              QStringLiteral("compute on: the processor, and a graphics card found through Vulkan (here Mesa's lavapipe)"),
              devs.join(QLatin1String(" | ")));
        dlg.show();
        settle(300);
        dlg.grab().save(outDir + QStringLiteral("/04-options.png"));
        if (auto *sa = dlg.findChild<QScrollArea *>(QStringLiteral("toolSection:speech-transcription"))) {
            sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum());
            settle(100);
            dlg.grab().save(outDir + QStringLiteral("/05-options-lower.png"));
        }
        // 0.64: record now, the text later
        auto *live = dlg.findChild<QRadioButton *>(QStringLiteral("speechModeLive"));
        auto *recOnly = dlg.findChild<QRadioButton *>(QStringLiteral("speechModeRecord"));
        auto *delAfter = dlg.findChild<QCheckBox *>(QStringLiteral("speechDeleteAfter"));
        check(live && recOnly && delAfter && live->isChecked() && !recOnly->isChecked() && !delAfter->isChecked(),
              QStringLiteral("0.64: the options offer Transcribe live (the standard on Linux) or Record only, and deleting "
                             "a recording once its text is written (off)"));
        auto *hint = dlg.findChild<QPlainTextEdit *>(QStringLiteral("speechHint"));
        if (hint)
            hint->setPlainText(QStringLiteral("Eleanor, Brackwater"));
        if (recOnly)
            recOnly->setChecked(true);
        dlg.apply();
        check(speechSetting(QStringLiteral("mode")).toString() == QLatin1String("record"),
              QStringLiteral("... Record only is kept by the options"));
        setSpeechSetting(QStringLiteral("mode"), QStringLiteral("live"));
        check(speechSetting(QStringLiteral("hint")).toString() == QLatin1String("Eleanor, Brackwater")
                  && speechSetting(QStringLiteral("main")).toString() == QLatin1String("en")
                  && speechSetting(QStringLiteral("fallback")).toString() == QLatin1String("de"),
              QStringLiteral("Apply stores the speech options"));
    }

    // ================================================================ models: download and checks
    TestServer server;
    check(server.start(), QStringLiteral("(test server)"));
    qputenv("QB_SPEECH_SOURCE", server.base().toUtf8());
    QTemporaryDir copies;
    // the empty test models of whisper.cpp under the names of the real ones
    server.files.insert(QStringLiteral("/ggml-small-q5_1.bin"), smallFile);
    server.files.insert(QStringLiteral("/ggml-medium-q5_0.bin"), tinyFile);   // first: the wrong model behind the name
    const QString modelsDir = qb::dataDir() + QStringLiteral("/tools/speech-transcription/models");
    bool done = false;
    if (tool) {
        // no model yet: recording asks for one
        check(!callBool(tool, "startRecording") && callString(tool, "lastError").contains(QLatin1String("No speech model")),
              QStringLiteral("without a model, the button says to choose one in the options"));
        // the official source (here the test server): a file without the known checksum is refused
        // before it is read (0.64.1, audit X-8)
        done = runModelJob(tool, "downloadModel", QStringLiteral("medium"));
        check(done && callString(tool, "modelMessage").contains(QLatin1String("known checksum"))
                  && speechSetting(QStringLiteral("modelFile")).toString().isEmpty()
                  && QDir(modelsDir).entryList(QDir::Files).isEmpty(),
              QStringLiteral("0.64.1: from the official source, a file without the known checksum (a tiny model behind the name "
                             "of Medium) is refused before it is read; nothing kept"),
              callString(tool, "modelMessage"));
        // Small: interrupted after 100 kB, then continued
        server.cutAfter = 100000;
        done = runModelJob(tool, "downloadModel", QStringLiteral("small"));
        check(done && callString(tool, "modelMessage").contains(QLatin1String("download failed"))
                  && QFileInfo(modelsDir + QStringLiteral("/ggml-small-q5_1.bin.part")).size() > 0
                  && QFileInfo(modelsDir + QStringLiteral("/ggml-small-q5_1.bin.part")).size() <= 100000,
              QStringLiteral("an interrupted download says so and keeps what came (up to 100 kB)"),
              callString(tool, "modelMessage") + QStringLiteral(" part %1").arg(QFileInfo(modelsDir + QStringLiteral("/ggml-small-q5_1.bin.part")).size()));
        done = runModelJob(tool, "downloadModel", QStringLiteral("small"));
        check(done && !server.ranges.isEmpty() && server.ranges.last().startsWith(QLatin1String("Range: bytes="))
                  && callString(tool, "modelMessage").contains(QLatin1String("known checksum"))
                  && speechSetting(QStringLiteral("modelFile")).toString().isEmpty(),
              QStringLiteral("the next download goes on at 100 kB (the test model then fails the checksum)"),
              callString(tool, "modelMessage") + server.ranges.join(QLatin1Char(',')));
        // another address (the test server as a mirror): a file that differs is read only after a yes
        setSpeechSetting(QStringLiteral("source"), QStringLiteral("other"));
        setSpeechSetting(QStringLiteral("otherAddress"), QStringLiteral("http://mirror.example/models/"));
        int before = server.requests;
        done = runModelJob(tool, "downloadModel", QStringLiteral("small"));
        check(done && server.requests == before && callString(tool, "modelMessage").contains(QLatin1String("https://"))
                  && speechSetting(QStringLiteral("modelFile")).toString().isEmpty(),
              QStringLiteral("0.64.1: another address with http:// is not used (nothing is fetched)"), callString(tool, "modelMessage"));
        setSpeechSetting(QStringLiteral("otherAddress"), server.base());
        done = runModelJob(tool, "downloadModel", QStringLiteral("medium"));
        check(done && callString(tool, "modelMessage").contains(QLatin1String("differs"))
                  && speechSetting(QStringLiteral("modelFile")).toString().isEmpty() && QDir(modelsDir).entryList(QDir::Files).isEmpty(),
              QStringLiteral("0.64.1: from another address, a file that differs from the known one is not read without a yes"),
              callString(tool, "modelMessage"));
        qputenv("QB_SPEECH_ACCEPT_CHANGED", "1");
        done = runModelJob(tool, "downloadModel", QStringLiteral("medium"));
        check(done && callString(tool, "modelMessage").contains(QLatin1String("not \"medium\""))
                  && speechSetting(QStringLiteral("modelFile")).toString().isEmpty()
                  && QDir(modelsDir).entryList(QDir::Files).isEmpty(),
              QStringLiteral("... with a yes it is read: a tiny model behind the name of Medium is refused by the check of its "
                             "description, nothing kept"),
              callString(tool, "modelMessage"));
        done = runModelJob(tool, "downloadModel", QStringLiteral("small"));
        check(done && callString(tool, "modelMessage").contains(QLatin1String("test transcription"))
                  && speechSetting(QStringLiteral("modelFile")).toString().isEmpty(),
              QStringLiteral("a model that does not pass the test transcription is not used"), callString(tool, "modelMessage"));
        // with the test transcription passed (QB_SPEECH_SKIP_TEST)
        qputenv("QB_SPEECH_SKIP_TEST", "1");
        done = runModelJob(tool, "downloadModel", QStringLiteral("small"));
        const QString smallInstalled = speechSetting(QStringLiteral("modelFile")).toString();
        check(done && callString(tool, "modelMessage").startsWith(QLatin1String("Installed: Small"))
                  && smallInstalled == modelsDir + QStringLiteral("/ggml-small-q5_1.bin") && sha1(smallInstalled) == sha1(smallFile)
                  && speechSetting(QStringLiteral("model")).toString() == QLatin1String("small"),
              QStringLiteral("... then it is installed: Small, under its own name in the models folder"),
              callString(tool, "modelMessage"));
        // another address: a file address
        setSpeechSetting(QStringLiteral("otherAddress"), server.base() + QStringLiteral("/mirror/medium.bin"));
        server.files.insert(QStringLiteral("/mirror/medium.bin"), mediumFile);
        before = server.requests;
        done = runModelJob(tool, "downloadModel", QStringLiteral("medium"));
        const QString mediumInstalled = speechSetting(QStringLiteral("modelFile")).toString();
        check(done && server.requests == before + 1 && mediumInstalled == modelsDir + QStringLiteral("/ggml-medium-q5_0.bin")
                  && !QFileInfo::exists(smallInstalled),
              QStringLiteral("Medium from another address: installed, and only then the old model (Small) is deleted"),
              callString(tool, "modelMessage"));
        setSpeechSetting(QStringLiteral("source"), QStringLiteral("official"));
        // a file the writer downloaded: copied, recognized by its description
        QFile::copy(mediumFile, copies.path() + QStringLiteral("/my-model.bin"));
        done = runModelJob(tool, "useLocalModel", copies.path() + QStringLiteral("/my-model.bin"));
        check(done && callString(tool, "modelMessage").startsWith(QLatin1String("Installed: Medium"))
                  && QFileInfo::exists(copies.path() + QStringLiteral("/my-model.bin"))
                  && speechSetting(QStringLiteral("modelFile")).toString() == modelsDir + QStringLiteral("/my-model.bin")
                  && !QFileInfo::exists(mediumInstalled),
              QStringLiteral("\"Use a file I downloaded\": copied into the models folder under its name (the original stays), "
                             "recognized as Medium; the model before it is deleted"),
              callString(tool, "modelMessage"));
        qunsetenv("QB_SPEECH_SKIP_TEST");
        qunsetenv("QB_SPEECH_ACCEPT_CHANGED");
    }

    // ================================================================ recordings (pieces without a model: QB_SPEECH_FAKE)
    const QString wav = home.path() + QStringLiteral("/speech.wav");
    writeWav(wav, audio);
    qputenv("QB_SPEECH_FAKE", "1");
    QString recFile;
    if (tool) {
        QAction *recAction = w.findChild<QAction *>(QStringLiteral("tool:speech-transcription.record"));
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, wav));
        recFile = callString(tool, "recordingFile");
        const bool checkedWhile = recAction && recAction->isChecked();
        auto *edit = panel ? panel->findChild<QPlainTextEdit *>(QStringLiteral("speechText")) : nullptr;
        const bool readOnlyWhile = edit && edit->isReadOnly();
        settle(300);
        w.grab().save(outDir + QStringLiteral("/06-recording.png"));
        waitFor([tool] { return !callBool(tool, "isBusy"); }, 60000);
        settle(300);
        const QString text = readFile(recFile);
        check(started && checkedWhile && readOnlyWhile, QStringLiteral("a recording: the button is pressed, the text is read-only while it is written"));
        check(QFileInfo(recFile).fileName().startsWith(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")))
                  && QFileInfo(recFile).absolutePath() == home.path() + QStringLiteral("/Documents/Quillbinder Transcriptions"),
              QStringLiteral("it writes into a new file named by date and time in ~/Documents/Quillbinder Transcriptions"), recFile);
        check(text.contains(QRegularExpression(QStringLiteral("^Piece 1 \\(1\\.\\d s\\) Piece 2 \\(1\\.\\d s\\)\\n\\nPiece 3 \\(1\\.\\d s\\)$"))),
              QStringLiteral("three pieces in the file: the first two joined, the third after a long pause in a new paragraph"),
              text);
        check(edit && edit->toPlainText() == text && !edit->isReadOnly() && recAction && !recAction->isChecked(),
              QStringLiteral("the tab shows the text as it came; afterwards it can be edited and the button is up again"));
        // editing, renaming, copying, deleting
        if (edit && panel) {
            edit->setFocus();
            edit->moveCursor(QTextCursor::End);
            edit->insertPlainText(QStringLiteral(" Edited."));
            settle(1200); // saved by itself after a short pause
            check(readFile(recFile).endsWith(QLatin1String("Piece 3 (1.0 s) Edited.")) || readFile(recFile).endsWith(QLatin1String(" Edited.")),
                  QStringLiteral("an edit in the tab is saved by itself"), readFile(recFile));
            bool renamed = false;
            QMetaObject::invokeMethod(panel, "rename", Qt::DirectConnection, Q_RETURN_ARG(bool, renamed), Q_ARG(QString, QStringLiteral("Lighthouse scene")));
            QStringList titles;
            QMetaObject::invokeMethod(panel, "titles", Qt::DirectConnection, Q_RETURN_ARG(QStringList, titles));
            check(renamed && titles.contains(QStringLiteral("Lighthouse scene"))
                      && QFileInfo::exists(QFileInfo(recFile).absolutePath() + QStringLiteral("/Lighthouse scene.txt")),
                  QStringLiteral("a transcription can be renamed"), titles.join(QLatin1String(", ")));
            QMetaObject::invokeMethod(panel, "copyAll", Qt::DirectConnection);
            check(QApplication::clipboard()->text().endsWith(QLatin1String(" Edited.")), QStringLiteral("Copy puts the whole text on the clipboard"));
            w.grab().save(outDir + QStringLiteral("/07-transcription.png"));
        }
    }
    // ================================================================ 0.64: record only, the text later
    if (tool && panel) {
        auto *list = panel->findChild<QListWidget *>(QStringLiteral("speechList"));
        auto *status = panel->findChild<QLabel *>(QStringLiteral("speechStatus"));
        auto *all = panel->findChild<QToolButton *>(QStringLiteral("speechTranscribeAll"));
        auto *edit = panel->findChild<QPlainTextEdit *>(QStringLiteral("speechText"));
        // the Transcribe button in the line of a recording
        auto nowButton = [&](const QString &title) -> QToolButton * {
            if (!list)
                return nullptr;
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text() == title)
                    if (QWidget *iw = list->itemWidget(list->item(i)))
                        return iw->findChild<QToolButton *>(QStringLiteral("speechTranscribeNow"));
            return nullptr;
        };
        auto recordOnce = [&](QString *textFile) -> bool {
            bool started = false;
            QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, wav));
            *textFile = callString(tool, "recordingFile");
            settle(200);
            waitFor([tool] { return !callBool(tool, "isBusy"); }, 30000);
            settle(400);
            return started;
        };
        check(!callBool(tool, "recordOnly"), QStringLiteral("0.64: transcribing live is the standard on Linux"));
        setSpeechSetting(QStringLiteral("mode"), QStringLiteral("record"));
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, wav));
        const QString future = callString(tool, "recordingFile");
        const QString recWav = future.left(future.size() - 4) + QStringLiteral(".wav");
        settle(300);
        const QString during = status ? status->text() : QString();
        w.grab().save(outDir + QStringLiteral("/10-record-only.png"));
        waitFor([tool] { return !callBool(tool, "isBusy"); }, 30000);
        settle(400);
        QVector<float> back;
        QFile rf(recWav);
        QByteArray head;
        if (rf.open(QIODevice::ReadOnly))
            head = rf.read(44);
        const qint64 wavBytes = QFileInfo(recWav).size();
        const double seconds = (wavBytes - 44) / 32000.0;
        check(started && during.startsWith(QLatin1String("Recording")) && QFileInfo::exists(recWav) && !QFileInfo::exists(future),
              QStringLiteral("record only: the sound goes into a recording (.wav) named by date and time, no text is written"),
              QStringLiteral("started %1 status [%2] %3").arg(started).arg(during, recWav));
        check(head.startsWith("RIFF") && head.mid(8, 8) == "WAVEfmt " && qFromLittleEndian<quint32>(head.constData() + 24) == 16000
                  && qFromLittleEndian<quint16>(head.constData() + 22) == 1 && qFromLittleEndian<quint16>(head.constData() + 34) == 16
                  && qFromLittleEndian<quint32>(head.constData() + 40) == quint32(wavBytes - 44)
                  && std::abs(seconds - audio.size() / 16000.0) < 0.3,
              QStringLiteral("... a WAV file of 16 kHz, mono, 16 bit, as long as what was said (1.92 MB per minute)"),
              QStringLiteral("%1 s of %2 s").arg(seconds).arg(audio.size() / 16000.0));
        const QString title = QFileInfo(future).completeBaseName();
        QToolButton *now = nowButton(title);
        check(now && now->isEnabled() && now->text() == QLatin1String("Transcribe") && all && all->isVisible() && all->isEnabled(),
              QStringLiteral("the list shows the recording with a Transcribe button in its line, and Transcribe All"));
        if (list)
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text() == title)
                    list->setCurrentRow(i);
        check(edit && edit->isReadOnly() && edit->toPlainText().isEmpty() && edit->placeholderText().contains(QLatin1String("no text yet")),
              QStringLiteral("... chosen, it says that it has no text yet and cannot be typed into"));
        w.grab().save(outDir + QStringLiteral("/11-recording-in-list.png"));
        if (now)
            now->click();
        waitFor([&] { return QFileInfo::exists(future) && !callBool(tool, "isBusy"); }, 60000);
        settle(400);
        const QString text = readFile(future);
        check(text.contains(QRegularExpression(QStringLiteral("^Piece 1 \\(1\\.\\d s\\) Piece 2 \\(1\\.\\d s\\)\\n\\nPiece 3 \\(1\\.\\d s\\)$")))
                  && !QFileInfo::exists(speech::partFileFor(future)) && QFileInfo::exists(recWav) && !nowButton(title),
              QStringLiteral("Transcribe writes the text of the recording (the same pieces and paragraph as live); the recording "
                             "stays, the button goes"),
              text);
        check(edit && edit->toPlainText() == text && !edit->isReadOnly(), QStringLiteral("... the tab shows the text, which can be edited now"));
        // two more recordings, Transcribe All, and deleting the recordings afterwards
        QString f2, f3;
        const bool two = recordOnce(&f2) && recordOnce(&f3);
        setSpeechSetting(QStringLiteral("deleteRecording"), true);
        QMetaObject::invokeMethod(panel, "refresh", Qt::DirectConnection);
        int waiting = 0;
        QMetaObject::invokeMethod(panel, "pendingCount", Qt::DirectConnection, Q_RETURN_ARG(int, waiting));
        QStringList inFolder = QDir(QFileInfo(f2).absolutePath()).entryList(QDir::Files);
        if (all)
            all->click();
        waitFor([&] { return QFileInfo::exists(f2) && QFileInfo::exists(f3) && !callBool(tool, "isBusy"); }, 90000);
        settle(500);
        auto wavOf = [](const QString &txt) { return txt.left(txt.size() - 4) + QStringLiteral(".wav"); };
        check(two && waiting == 2 && readFile(f2).count(QLatin1String("Piece")) == 3 && readFile(f3).count(QLatin1String("Piece")) == 3
                  && !QFileInfo::exists(wavOf(f2)) && !QFileInfo::exists(wavOf(f3)),
              QStringLiteral("Transcribe All writes the text of both waiting recordings one after the other; with the option "
                             "on, a recording is deleted once its text is there"),
              QStringLiteral("two %1 waiting %2, %3, %4; before: %5; after: %6")
                  .arg(two)
                  .arg(waiting)
                  .arg(QFileInfo(f2).fileName(), QFileInfo(f3).fileName(), inFolder.join(QLatin1String(" | ")),
                       QDir(QFileInfo(f2).absolutePath()).entryList(QDir::Files).join(QLatin1String(" | "))));
        setSpeechSetting(QStringLiteral("deleteRecording"), false);
        // a recording without text: renamed and deleted with its sound
        QString f4;
        recordOnce(&f4);
        QMetaObject::invokeMethod(panel, "refresh", Qt::DirectConnection);
        QMetaObject::invokeMethod(panel, "selectTranscript", Qt::DirectConnection, Q_ARG(QString, QFileInfo(f4).completeBaseName()));
        bool renamed = false, removed = false;
        QMetaObject::invokeMethod(panel, "rename", Qt::DirectConnection, Q_RETURN_ARG(bool, renamed), Q_ARG(QString, QStringLiteral("Harbor walk")));
        const QString moved = QFileInfo(f4).absolutePath() + QStringLiteral("/Harbor walk.wav");
        const bool renamedOk = renamed && QFileInfo::exists(moved) && !QFileInfo::exists(wavOf(f4));
        QMetaObject::invokeMethod(panel, "remove", Qt::DirectConnection, Q_RETURN_ARG(bool, removed), Q_ARG(bool, false));
        check(renamedOk && removed && !QFileInfo::exists(moved),
              QStringLiteral("a recording without text can be renamed and deleted (its sound goes along)"));
        // a recording that ends without being closed (program ended, power cut) is still a valid file
        {
            const QString crash = home.path() + QStringLiteral("/unclosed.wav");
            auto *wr = new speech::WavWriter;
            QString err;
            wr->open(crash, &err);
            const QVector<float> part = audio.mid(0, 24000); // 1.5 s
            wr->write(part.constData(), int(part.size()));
            QFile c(crash);
            QByteArray h;
            if (c.open(QIODevice::ReadOnly))
                h = c.read(44);
            const quint32 dataBytes = h.size() == 44 ? qFromLittleEndian<quint32>(h.constData() + 40) : 0;
            check(dataBytes >= 32000 && dataBytes <= 48000,
                  QStringLiteral("a recording not closed: its header already names about the first second (it is updated every second)"),
                  QString::number(dataBytes));
            delete wr;
        }
        setSpeechSetting(QStringLiteral("mode"), QStringLiteral("live"));
    }

    // a microphone: a PulseAudio pipe source fed with the same sound in real time
    if (tool && pulseError.isEmpty()) {
        setSpeechSetting(QStringLiteral("audioInput"), QByteArray("qbspeechmic"));
        std::atomic<bool> stopFeed(false);
        QThread *feeder = QThread::create([&] {
            QFile f(fifo);
            if (!f.open(QIODevice::WriteOnly))
                return;
            QVector<float> s = audio;
            s += makeAudio({{false, 2000}});
            QByteArray pcm(int(s.size()) * 2, Qt::Uninitialized);
            for (int i = 0; i < s.size(); ++i)
                qToLittleEndian(qint16(qBound(-32767, int(s[i] * 32767), 32767)), pcm.data() + 2 * i);
            // 20 ms at a time, as a sound card delivers: Qt 6.12's PulseAudio input keeps at most 1024 frames
            // (64 ms) in the server, so the pipe source's 100 ms blocks lost two thirds of the sound (0.80.0)
            for (int at = 0; at < pcm.size() && !stopFeed; at += 640) {
                f.write(pcm.mid(at, 640));
                f.flush();
                QThread::msleep(20);
            }
        });
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, QString()));
        feeder->start();
        const QString micFile = callString(tool, "recordingFile");
        auto *level = panel ? panel->findChild<QProgressBar *>(QStringLiteral("speechLevel")) : nullptr;
        int maxLevel = 0;
        QElapsedTimer t;
        t.start();
        while (!feeder->isFinished() && t.elapsed() < 40000) {
            settle(100);
            if (level)
                maxLevel = qMax(maxLevel, level->value());
        }
        QMetaObject::invokeMethod(tool, "stopRecording", Qt::DirectConnection);
        waitFor([tool] { return !callBool(tool, "isBusy"); }, 30000);
        stopFeed = true;
        feeder->wait(5000);
        delete feeder;
        const QString text = readFile(micFile);
        check(started && text.count(QLatin1String("Piece")) == 3 && text.contains(QLatin1String("\n\nPiece 3")) && maxLevel > 50,
              QStringLiteral("from a microphone (a PulseAudio source): three pieces, the paragraph, the level meter moves"),
              QStringLiteral("started %1 level %2 text [%3] %4").arg(started).arg(maxLevel).arg(text, callString(tool, "lastError")));
        // 0.64: Record only from the same microphone
        {
            setSpeechSetting(QStringLiteral("mode"), QStringLiteral("record"));
            std::atomic<bool> stop2(false);
            QThread *feeder2 = QThread::create([&] {
                QFile f(fifo);
                if (!f.open(QIODevice::WriteOnly))
                    return;
                QByteArray pcm(int(audio.size()) * 2, Qt::Uninitialized);
                for (int i = 0; i < audio.size(); ++i)
                    qToLittleEndian(qint16(qBound(-32767, int(audio[i] * 32767), 32767)), pcm.data() + 2 * i);
                for (int at = 0; at < pcm.size() && !stop2; at += 640) { // 20 ms, see above
                    f.write(pcm.mid(at, 640));
                    f.flush();
                    QThread::msleep(20);
                }
            });
            bool rec = false;
            QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, rec), Q_ARG(QString, QString()));
            feeder2->start();
            const QString txt = callString(tool, "recordingFile");
            int maxLevel2 = 0;
            QElapsedTimer t2;
            t2.start();
            while (!feeder2->isFinished() && t2.elapsed() < 40000) {
                settle(100);
                if (level)
                    maxLevel2 = qMax(maxLevel2, level->value());
            }
            settle(300);
            QMetaObject::invokeMethod(tool, "stopRecording", Qt::DirectConnection);
            waitFor([tool] { return !callBool(tool, "isBusy"); }, 10000);
            stop2 = true;
            feeder2->wait(5000);
            delete feeder2;
            const QString recWav = txt.left(txt.size() - 4) + QStringLiteral(".wav");
            const double secs = (QFileInfo(recWav).size() - 44) / 32000.0;
            check(rec && secs > audio.size() / 16000.0 - 1.0 && !QFileInfo::exists(txt) && maxLevel2 > 50,
                  QStringLiteral("0.64: Record only from a microphone: the whole recording is in the file, the level meter "
                                 "moves, no text yet"),
                  QStringLiteral("%1 s, level %2, %3").arg(secs).arg(maxLevel2).arg(callString(tool, "lastError")));
            setSpeechSetting(QStringLiteral("mode"), QStringLiteral("live"));
        }
        QProcess::execute(QStringLiteral("pactl"), {QStringLiteral("unload-module"), moduleId});
    } else {
        note(QStringLiteral("microphone checks not run: %1").arg(pulseError));
    }
    qunsetenv("QB_SPEECH_FAKE");

    // ================================================================ the official source (QB_SPEECH_OFFICIAL=<model id>)
    // downloads that model from whisper.cpp's files on Hugging Face with every check (0.61.1; the
    // network must reach huggingface.co and its download host)
    const QString official = qEnvironmentVariable("QB_SPEECH_OFFICIAL");
    if (tool && !official.isEmpty()) {
        const QByteArray keep = qgetenv("QB_SPEECH_SOURCE");
        qunsetenv("QB_SPEECH_SOURCE");
        setSpeechSetting(QStringLiteral("gpu"), -1);
        QElapsedTimer t;
        t.start();
        const bool ok = runModelJob(tool, "downloadModel", official, 1800000);
        check(ok && callString(tool, "modelMessage").startsWith(QLatin1String("Installed")),
              QStringLiteral("a model from the official source (Hugging Face) is downloaded and passes every check"),
              callString(tool, "modelMessage") + QLatin1Char(' ') + callString(tool, "lastError"));
        note(QStringLiteral("%1 (%2 s)").arg(callString(tool, "modelMessage")).arg(t.elapsed() / 1000));
        qputenv("QB_SPEECH_SOURCE", keep);
    }

    // ================================================================ a real model (QB_SPEECH_MODEL_FILE)
    const QString real = qEnvironmentVariable("QB_SPEECH_MODEL_FILE");
    if (tool && QFileInfo::exists(real)) {
        setSpeechSetting(QStringLiteral("gpu"), -1);
        const bool ok = runModelJob(tool, "useLocalModel", real, 900000);
        check(ok && callString(tool, "modelMessage").startsWith(QLatin1String("Installed")),
              QStringLiteral("a real model passes the check (checksum, description, test transcription of the sample)"),
              callString(tool, "modelMessage"));
        note(callString(tool, "modelMessage"));
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started),
                                  Q_ARG(QString, buildDir + QStringLiteral("/tool-plugins/speech/samples/jfk.wav")));
        const QString f = callString(tool, "recordingFile");
        waitFor([tool] { return !callBool(tool, "isBusy"); }, 900000);
        const QString text = readFile(f);
        check(started && text.contains(QLatin1String("country"), Qt::CaseInsensitive),
              QStringLiteral("a recording with the real model writes what was said"), text);
        note(QStringLiteral("real model wrote: %1").arg(text));
        // more recordings: QB_SPEECH_EXTRA="<wav>=<word>|<word>;..." (main language English, fallback
        // German, hint words from QB_SPEECH_HINT: each text must hold one of its words, e.g. English
        // words for German speech)
        for (const QString &spec : qEnvironmentVariable("QB_SPEECH_EXTRA").split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString wav = spec.section(QLatin1Char('='), 0, 0);
            const QStringList words = spec.section(QLatin1Char('='), 1).split(QLatin1Char('|'), Qt::SkipEmptyParts);
            setSpeechSetting(QStringLiteral("main"), QStringLiteral("en"));
            setSpeechSetting(QStringLiteral("fallback"), QStringLiteral("de"));
            setSpeechSetting(QStringLiteral("hint"), qEnvironmentVariable("QB_SPEECH_HINT"));   // hint words, if any
            QElapsedTimer t;
            t.start();
            bool go = false;
            QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, go), Q_ARG(QString, wav));
            const QString xf = callString(tool, "recordingFile");
            waitFor([tool] { return !callBool(tool, "isBusy"); }, 900000);
            const QString xt = readFile(xf);
            bool hit = false;
            for (const QString &wd : words)
                hit = hit || xt.contains(wd, Qt::CaseInsensitive);
            check(go && hit, QStringLiteral("real model, %1: the text holds one of %2").arg(QFileInfo(wav).fileName(), words.join(QLatin1String(", "))),
                  xt);
            note(QStringLiteral("%1 (%2 s): %3").arg(QFileInfo(wav).fileName()).arg(t.elapsed() / 1000).arg(xt.simplified()));
        }
    } else {
        note(QStringLiteral("real-model checks not run (QB_SPEECH_MODEL_FILE not set: no model could be fetched here)"));
    }

    // removing the model
    if (tool) {
        const QString f = speechSetting(QStringLiteral("modelFile")).toString();
        check(callBool(tool, "removeModel") && !QFileInfo::exists(f) && speechSetting(QStringLiteral("modelFile")).toString().isEmpty(),
              QStringLiteral("Remove deletes the model"));
    }
    if (panel) {
        // (0.70.0: after the tool's last job, which ran longer on GitHub's runner than here; the details say why not)
        if (tool)
            waitFor([tool] { return !callBool(tool, "isBusy"); }, 60000);
        const QString shown = callString(panel, "currentFile");
        bool removed = false;
        QMetaObject::invokeMethod(panel, "remove", Qt::DirectConnection, Q_RETURN_ARG(bool, removed), Q_ARG(bool, false));
        check(removed && !shown.isEmpty() && !QFileInfo::exists(shown), QStringLiteral("a transcription can be moved to the trash"),
              QStringLiteral("file '%1', busy %2").arg(shown).arg(tool ? callBool(tool, "isBusy") : false));
    }
    delete wp;
    std::printf("%d failure(s)\n", failures);
    return failures;
}
