#include "SpeechTool.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QThread>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <dlfcn.h>
#include <memory>

using namespace qb::tools;

namespace {

const QString kRecord = QStringLiteral("speech-transcription.record");
const QString kShow = QStringLiteral("speech-transcription.show");
const QString kTab = QStringLiteral("speech-transcription.list");

QIcon themeIcon(const QString &name)
{
    return QIcon::fromTheme(name);
}

bool quiet() { return qEnvironmentVariableIsSet("QB_NO_DIALOGS"); }

// memory a model needs (bytes, estimate) to offer a graphics card for it
qint64 memoryNeed(const QString &id)
{
    if (id == QLatin1String("small"))
        return 700LL << 20;
    if (id == QLatin1String("medium"))
        return 1400LL << 20;
    if (id == QLatin1String("large-v3-turbo"))
        return 1100LL << 20;
    return 2300LL << 20; // large-v3 and models of unknown kind
}

QString sizeText(qint64 bytes)
{
    if (bytes >= 1000LL * 1000 * 1000)
        return QObject::tr("%1 GB").arg(bytes / 1e9, 0, 'f', 1);
    return QObject::tr("%1 MB").arg(qRound64(bytes / 1e6));
}

} // namespace

// ==================================================================== the tool

QList<Command> SpeechTool::commands() const
{
    Command rec;
    rec.key = kRecord;
    rec.text = tr("Speech &Transcription");
    rec.place = QStringLiteral("edit/writing-tools");
    rec.icon = QStringLiteral("audio-input-microphone");
    rec.toolTip = tr("Start or stop speech transcription (the arrow beside the button: microphone and options)");
    Command show;
    show.key = kShow;
    show.text = tr("Show &Transcriptions");
    show.place = QStringLiteral("edit/writing-tools");
    show.icon = QStringLiteral("view-list-text");
    show.toolTip = tr("The transcriptions in the inspector");
    return {rec, show};
}

void SpeechTool::setHost(ToolHost *host)
{
    m_host = host;
}

QString SpeechTool::optionsPage() const
{
    // a section of its own needs the program's interface 1.1 (0.61)
    return m_host && m_host->apiVersion() >= 110 ? QStringLiteral("Section") : QStringLiteral("General");
}

void SpeechTool::run(const QString &key)
{
    if (key == kRecord)
        toggleRecording();
    else if (key == kShow && m_host && m_host->apiVersion() >= 110)
        m_host->showInspectorTab(kTab);
}

