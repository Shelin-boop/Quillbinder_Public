#pragma once
// Writing tool "Speech Transcription" (add-on, 0.61; tool interface 1.1).
//
// A button of the main toolbar starts and stops a recording; the arrow beside it chooses the
// microphone. What is said becomes text in a new transcription file (a folder outside every
// project, File > Options > Speech), listed in the inspector's tab "Transcriptions", where it can
// be edited and copied. Speech recognition runs on this computer only (whisper.cpp): the model
// is downloaded once, when the writer chooses it, from whisper.cpp's model files on Hugging Face
// (or another address, or a file the writer downloaded), checked (checksum, the model's
// description in the file, a test transcription) and only then put in place of the old one.
#include "SpeechApi.h"
#include "SpeechLogic.h"
#include "tools/ToolApi.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

class QComboBox;
class QFileSystemWatcher;
class QLabel;
class QListWidget;
class QMenu;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QProgressBar;
class QTimer;
class QToolButton;
class SpeechTool;

// The inspector tab: the transcriptions, newest first, and the text of the one chosen.
class TranscriptPanel : public QWidget
{
    Q_OBJECT
public:
    TranscriptPanel(SpeechTool *tool, QWidget *parent);
    ~TranscriptPanel() override;

public slots:
    // also used by the tests (QMetaObject::invokeMethod)
    void refresh();
    bool selectTranscript(const QString &title);
    QStringList titles() const;
    QString currentFile() const { return m_file; }
    QString text() const;
    void setText(const QString &text);   // as if typed
    void save();
    bool rename(const QString &title);
    bool remove(bool ask = true);
    void copyAll();
    QString status() const;

    // from the tool while it records
    void recordingStarted(const QString &file);
    // 0.64: a recording's text is being written / is done (file: the .txt it becomes)
    void transcriptionStarted(const QString &file);
    void transcriptionFinished(const QString &file);
    int pendingCount() const;    // recordings without text in the list
    void appendPiece(const QString &added);
    void setLevel(float level);
    void setState(const QString &state);
    void setRecording(bool on);

private:
    void load(const QString &file);
    void updateButtons();

    SpeechTool *m_tool;
    QString m_folder;
    QString m_file;
    QString m_recordingFile;
    bool m_loading = false;
    int m_pending = 0;          // recordings without text (0.64)
    QListWidget *m_list;
    QPlainTextEdit *m_edit;
    QToolButton *m_record, *m_rename, *m_delete, *m_copy, *m_folderButton, *m_transcribeAll;
    QLabel *m_status;
    QProgressBar *m_level;
    QTimer *m_saveTimer;
    QFileSystemWatcher *m_watch;
};

class SpeechTool : public QObject, public qb::tools::ToolInterface, public qb::tools::ToolExtension
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_TOOL_IID)
    Q_INTERFACES(qb::tools::ToolInterface qb::tools::ToolExtension)
public:
    static constexpr const char *kId = "speech-transcription";
    QString id() const override { return QString::fromLatin1(kId); }
    QString name() const override { return tr("Speech Transcription"); }
    QString version() const override { return QStringLiteral("1.1"); } // 1.1 (0.64): Record only
    QList<qb::tools::Command> commands() const override;
    void setHost(qb::tools::ToolHost *host) override;
    void run(const QString &key) override;
    QString optionsPage() const override;
    qb::tools::OptionsSection *createOptions(QWidget *parent) override;
    void shutdown() override;
    int openWindows() const override { return 0; }
    // 1.1
    QList<qb::tools::InspectorTab> inspectorTabs() const override;
    QWidget *createInspectorTab(const QString &key, QWidget *parent) override;
    QList<qb::tools::ToolbarButton> toolbarButtons() const override;
    QMenu *commandMenu(const QString &commandKey) override;
    QString optionsTitle() const override { return tr("Speech"); }
    QString optionsIcon() const override { return QStringLiteral("audio-input-microphone"); }

    // ---- for the panel and the options
    qb::tools::ToolHost *host() const { return m_host; }
    QVariant setting(const QString &key, const QVariant &def = QVariant()) const;
    void setSetting(const QString &key, const QVariant &value);
    QString folder() const;                 // the transcriptions
    QString modelsDir() const;              // ~/.local/share/<App>/tools/speech-transcription/models
    QString installedModelFile() const;     // empty: none
    QString installedModelId() const;       // catalog id, or "file"
    QString speechDir() const;              // the add-on's speech/ folder (engine, libraries, sample)
    QString samplePath() const;             // the test recording (samples/jfk.wav)
    speech::EngineSettings engineSettings() const;
    // The engine, loaded on first use; null with *error when it cannot be loaded.
    speech::EngineInterface *engine(QString *error = nullptr);
    TranscriptPanel *panel() const { return m_panel; }

public slots:
    // also used by the tests
    void toggleRecording();
    bool startRecording(const QString &wavFile = QString());   // a WAV file instead of the microphone (tests)
    void stopRecording();
    // 0.64: Record only (File > Options > Speech; the standard on Android): the microphone goes into
    // a WAV file, and its text is written later with Transcribe (one recording or all of them)
    bool recordOnly() const;
    bool transcribeRecording(const QString &wavFile);
    int transcribeAll();
    bool isTranscribingRecording() const;
    QStringList queuedRecordings() const { return m_queue; }
    bool isBusy() const;
    bool isRecording() const;
    QString lastError() const { return m_lastError; }
    QString recordingFile() const { return m_recFile; }
    // models
    void downloadModel(const QString &id);                      // from the source chosen in the options
    void useLocalModel(const QString &file);                     // a file the writer downloaded
    void cancelDownload();
    bool removeModel();
    bool modelJobRunning() const { return m_job != Job::None; }
    QString modelMessage() const { return m_modelMessage; }
    // A model whose checksum is not the known one (another address, a file of the writer's): the
    // answer to the question asked before it is read (tests: QB_SPEECH_ACCEPT_CHANGED=1 answers yes).
    void setAcceptChanged(bool yes) { m_acceptChanged = yes; }

signals:
    void modelProgress(qint64 done, qint64 total);
    void modelJobFinished(bool ok, const QString &message);
    void recordingChanged(bool on);

private:
    enum class Job { None, Download, Check };
    void fillMenu();
    void onText(const QString &text, bool newParagraph);
    void onState(const QString &state);
    void checkAndInstall(const QString &file, const QString &expectedId, bool fromDownload);
    // after the sums: the description and the test transcription; known = the catalog entry whose
    // sums the file has (nullptr: none)
    void checkModelFile(const QString &file, const speech::CatalogEntry *known, const QString &expectedId);
    void finishJob(bool ok, const QString &message);
    void updateCommand();
    bool startRecordOnly(const QString &wavFile);
    void finishRecordingText();

    qb::tools::ToolHost *m_host = nullptr;
    QPointer<TranscriptPanel> m_panel;
    QMenu *m_menu = nullptr;
    speech::EngineInterface *m_engine = nullptr;
    QString m_engineError;
    QString m_recFile;
    QString m_tail;            // the end of the file being written (to join the next piece)
    QString m_transcribingWav; // the recording whose text is being written (0.64)
    bool m_transcribeStopped = false;
    QStringList m_queue;       // recordings waiting for Transcribe
    QString m_lastError;
    // models
    Job m_job = Job::None;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QString m_partFile;
    QString m_jobId;
    QString m_modelMessage;
    bool m_acceptChanged = false;
    bool m_cancelled = false;
};