void SpeechTool::shutdown()
{
    // the program ends while a recording runs: stop it and let the pieces still waiting be written
    // (at most a minute), so nothing that was said gets lost
    if (m_engine && m_engine->isRecording())
        m_engine->stop();
    QElapsedTimer t;
    t.start();
    while (m_engine && m_engine->isBusy() && t.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    if (m_panel)
        m_panel->save();
}

QList<InspectorTab> SpeechTool::inspectorTabs() const
{
    return {InspectorTab{kTab, QStringLiteral("audio-input-microphone"), tr("Speech transcriptions")}};
}

QWidget *SpeechTool::createInspectorTab(const QString &key, QWidget *parent)
{
    if (key != kTab)
        return nullptr;
    auto *p = new TranscriptPanel(this, parent);
    m_panel = p;
    return p;
}

QList<ToolbarButton> SpeechTool::toolbarButtons() const
{
    // on the right of the main toolbar, before the inspector's button (it opens the inspector)
    return {ToolbarButton{kRecord, QStringLiteral("showInspector")}};
}

QMenu *SpeechTool::commandMenu(const QString &commandKey)
{
    if (commandKey != kRecord)
        return nullptr;
    if (!m_menu) {
        m_menu = new QMenu;
        m_menu->setObjectName(QStringLiteral("speechMenu"));
        connect(m_menu, &QMenu::aboutToShow, this, &SpeechTool::fillMenu);
        fillMenu();
    }
    return m_menu;
}

void SpeechTool::fillMenu()
{
    m_menu->clear();
    QAction *head = m_menu->addAction(tr("Microphone"));
    head->setEnabled(false);
    QString error;
    speech::EngineInterface *e = engine(&error);
    const QByteArray chosen = setting(QStringLiteral("audioInput")).toByteArray();
    auto *group = new QActionGroup(m_menu);
    QAction *def = m_menu->addAction(tr("The System's Default Input"));
    def->setCheckable(true);
    def->setChecked(chosen.isEmpty());
    group->addAction(def);
    connect(def, &QAction::triggered, this, [this] { setSetting(QStringLiteral("audioInput"), QByteArray()); });
    if (e) {
        for (const speech::AudioInput &in : e->audioInputs()) {
            QAction *ac = m_menu->addAction(in.isDefault ? tr("%1 (default)").arg(in.name) : in.name);
            ac->setCheckable(true);
            ac->setChecked(in.id == chosen);
            group->addAction(ac);
            const QByteArray id = in.id;
            connect(ac, &QAction::triggered, this, [this, id] { setSetting(QStringLiteral("audioInput"), id); });
        }
    } else {
        m_menu->addAction(error)->setEnabled(false);
    }
    m_menu->addSeparator();
    connect(m_menu->addAction(themeIcon(QStringLiteral("view-list-text")), tr("Show Transcriptions")), &QAction::triggered,
            this, [this] { run(kShow); });
    QAction *opt = m_menu->addAction(themeIcon(QStringLiteral("configure")), tr("Speech Transcription Options…"));
    opt->setObjectName(QStringLiteral("speechOptions"));
    connect(opt, &QAction::triggered, this, [this] {
        if (m_host && m_host->apiVersion() >= 110)
            m_host->showOptions(id());
    });
}

QVariant SpeechTool::setting(const QString &key, const QVariant &def) const
{
    return m_host ? m_host->setting(id(), key, def) : def;
}

void SpeechTool::setSetting(const QString &key, const QVariant &value)
{
    if (m_host)
        m_host->setSetting(id(), key, value);
}

QString SpeechTool::folder() const
{
    const QString f = setting(QStringLiteral("folder")).toString();
    return f.isEmpty() ? speech::defaultFolder() : f;
}

QString SpeechTool::modelsDir() const
{
    const QString d = (m_host ? m_host->dataDir(id()) : QDir::tempPath()) + QStringLiteral("/models");
    QDir().mkpath(d);
    return d;
}

QString SpeechTool::installedModelFile() const
{
    const QString f = setting(QStringLiteral("modelFile")).toString();
    return !f.isEmpty() && QFileInfo::exists(f) ? f : QString();
}

QString SpeechTool::installedModelId() const
{
    return installedModelFile().isEmpty() ? QString() : setting(QStringLiteral("model")).toString();
}

QString SpeechTool::speechDir() const
{
    const QString env = qEnvironmentVariable("QB_SPEECH_DIR");
    if (!env.isEmpty())
        return env;
    const QStringList dirs = m_host ? m_host->contentDirs(QStringLiteral("speech")) : QStringList();
    return dirs.value(0);
}

QString SpeechTool::samplePath() const
{
    const QString env = qEnvironmentVariable("QB_SPEECH_SAMPLE");
    return env.isEmpty() ? speechDir() + QStringLiteral("/samples/jfk.wav") : env;
}

speech::EngineSettings SpeechTool::engineSettings() const
{
    speech::EngineSettings s;
    s.modelFile = installedModelFile();
    const speech::ModelHeader h = speech::readModelHeader(s.modelFile);
    s.modelTranslates = !h.ok || h.translates();
    s.gpuIndex = setting(QStringLiteral("gpu"), -1).toInt();
    const int ideal = qMax(1, QThread::idealThreadCount());
    s.threads = qBound(1, setting(QStringLiteral("threads"), qMax(1, ideal - 1)).toInt(), ideal);
    s.mainLanguage = setting(QStringLiteral("main"), QStringLiteral("en")).toString();
    s.fallbackLanguage = setting(QStringLiteral("fallback"), QStringLiteral("de")).toString();
    s.keepOriginal = setting(QStringLiteral("keepOriginal"), false).toBool();
    s.careful = setting(QStringLiteral("careful"), false).toBool();
    s.hint = setting(QStringLiteral("hint")).toString();
    s.useContext = setting(QStringLiteral("useContext"), false).toBool();
    s.silenceGuard = setting(QStringLiteral("silenceGuard"), true).toBool();
    s.sensitivity = qBound(0, setting(QStringLiteral("sensitivity"), 1).toInt(), 2);
    s.pauseMs = qBound(300, setting(QStringLiteral("pauseMs"), 700).toInt(), 3000);
    s.paragraphMs = qBound(0, setting(QStringLiteral("paragraphMs"), 3000).toInt(), 30000);
    s.audioInput = setting(QStringLiteral("audioInput")).toByteArray();
    return s;
}

speech::EngineInterface *SpeechTool::engine(QString *error)
{
    if (m_engine)
        return m_engine;
    auto fail = [&](const QString &e) -> speech::EngineInterface * {
        m_engineError = e;
        if (error)
            *error = e;
        return nullptr;
    };
    if (!m_engineError.isEmpty())
        return fail(m_engineError);
    const QString dir = speechDir();
    const QString impl = dir + QStringLiteral("/libqb-speech-impl.so");
    if (dir.isEmpty() || !QFileInfo::exists(impl))
        return fail(tr("The add-on's speech engine is missing (install the add-on again)."));
    // Qt Multimedia comes with the add-on (the program has none): loaded by its full path first,
    // so the engine takes this copy (the same Qt as the program) whatever the system has
    const QString mm = dir + QStringLiteral("/lib/libQt6Multimedia.so.6");
    if (QFileInfo::exists(mm) && !dlopen(QFile::encodeName(mm).constData(), RTLD_NOW | RTLD_GLOBAL))
        return fail(tr("Cannot load Qt Multimedia: %1").arg(QString::fromLocal8Bit(dlerror())));
    if (QFileInfo::exists(dir + QStringLiteral("/qtplugins")))
        QCoreApplication::addLibraryPath(dir + QStringLiteral("/qtplugins"));
    void *h = dlopen(QFile::encodeName(impl).constData(), RTLD_NOW | RTLD_LOCAL);
    if (!h)
        return fail(tr("Cannot load the speech engine: %1").arg(QString::fromLocal8Bit(dlerror())));
    auto create = reinterpret_cast<SpeechCreateFn>(dlsym(h, "qb_speech_impl_create"));
    if (!create)
        return fail(tr("The speech engine is damaged (no entry point)."));
    m_engine = create();
    m_engine->onText = [this](const QString &t, bool p) { onText(t, p); };
    m_engine->onState = [this](const QString &s) { onState(s); };
    m_engine->onLevel = [this](float l) {
        if (m_panel)
            m_panel->setLevel(l);
    };
    m_engine->onError = [this](const QString &m) {
        m_lastError = m;
        if (m_panel)
            m_panel->setState(QStringLiteral("error ") + m);
    };
    return m_engine;
}

// ------------------------------------------------------------------ recording

bool SpeechTool::isBusy() const { return m_engine && m_engine->isBusy(); }
bool SpeechTool::isRecording() const { return m_engine && m_engine->isRecording(); }

void SpeechTool::toggleRecording()
{
    if (isRecording())
        stopRecording();
    else if (!isBusy())
        startRecording();
}

bool SpeechTool::startRecording(const QString &wavFile)
{
    m_lastError.clear();
    QWidget *parent = m_host ? m_host->window() : nullptr;
    if (installedModelFile().isEmpty()) {
        m_lastError = tr("No speech model is installed yet.");
        if (!quiet()) {
            QMessageBox box(QMessageBox::Information, tr("Speech Transcription"),
                            tr("No speech model is installed yet. Choose one under File > Options > Speech; it is "
                               "downloaded once, after that everything runs on this computer."),
                            QMessageBox::Cancel, parent);
            QPushButton *open = box.addButton(tr("Open the Options"), QMessageBox::AcceptRole);
            box.exec();
            if (box.clickedButton() == open && m_host && m_host->apiVersion() >= 110)
                m_host->showOptions(id());
        }
        return false;
    }
    QString error;
    speech::EngineInterface *e = engine(&error);
    if (!e) {
        m_lastError = error;
        if (!quiet())
            QMessageBox::warning(parent, tr("Speech Transcription"), error);
        return false;
    }
    m_recFile = speech::newTranscriptFile(folder(), QDateTime::currentDateTime());
    QFile f(m_recFile);
    if (!f.open(QIODevice::WriteOnly)) {
        m_lastError = tr("The transcription file cannot be written: %1").arg(f.errorString());
        if (!quiet())
            QMessageBox::warning(parent, tr("Speech Transcription"), m_lastError);
        return false;
    }
    f.close();
    m_tail.clear();
    if (!e->start(engineSettings(), &error, wavFile)) {
        QFile::remove(m_recFile);
        m_recFile.clear();
        m_lastError = error;
        if (!quiet())
            QMessageBox::warning(parent, tr("Speech Transcription"), error);
        return false;
    }
    if (m_host && m_host->apiVersion() >= 110)
        m_host->showInspectorTab(kTab);
    if (m_panel)
        m_panel->recordingStarted(m_recFile);
    updateCommand();
    emit recordingChanged(true);
    return true;
}

void SpeechTool::stopRecording()
{
    if (m_engine)
        m_engine->stop();
    updateCommand();
}

void SpeechTool::onText(const QString &text, bool newParagraph)
{
    const QString add = speech::joinPiece(m_tail, text, newParagraph);
    if (add.isEmpty() || m_recFile.isEmpty())
        return;
    // written at once: what was said is safe even if the program ends
    QFile f(m_recFile);
    if (f.open(QIODevice::Append)) {
        f.write(add.toUtf8());
        f.close();
    }
    m_tail = (m_tail + add).right(8);
    if (m_panel)
        m_panel->appendPiece(add);
}

void SpeechTool::onState(const QString &state)
{
    if (m_panel)
        m_panel->setState(state);
    if (state == QLatin1String("finished")) {
        updateCommand();
        emit recordingChanged(false);
    }
}

void SpeechTool::updateCommand()
{
    if (!m_host || m_host->apiVersion() < 110)
        return;
    const bool on = isRecording();
    m_host->setCommandState(kRecord, on, on ? QStringLiteral("media-record") : QString(),
                            on ? tr("Stop speech transcription") : QString());
    if (m_panel)
        m_panel->setRecording(on);
}

// ------------------------------------------------------------------ models

void SpeechTool::downloadModel(const QString &id)
{
    const speech::CatalogEntry *e = speech::catalogEntry(id);
    if (!e || m_job != Job::None)
        return;
    // Qt's TLS plugin comes with the add-on (the program has none); proxies as the system says
    static bool tls = false;
    if (!tls) {
        tls = true;
        if (QFileInfo::exists(speechDir() + QStringLiteral("/qtplugins")))
            QCoreApplication::addLibraryPath(speechDir() + QStringLiteral("/qtplugins"));
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    }
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    const QString other = setting(QStringLiteral("source")).toString() == QLatin1String("other")
                              ? setting(QStringLiteral("otherAddress")).toString()
                              : QString();
    const QUrl url(speech::downloadUrl(*e, other));
    m_job = Job::Download;
    m_jobId = id;
    m_cancelled = false;
    m_partFile = modelsDir() + QLatin1Char('/') + e->file + QStringLiteral(".part");
    const qint64 have = QFileInfo(m_partFile).size();
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("%1/%2").arg(m_host ? m_host->appName() : QStringLiteral("Quillbinder"),
                                             m_host ? m_host->appVersion() : QString()));
    if (have > 0) // an interrupted download goes on where it stopped
        req.setRawHeader("Range", "bytes=" + QByteArray::number(have) + '-');
    m_modelMessage = tr("Downloading %1 from %2 ...").arg(e->title, url.host());
    auto *file = new QFile(m_partFile, this);
    if (!file->open(QIODevice::ReadWrite)) {
        delete file;
        finishJob(false, tr("The model cannot be saved in %1.").arg(modelsDir()));
        return;
    }
    file->seek(file->size());
    m_reply = m_net->get(req);
    auto first = std::make_shared<bool>(true);
    connect(m_reply, &QNetworkReply::readyRead, this, [this, file, first, have] {
        if (*first) {
            *first = false;
            // the server sent the whole file instead of the rest
            if (have > 0 && m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
                file->resize(0);
                file->seek(0);
            }
        }
        file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, have](qint64 done, qint64 total) {
        const bool rest = m_reply && m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 206;
        emit modelProgress((rest ? have : 0) + done, total > 0 ? (rest ? have : 0) + total : -1);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, file, e] {
        file->write(m_reply->readAll());
        file->close();
        file->deleteLater();
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError err = m_reply->error();
        const QString errText = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_cancelled) {
            finishJob(false, tr("Download stopped. A new download continues where this one stopped."));
            return;
        }
        if (status == 416) { // the part on disk is complete already
            checkAndInstall(m_partFile, e->id, true);
            return;
        }
        if (err != QNetworkReply::NoError) {
            finishJob(false, tr("The download failed: %1. Try again (it continues where it stopped), choose another "
                                "address, or use a file you downloaded yourself.")
                                 .arg(errText));
            return;
        }
        checkAndInstall(m_partFile, e->id, true);
    });
}

void SpeechTool::cancelDownload()
{
    if (m_job == Job::Download && m_reply) {
        m_cancelled = true;
        m_reply->abort();
    }
}

void SpeechTool::useLocalModel(const QString &file)
{
    if (m_job != Job::None || !QFileInfo::exists(file))
        return;
    m_job = Job::Check;
    m_jobId.clear();
    // copied next to the other models first (the original stays where it is)
    const QString part = modelsDir() + QLatin1Char('/') + QFileInfo(file).fileName() + QStringLiteral(".part");
    m_modelMessage = tr("Copying %1 ...").arg(QFileInfo(file).fileName());
    QFile::remove(part);
    QThread *t = QThread::create([file, part] { QFile::copy(file, part); });
    connect(t, &QThread::finished, this, [this, t, part] {
        t->deleteLater();
        if (!QFileInfo::exists(part)) {
            finishJob(false, tr("The file could not be copied to %1.").arg(modelsDir()));
            return;
        }
        checkAndInstall(part, QString(), false);
    });
    t->start();
}

void SpeechTool::checkAndInstall(const QString &file, const QString &expectedId, bool fromDownload)
{
    Q_UNUSED(fromDownload)
    m_job = Job::Check;
    m_modelMessage = tr("Checking the model (checksum, description, test transcription) ...");
    emit modelProgress(0, 0);
    QString engineError;
    speech::EngineInterface *e = engine(&engineError);
    const speech::EngineSettings settings = [this, file] {
        speech::EngineSettings s = engineSettings();
        s.modelFile = file;
        return s;
    }();
    const QString sample = samplePath();
    struct Result {
        QString sha1;
        speech::ModelHeader header;
        speech::TestResult test;
    };
    auto result = std::make_shared<Result>();
    QThread *t = QThread::create([file, e, settings, sample, result] {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            QCryptographicHash h(QCryptographicHash::Sha1);
            h.addData(&f);
            result->sha1 = QString::fromLatin1(h.result().toHex());
        }
        result->header = speech::readModelHeader(file);
        if (result->header.ok && e)
            result->test = e->transcribeFile(sample, settings, false);
    });
    connect(t, &QThread::finished, this, [=] {
        t->deleteLater();
        const speech::CatalogEntry *entry = expectedId.isEmpty() ? speech::catalogEntryBySha1(result->sha1)
                                                                 : speech::catalogEntry(expectedId);
        if (!entry && expectedId.isEmpty() && result->header.ok)
            entry = speech::catalogEntry(result->header.kind()); // a file of a known kind, another version
        const bool sumOk = entry && entry->sha1.compare(result->sha1, Qt::CaseInsensitive) == 0;
        QString why;
        const bool headOk = entry ? speech::headerMatches(result->header, *entry, &why)
                                  : (result->header.ok && result->header.multilingual());
        if (!headOk) {
            QFile::remove(file);
            finishJob(false, tr("The file is not a usable model: %1")
                                 .arg(why.isEmpty() ? (result->header.error.isEmpty() ? tr("it knows English only")
                                                                                     : result->header.error)
                                                    : why));
            return;
        }
        if (!e) {
            finishJob(false, engineError);
            return;
        }
        // the sample says "... ask not what your country can do for you ..." (J. F. Kennedy, 1961)
        const bool testOk = result->test.ok && result->test.text.contains(QStringLiteral("country"), Qt::CaseInsensitive);
        if (!testOk && !qEnvironmentVariableIsSet("QB_SPEECH_SKIP_TEST")) {
            QFile::remove(file);
            const QString got = result->test.error.isEmpty()
                                    ? tr("it wrote \"%1\"").arg(result->test.text.left(80))
                                    : result->test.error;
            finishJob(false, tr("The model did not pass the test transcription (%1). The model in use stays.").arg(got));
            return;
        }
        if (entry && !sumOk) {
            bool yes = m_acceptChanged || qEnvironmentVariableIsSet("QB_SPEECH_ACCEPT_CHANGED");
            if (!yes && !quiet())
                yes = QMessageBox::question(m_host ? m_host->window() : nullptr, tr("Speech Transcription"),
                                            tr("The file differs from the one Quillbinder knows for \"%1\" (the source may "
                                               "have updated it). It passed the check of its description and the test "
                                               "transcription. Use it anyway?")
                                                .arg(entry->title))
                      == QMessageBox::Yes;
            if (!yes) {
                QFile::remove(file);
                finishJob(false, tr("Not installed: the file differs from the known one. The model in use stays."));
                return;
            }
        }
        // in place of the old model, which goes only now
        QString target = file;
        if (target.endsWith(QLatin1String(".part")))
            target.chop(5);
        if (entry && sumOk)
            target = modelsDir() + QLatin1Char('/') + entry->file;
        const QString previous = installedModelFile();
        if (m_engine)
            m_engine->unloadModel();
        if (target != file) {
            if (target == previous)
                QFile::remove(previous);
            QFile::remove(target);
            QFile::rename(file, target);
        }
        setSetting(QStringLiteral("model"), entry ? entry->id : QStringLiteral("file"));
        setSetting(QStringLiteral("modelFile"), target);
        if (!previous.isEmpty() && previous != target && QFileInfo(previous).absolutePath() == QFileInfo(modelsDir()).absoluteFilePath())
            QFile::remove(previous);
        const double ratio = result->test.audioSeconds > 0 ? result->test.seconds / result->test.audioSeconds : 0;
        finishJob(true, tr("Installed: %1 (%2). Test transcription passed: %3 s of speech took %4 s.")
                            .arg(entry ? entry->title : QFileInfo(target).fileName(), sizeText(QFileInfo(target).size()))
                            .arg(result->test.audioSeconds, 0, 'f', 1)
                            .arg(result->test.seconds, 0, 'f', 1)
                        + (ratio > 1.0 ? QLatin1Char(' ') + tr("On this computer the model is slower than speech: in long "
                                                               "stretches the text falls behind.")
                                       : QString()));
    });
    t->start();
}

void SpeechTool::finishJob(bool ok, const QString &message)
{
    m_job = Job::None;
    m_modelMessage = message;
    emit modelJobFinished(ok, message);
}

bool SpeechTool::removeModel()
{
    const QString f = installedModelFile();
    if (f.isEmpty() || isBusy() || m_job != Job::None)
        return false;
    if (!quiet()
        && QMessageBox::question(m_host ? m_host->window() : nullptr, tr("Speech Transcription"),
                                 tr("Remove the model %1? Speech transcription needs a model; it can be downloaded again.")
                                     .arg(QFileInfo(f).fileName()))
               != QMessageBox::Yes)
        return false;
    if (m_engine)
        m_engine->unloadModel();
    QFile::remove(f);
    setSetting(QStringLiteral("modelFile"), QString());
    setSetting(QStringLiteral("model"), QString());
    m_modelMessage = tr("The model was removed.");
    return true;
}

// ==================================================================== the inspector tab

TranscriptPanel::TranscriptPanel(SpeechTool *tool, QWidget *parent) : QWidget(parent), m_tool(tool)
{
    setObjectName(QStringLiteral("speechPanel"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto *bar = new QHBoxLayout;
    auto button = [this, bar](const QString &icon, const QString &tip, const char *name) {
        auto *b = new QToolButton(this);
        b->setIcon(QIcon::fromTheme(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setObjectName(QLatin1String(name));
        bar->addWidget(b);
        return b;
    };
    m_record = button(QStringLiteral("audio-input-microphone"), tr("Start or stop speech transcription"), "speechRecord");
    m_record->setCheckable(true);
    m_rename = button(QStringLiteral("edit-rename"), tr("Rename the transcription"), "speechRename");
    m_copy = button(QStringLiteral("edit-copy"), tr("Copy the whole text"), "speechCopy");
    m_delete = button(QStringLiteral("edit-delete"), tr("Move the transcription to the trash"), "speechDelete");
    bar->addStretch(1);
    m_folderButton = button(QStringLiteral("folder-open"), tr("Show the folder of the transcriptions"), "speechFolder");
    lay->addLayout(bar);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("speechList"));
    m_list->setMaximumHeight(150);
    lay->addWidget(m_list);
    m_edit = new QPlainTextEdit(this);
    m_edit->setObjectName(QStringLiteral("speechText"));
    m_edit->setPlaceholderText(tr("Choose a transcription, or press the microphone to start one."));
    lay->addWidget(m_edit, 1);
    m_level = new QProgressBar(this);
    m_level->setObjectName(QStringLiteral("speechLevel"));
    m_level->setRange(0, 100);
    m_level->setTextVisible(false);
    m_level->setMaximumHeight(6);
    m_level->hide();
    lay->addWidget(m_level);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("speechStatus"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(800);
    connect(m_saveTimer, &QTimer::timeout, this, &TranscriptPanel::save);
    m_watch = new QFileSystemWatcher(this);
    connect(m_watch, &QFileSystemWatcher::directoryChanged, this, [this] { QTimer::singleShot(200, this, &TranscriptPanel::refresh); });

    connect(m_record, &QToolButton::clicked, m_tool, &SpeechTool::toggleRecording);
    connect(m_rename, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString t = QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal,
                                                QFileInfo(m_file).completeBaseName(), &ok);
        if (ok && !t.trimmed().isEmpty())
            rename(t.trimmed());
    });
    connect(m_copy, &QToolButton::clicked, this, &TranscriptPanel::copyAll);
    connect(m_delete, &QToolButton::clicked, this, [this] { remove(true); });
    connect(m_folderButton, &QToolButton::clicked, this, [this] {
        QDir().mkpath(m_tool->folder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_tool->folder()));
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_loading || row < 0)
            return;
        save();
        load(m_list->item(row)->data(Qt::UserRole).toString());
    });
    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_loading && !m_file.isEmpty() && m_file != m_recordingFile)
            m_saveTimer->start();
    });
    connect(m_tool, &SpeechTool::recordingChanged, this, &TranscriptPanel::setRecording);
    refresh();
    setState(QString());
}

TranscriptPanel::~TranscriptPanel()
{
    save();
}

void TranscriptPanel::refresh()
{
    const QString folder = m_tool->folder();
    if (folder != m_folder) {
        if (!m_watch->directories().isEmpty())
            m_watch->removePaths(m_watch->directories());
        m_folder = folder;
        if (QFileInfo::exists(folder))
            m_watch->addPath(folder);
    }
    m_loading = true;
    m_list->clear();
    int row = -1;
    for (const speech::Transcript &t : speech::transcriptsIn(folder)) {
        auto *it = new QListWidgetItem(t.title, m_list);
        it->setData(Qt::UserRole, t.file);
        if (t.file == m_file)
            row = m_list->count() - 1;
    }
    m_list->setCurrentRow(row);
    m_loading = false;
    if (row < 0 && !m_file.isEmpty() && !QFileInfo::exists(m_file)) {
        m_file.clear();
        m_loading = true;
        m_edit->clear();
        m_loading = false;
    }
    updateButtons();
}

void TranscriptPanel::load(const QString &file)
{
    m_loading = true;
    m_file = file;
    QFile f(file);
    m_edit->setPlainText(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
    m_edit->setReadOnly(file == m_recordingFile && m_tool->isBusy());
    m_loading = false;
    updateButtons();
}

bool TranscriptPanel::selectTranscript(const QString &title)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->text() == title) {
            m_list->setCurrentRow(i);
            return true;
        }
    return false;
}

QStringList TranscriptPanel::titles() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
        out << m_list->item(i)->text();
    return out;
}

QString TranscriptPanel::text() const { return m_edit->toPlainText(); }

void TranscriptPanel::setText(const QString &text)
{
    m_edit->setPlainText(text);
}

void TranscriptPanel::save()
{
    m_saveTimer->stop();
    if (m_file.isEmpty() || m_loading || (m_file == m_recordingFile && m_tool->isBusy()))
        return;
    QFile cur(m_file);
    const QByteArray now = m_edit->toPlainText().toUtf8();
    if (cur.open(QIODevice::ReadOnly) && cur.readAll() == now)
        return;
    cur.close();
    QSaveFile f(m_file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(now);
        f.commit();
    }
}

bool TranscriptPanel::rename(const QString &title)
{
    if (m_file.isEmpty() || (m_file == m_recordingFile && m_tool->isBusy()))
        return false;
    save();
    QString safe = title;
    safe.replace(QLatin1Char('/'), QLatin1Char('-'));
    const QString target = QFileInfo(m_file).absolutePath() + QLatin1Char('/') + safe + QStringLiteral(".txt");
    if (QFileInfo::exists(target) || !QFile::rename(m_file, target))
        return false;
    m_file = target;
    refresh();
    return true;
}

bool TranscriptPanel::remove(bool ask)
{
    if (m_file.isEmpty() || (m_file == m_recordingFile && m_tool->isBusy()))
        return false;
    if (ask && !qEnvironmentVariableIsSet("QB_NO_DIALOGS")
        && QMessageBox::question(this, tr("Speech Transcription"),
                                 tr("Move \"%1\" to the trash?").arg(QFileInfo(m_file).completeBaseName()))
               != QMessageBox::Yes)
        return false;
    m_saveTimer->stop();
    const QString f = m_file;
    m_file.clear();
    if (!QFile::moveToTrash(f))
        QFile::remove(f);
    m_loading = true;
    m_edit->clear();
    m_loading = false;
    refresh();
    return true;
}

void TranscriptPanel::copyAll()
{
    QApplication::clipboard()->setText(m_edit->toPlainText());
    m_status->setText(tr("The text is on the clipboard."));
}

QString TranscriptPanel::status() const { return m_status->text(); }

void TranscriptPanel::recordingStarted(const QString &file)
{
    save();
    m_recordingFile = file;
    refresh();
    selectTranscript(QFileInfo(file).completeBaseName());
    if (m_file != file)
        load(file);
    m_edit->setReadOnly(true);
    m_level->setValue(0);
    m_level->show();
    setRecording(true);
}

void TranscriptPanel::appendPiece(const QString &added)
{
    if (m_file != m_recordingFile)
        return;
    m_loading = true;
    QTextCursor c(m_edit->document());
    c.movePosition(QTextCursor::End);
    c.insertText(added);
    m_edit->ensureCursorVisible();
    m_loading = false;
}

void TranscriptPanel::setLevel(float level)
{
    m_level->setValue(qRound(level * 100));
}

void TranscriptPanel::setState(const QString &state)
{
    QString text;
    if (state.isEmpty()) {
        text = m_tool->installedModelFile().isEmpty()
                   ? tr("No speech model yet: File > Options > Speech.")
                   : tr("Ready. Press the microphone (toolbar or above) to start.");
    } else if (state == QLatin1String("loading")) {
        text = tr("Loading the model ...");
    } else if (state == QLatin1String("listening")) {
        text = tr("Listening: speak now. A pause ends a piece; its text follows a few seconds later.");
    } else if (state.startsWith(QLatin1String("transcribing"))) {
        text = tr("Writing the last pieces (%1 waiting) ...").arg(state.section(QLatin1Char(' '), 1));
    } else if (state == QLatin1String("finished")) {
        text = tr("Finished. The text can be edited and copied now.");
    } else if (state.startsWith(QLatin1String("error "))) {
        text = state.mid(6);
    }
    m_status->setText(text);
}

void TranscriptPanel::setRecording(bool on)
{
    QSignalBlocker block(m_record);
    m_record->setChecked(on);
    m_record->setIcon(QIcon::fromTheme(on ? QStringLiteral("media-record") : QStringLiteral("audio-input-microphone")));
    if (!on) {
        m_level->hide();
        if (!m_tool->isBusy()) {
            if (m_file == m_recordingFile)
                m_edit->setReadOnly(false);
            m_recordingFile.clear();
        }
    }
    updateButtons();
}

void TranscriptPanel::updateButtons()
{
    const bool has = !m_file.isEmpty();
    const bool locked = has && m_file == m_recordingFile && m_tool->isBusy();
    m_rename->setEnabled(has && !locked);
    m_delete->setEnabled(has && !locked);
    m_copy->setEnabled(has);
}

// ==================================================================== File > Options > Speech

namespace {

class SpeechOptions : public QObject, public OptionsSection
{
public:
    SpeechOptions(SpeechTool *tool, QWidget *parent);
    QWidget *widget() override { return m_w; }
    void apply() override;

private:
    void showModel();
    void fillDevices();
    void updateLanguageNote();
    void setBusy(bool busy);

    SpeechTool *m_tool;
    QWidget *m_w;
    QComboBox *m_model, *m_device, *m_main, *m_fallback, *m_accuracy, *m_sensitivity, *m_input;
    QLabel *m_impact, *m_installed, *m_message, *m_deviceNote, *m_langNote, *m_speed;
    QPushButton *m_download, *m_local, *m_remove, *m_cancel, *m_speedTest;
    QRadioButton *m_official, *m_other;
    QLineEdit *m_otherAddress, *m_folder;
    QProgressBar *m_progress;
    QWidget *m_progressRow;
    QSpinBox *m_threads;
    QDoubleSpinBox *m_pause, *m_paragraph;
    QCheckBox *m_keepOriginal, *m_guard, *m_context;
    QPlainTextEdit *m_hint;
};

SpeechOptions::SpeechOptions(SpeechTool *tool, QWidget *parent) : QObject(nullptr), m_tool(tool)
{
    m_w = new QWidget(parent);
    m_w->setObjectName(QStringLiteral("speechOptions"));
    auto *lay = new QVBoxLayout(m_w);
    auto note = [this](const QString &text = QString()) {
        auto *l = new QLabel(text, m_w);
        l->setWordWrap(true);
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        l->setFont(f);
        return l;
    };

    // ---- model
    auto *modelBox = new QGroupBox(tr("Model (speech recognition on this computer)"), m_w);
    auto *mf = new QFormLayout(modelBox);
    m_model = new QComboBox(modelBox);
    m_model->setObjectName(QStringLiteral("speechModel"));
    for (const speech::CatalogEntry &e : speech::catalog())
        m_model->addItem(e.id == speech::recommendedModel() ? tr("%1 (recommended)").arg(e.title) : e.title, e.id);
    mf->addRow(tr("Model:"), m_model);
    m_impact = note();
    m_impact->setObjectName(QStringLiteral("speechImpact"));
    mf->addRow(QString(), m_impact);
    m_installed = note();
    m_installed->setObjectName(QStringLiteral("speechInstalled"));
    mf->addRow(tr("Installed:"), m_installed);
    auto *buttons = new QHBoxLayout;
    m_download = new QPushButton(tr("Download and Use"), modelBox);
    m_download->setObjectName(QStringLiteral("speechDownload"));
    m_local = new QPushButton(tr("Use a File I Downloaded…"), modelBox);
    m_local->setObjectName(QStringLiteral("speechLocal"));
    m_remove = new QPushButton(tr("Remove"), modelBox);
    m_remove->setObjectName(QStringLiteral("speechRemove"));
    buttons->addWidget(m_download);
    buttons->addWidget(m_local);
    buttons->addWidget(m_remove);
    buttons->addStretch(1);
    mf->addRow(QString(), buttons);
    m_official = new QRadioButton(tr("whisper.cpp's model files on Hugging Face (the usable original source)"), modelBox);
    m_official->setObjectName(QStringLiteral("speechSourceOfficial"));
    m_other = new QRadioButton(tr("Other address:"), modelBox);
    m_other->setObjectName(QStringLiteral("speechSourceOther"));
    m_otherAddress = new QLineEdit(modelBox);
    m_otherAddress->setObjectName(QStringLiteral("speechOtherAddress"));
    m_otherAddress->setPlaceholderText(tr("https://... (a folder with the model files, or the address of one file)"));
    auto *otherRow = new QHBoxLayout;
    otherRow->addWidget(m_other);
    otherRow->addWidget(m_otherAddress, 1);
    mf->addRow(tr("Source:"), m_official);
    mf->addRow(QString(), otherRow);
    m_progressRow = new QWidget(modelBox);
    auto *prog = new QHBoxLayout(m_progressRow);
    prog->setContentsMargins(0, 0, 0, 0);
    m_progress = new QProgressBar(m_progressRow);
    m_progress->setObjectName(QStringLiteral("speechProgress"));
    m_cancel = new QPushButton(tr("Stop"), m_progressRow);
    m_cancel->setObjectName(QStringLiteral("speechCancel"));
    prog->addWidget(m_progress, 1);
    prog->addWidget(m_cancel);
    mf->addRow(m_progressRow);
    m_message = note();
    m_message->setObjectName(QStringLiteral("speechMessage"));
    mf->addRow(QString(), m_message);
    mf->addRow(QString(), note(tr("The model is downloaded once, when you choose it; after that nothing leaves this "
                                  "computer. Before it is used it is checked: its checksum, the description of the model "
                                  "in the file, and a short test transcription. Only then does it replace the model in "
                                  "use, which is deleted.")));
    lay->addWidget(modelBox);

    // ---- processing
    auto *procBox = new QGroupBox(tr("Processing"), m_w);
    auto *pf = new QFormLayout(procBox);
    m_device = new QComboBox(procBox);
    m_device->setObjectName(QStringLiteral("speechDevice"));
    pf->addRow(tr("Compute on:"), m_device);
    m_deviceNote = note();
    pf->addRow(QString(), m_deviceNote);
    m_threads = new QSpinBox(procBox);
    m_threads->setObjectName(QStringLiteral("speechThreads"));
    const int ideal = qMax(1, QThread::idealThreadCount());
    m_threads->setRange(1, ideal);
    pf->addRow(tr("Processor threads:"), m_threads);
    pf->addRow(QString(), note(tr("One less than the processor has keeps typing smooth while a piece is written.")));
    m_accuracy = new QComboBox(procBox);
    m_accuracy->setObjectName(QStringLiteral("speechAccuracy"));
    m_accuracy->addItem(tr("Fast"));
    m_accuracy->addItem(tr("Careful (slower: weighs several possible texts, finds the language of each piece)"));
    pf->addRow(tr("Accuracy:"), m_accuracy);
    m_speedTest = new QPushButton(tr("Speed Test"), procBox);
    m_speedTest->setObjectName(QStringLiteral("speechSpeedTest"));
    m_speed = note();
    m_speed->setObjectName(QStringLiteral("speechSpeed"));
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(m_speedTest);
    speedRow->addWidget(m_speed, 1);
    pf->addRow(QString(), speedRow);
    lay->addWidget(procBox);

    // ---- languages
    auto *langBox = new QGroupBox(tr("Languages"), m_w);
    auto *lf = new QFormLayout(langBox);
    m_main = new QComboBox(langBox);
    m_main->setObjectName(QStringLiteral("speechMain"));
    m_fallback = new QComboBox(langBox);
    m_fallback->setObjectName(QStringLiteral("speechFallback"));
    m_fallback->addItem(tr("(none)"), QString());
    QList<speech::Language> langs = speech::languages();
    std::sort(langs.begin(), langs.end(), [](const speech::Language &a, const speech::Language &b) { return a.name < b.name; });
    for (const speech::Language &l : langs) {
        m_main->addItem(l.name, l.code);
        m_fallback->addItem(l.name, l.code);
    }
    lf->addRow(tr("Main language (the text):"), m_main);
    lf->addRow(tr("Fallback (also spoken):"), m_fallback);
    m_langNote = note();
    m_langNote->setObjectName(QStringLiteral("speechLanguageNote"));
    lf->addRow(QString(), m_langNote);
    m_keepOriginal = new QCheckBox(tr("Keep what was said beside a translation, in brackets (takes longer)"), langBox);
    m_keepOriginal->setObjectName(QStringLiteral("speechKeepOriginal"));
    lf->addRow(QString(), m_keepOriginal);
    m_hint = new QPlainTextEdit(langBox);
    m_hint->setObjectName(QStringLiteral("speechHint"));
    m_hint->setMaximumHeight(70);
    m_hint->setPlaceholderText(tr("Names and words you use, in the main language's spelling: Eleanor, Brackwater, Bahnhof ..."));
    lf->addRow(tr("Hint words:"), m_hint);
    lay->addWidget(langBox);

    // ---- microphone and pauses
    auto *micBox = new QGroupBox(tr("Microphone and pauses"), m_w);
    auto *xf = new QFormLayout(micBox);
    m_input = new QComboBox(micBox);
    m_input->setObjectName(QStringLiteral("speechInput"));
    xf->addRow(tr("Microphone:"), m_input);
    m_sensitivity = new QComboBox(micBox);
    m_sensitivity->setObjectName(QStringLiteral("speechSensitivity"));
    m_sensitivity->addItems({tr("Low (noisy room)"), tr("Medium"), tr("High (quiet voice)")});
    xf->addRow(tr("Sensitivity:"), m_sensitivity);
    m_pause = new QDoubleSpinBox(micBox);
    m_pause->setObjectName(QStringLiteral("speechPause"));
    m_pause->setRange(0.3, 3.0);
    m_pause->setSingleStep(0.1);
    m_pause->setDecimals(1);
    m_pause->setSuffix(tr(" s"));
    xf->addRow(tr("A pause that ends a piece:"), m_pause);
    m_paragraph = new QDoubleSpinBox(micBox);
    m_paragraph->setObjectName(QStringLiteral("speechParagraph"));
    m_paragraph->setRange(0, 30);
    m_paragraph->setSingleStep(0.5);
    m_paragraph->setDecimals(1);
    m_paragraph->setSuffix(tr(" s"));
    m_paragraph->setSpecialValueText(tr("never"));
    xf->addRow(tr("New paragraph after a pause of:"), m_paragraph);
    lay->addWidget(micBox);

    // ---- files and advanced
    auto *fileBox = new QGroupBox(tr("Transcriptions"), m_w);
    auto *ff = new QHBoxLayout(fileBox);
    m_folder = new QLineEdit(fileBox);
    m_folder->setObjectName(QStringLiteral("speechFolder"));
    auto *choose = new QPushButton(tr("Choose…"), fileBox);
    ff->addWidget(new QLabel(tr("Folder:"), fileBox));
    ff->addWidget(m_folder, 1);
    ff->addWidget(choose);
    lay->addWidget(fileBox);
    auto *advBox = new QGroupBox(tr("Advanced"), m_w);
    auto *af = new QVBoxLayout(advBox);
    m_guard = new QCheckBox(tr("Leave out text in silence and noise (whisper sometimes invents text there)"), advBox);
    m_guard->setObjectName(QStringLiteral("speechGuard"));
    m_context = new QCheckBox(tr("Use the text before as context for the next piece (smoother, but mistakes can repeat)"), advBox);
    m_context->setObjectName(QStringLiteral("speechContext"));
    af->addWidget(m_guard);
    af->addWidget(m_context);
    lay->addWidget(advBox);
    lay->addStretch(1);

    // ---- values
    const QString modelId = m_tool->setting(QStringLiteral("model")).toString();
    const int mi = m_model->findData(speech::catalogEntry(modelId) ? modelId : speech::recommendedModel());
    m_model->setCurrentIndex(qMax(0, mi));
    const bool other = m_tool->setting(QStringLiteral("source")).toString() == QLatin1String("other");
    m_official->setChecked(!other);
    m_other->setChecked(other);
    m_otherAddress->setText(m_tool->setting(QStringLiteral("otherAddress")).toString());
    const speech::EngineSettings s = m_tool->engineSettings();
    m_threads->setValue(s.threads);
    m_accuracy->setCurrentIndex(s.careful ? 1 : 0);
    m_main->setCurrentIndex(qMax(0, m_main->findData(s.mainLanguage)));
    m_fallback->setCurrentIndex(qMax(0, m_fallback->findData(s.fallbackLanguage)));
    m_keepOriginal->setChecked(s.keepOriginal);
    m_hint->setPlainText(s.hint);
    m_sensitivity->setCurrentIndex(s.sensitivity);
    m_pause->setValue(s.pauseMs / 1000.0);
    m_paragraph->setValue(s.paragraphMs / 1000.0);
    m_folder->setText(m_tool->folder());
    m_guard->setChecked(s.silenceGuard);
    m_context->setChecked(s.useContext);
    m_progressRow->hide();

    // microphones and graphics cards need the engine; without it the lists say why
    QString error;
    speech::EngineInterface *e = m_tool->engine(&error);
    m_input->addItem(tr("The system's default input"), QByteArray());
    if (e)
        for (const speech::AudioInput &in : e->audioInputs())
            m_input->addItem(in.name, in.id);
    m_input->setCurrentIndex(qMax(0, m_input->findData(s.audioInput)));
    fillDevices();
    showModel();
    updateLanguageNote();
    if (!error.isEmpty())
        m_message->setText(error);

    connect(m_model, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        showModel();
        fillDevices();
        updateLanguageNote();
    });
    connect(m_main, qOverload<int>(&QComboBox::currentIndexChanged), this, &SpeechOptions::updateLanguageNote);
    connect(m_fallback, qOverload<int>(&QComboBox::currentIndexChanged), this, &SpeechOptions::updateLanguageNote);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(m_w, tr("Folder of the Transcriptions"), m_folder->text());
        if (!d.isEmpty())
            m_folder->setText(d);
    });
    connect(m_download, &QPushButton::clicked, this, [this] {
        apply(); // the source and the device as shown
        setBusy(true);
        m_tool->downloadModel(m_model->currentData().toString());
    });
    connect(m_local, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(m_w, tr("Model File"), QDir::homePath(),
                                                       tr("whisper.cpp models (*.bin);;All files (*)"));
        if (f.isEmpty())
            return;
        apply();
        setBusy(true);
        m_tool->useLocalModel(f);
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        m_tool->removeModel();
        m_message->setText(m_tool->modelMessage());
        showModel();
    });
    connect(m_cancel, &QPushButton::clicked, m_tool, &SpeechTool::cancelDownload);
    connect(m_tool, &SpeechTool::modelProgress, this, [this](qint64 done, qint64 total) {
        m_message->setText(m_tool->modelMessage());
        if (total > 0) {
            m_progress->setRange(0, 1000);
            m_progress->setValue(int(done * 1000 / total));
            m_progress->setFormat(QStringLiteral("%1 / %2").arg(sizeText(done), sizeText(total)));
        } else {
            m_progress->setRange(0, 0);
        }
    });
    connect(m_tool, &SpeechTool::modelJobFinished, this, [this](bool, const QString &msg) {
        setBusy(false);
        m_message->setText(msg);
        showModel();
        fillDevices();
    });
    connect(m_speedTest, &QPushButton::clicked, this, [this] {
        apply();
        QString err;
        speech::EngineInterface *eng = m_tool->engine(&err);
        if (!eng || m_tool->installedModelFile().isEmpty()) {
            m_speed->setText(eng ? tr("Install a model first.") : err);
            return;
        }
        m_speedTest->setEnabled(false);
        m_speed->setText(tr("Measuring ..."));
        auto res = std::make_shared<speech::TestResult>();
        const speech::EngineSettings st = m_tool->engineSettings();
        const QString sample = m_tool->samplePath();
        QThread *t = QThread::create([eng, st, sample, res] { *res = eng->transcribeFile(sample, st, false); });
        connect(t, &QThread::finished, this, [this, t, res] {
            t->deleteLater();
            m_speedTest->setEnabled(true);
            if (!res->ok) {
                m_speed->setText(res->error);
                return;
            }
            const double ratio = res->audioSeconds > 0 ? res->seconds / res->audioSeconds : 0;
            m_speed->setText(tr("%1 s of speech took %2 s (%3 × the time of the speech; loading the model %4 s). %5")
                                 .arg(res->audioSeconds, 0, 'f', 1)
                                 .arg(res->seconds, 0, 'f', 1)
                                 .arg(ratio, 0, 'f', 2)
                                 .arg(res->loadSeconds, 0, 'f', 1)
                                 .arg(ratio <= 0.8 ? tr("It keeps up with speech.")
                                                   : tr("It is slower than speech: the text falls behind in long stretches.")));
        });
        t->start();
    });
}

void SpeechOptions::showModel()
{
    const speech::CatalogEntry *e = speech::catalogEntry(m_model->currentData().toString());
    m_impact->setText(e ? e->impact : QString());
    const QString file = m_tool->installedModelFile();
    if (file.isEmpty()) {
        m_installed->setText(tr("none yet"));
    } else {
        const speech::CatalogEntry *in = speech::catalogEntry(m_tool->installedModelId());
        m_installed->setText(tr("%1 (%2, %3)").arg(in ? in->title : QFileInfo(file).fileName(), sizeText(QFileInfo(file).size()),
                                                  QFileInfo(file).fileName()));
    }
    m_remove->setEnabled(!file.isEmpty());
    m_download->setText(e && m_tool->installedModelId() == e->id ? tr("Download Again") : tr("Download and Use"));
}

void SpeechOptions::fillDevices()
{
    const int chosen = m_device->count() ? m_device->currentData().toInt() : m_tool->setting(QStringLiteral("gpu"), -1).toInt();
    m_device->clear();
    QString error;
    speech::EngineInterface *e = m_tool->engine(&error);
    QStringList notes;
    if (!e) {
        m_device->addItem(tr("Processor"), -1);
        m_deviceNote->setText(error);
        return;
    }
    const QString id = m_model->currentData().toString();
    for (const speech::ComputeDevice &d : e->computeDevices()) {
        if (d.gpuIndex < 0) {
            m_device->addItem(d.name, -1);
            continue;
        }
        const bool enough = d.memoryTotal <= 0 || d.memoryTotal >= memoryNeed(id);
        m_device->addItem(QStringLiteral("%1 (%2)").arg(d.name, d.memoryTotal > 0 ? sizeText(d.memoryTotal) : tr("memory unknown")),
                          d.gpuIndex);
        if (!enough) {
            // shown, but not chosen: the model does not fit into the card's memory
            auto *model = qobject_cast<QStandardItemModel *>(m_device->model());
            if (model)
                model->item(m_device->count() - 1)->setEnabled(false);
            notes << tr("%1 has too little memory for this model.").arg(d.name);
        }
    }
    if (m_device->count() == 1)
        notes << tr("No graphics card that whisper.cpp can use (Vulkan) was found; the processor computes.");
    else
        notes << tr("A graphics card is usually much faster; the speed test shows how much on this computer.");
    m_deviceNote->setText(notes.join(QLatin1Char(' ')));
    int idx = m_device->findData(chosen);
    if (idx < 0 || !(m_device->model()->flags(m_device->model()->index(idx, 0)) & Qt::ItemIsEnabled))
        idx = 0;
    m_device->setCurrentIndex(idx);
}

void SpeechOptions::updateLanguageNote()
{
    const QString mainLang = m_main->currentData().toString();
    const QString fb = m_fallback->currentData().toString();
    const speech::CatalogEntry *e = speech::catalogEntry(m_model->currentData().toString());
    const bool translates = !e || e->translates;
    QString t;
    if (fb.isEmpty() || fb == mainLang)
        t = tr("Everything is written in %1.").arg(speech::languageName(mainLang));
    else if (speech::canTranslate(mainLang, fb, translates))
        t = tr("What you say in %1 is written in English (translated). Words of %1 inside English sentences usually come "
               "out in English too.")
                .arg(speech::languageName(fb));
    else if (!translates)
        t = tr("This model cannot translate: what you say in %1 stays in %1.").arg(speech::languageName(fb));
    else
        t = tr("whisper translates only into English: with %1 as the main language, what you say in %2 stays in %2.")
                .arg(speech::languageName(mainLang), speech::languageName(fb));
    m_langNote->setText(t);
    m_keepOriginal->setEnabled(speech::canTranslate(mainLang, fb, translates));
}

void SpeechOptions::setBusy(bool busy)
{
    m_progressRow->setVisible(busy);
    m_progress->setRange(0, 0);
    m_download->setEnabled(!busy);
    m_local->setEnabled(!busy);
    m_remove->setEnabled(!busy && !m_tool->installedModelFile().isEmpty());
    if (busy)
        m_message->setText(m_tool->modelMessage());
}

void SpeechOptions::apply()
{
    m_tool->setSetting(QStringLiteral("source"), m_other->isChecked() ? QStringLiteral("other") : QStringLiteral("official"));
    m_tool->setSetting(QStringLiteral("otherAddress"), m_otherAddress->text().trimmed());
    m_tool->setSetting(QStringLiteral("gpu"), m_device->currentData().toInt());
    m_tool->setSetting(QStringLiteral("threads"), m_threads->value());
    m_tool->setSetting(QStringLiteral("careful"), m_accuracy->currentIndex() == 1);
    m_tool->setSetting(QStringLiteral("main"), m_main->currentData().toString());
    m_tool->setSetting(QStringLiteral("fallback"), m_fallback->currentData().toString());
    m_tool->setSetting(QStringLiteral("keepOriginal"), m_keepOriginal->isChecked());
    m_tool->setSetting(QStringLiteral("hint"), m_hint->toPlainText().trimmed());
    m_tool->setSetting(QStringLiteral("audioInput"), m_input->currentData().toByteArray());
    m_tool->setSetting(QStringLiteral("sensitivity"), m_sensitivity->currentIndex());
    m_tool->setSetting(QStringLiteral("pauseMs"), qRound(m_pause->value() * 1000));
    m_tool->setSetting(QStringLiteral("paragraphMs"), qRound(m_paragraph->value() * 1000));
    const QString folder = m_folder->text().trimmed();
    m_tool->setSetting(QStringLiteral("folder"), folder == speech::defaultFolder() ? QString() : folder);
    m_tool->setSetting(QStringLiteral("silenceGuard"), m_guard->isChecked());
    m_tool->setSetting(QStringLiteral("useContext"), m_context->isChecked());
    if (m_tool->panel())
        m_tool->panel()->refresh();
}

} // namespace

OptionsSection *SpeechTool::createOptions(QWidget *parent)
{
    return m_host ? new SpeechOptions(this, parent) : nullptr;
}
