#pragma once
// Writing tool "Scratch Pad" (Window > Scratch Pad, Alt+Shift+Return; also
// `<program> --scratch-pad` for a shortcut that works in every program).
//
// Notes are files in a folder of their own, outside every project
// (File > Options > General > Scratch Pad): <title>.txt for plain text,
// <title>.html for formatted text; pictures of formatted notes live in
// the hidden sub-folder .images/. Notes can be sent to any open project:
// as a new document or added to the end of a text.
#include "tools/ToolApi.h"

#include <QDateTime>
#include <functional>
#include <QObject>
#include <QPointer>
#include <QTextEdit>
#include <QWidget>

class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QToolButton;
class QTimer;

namespace scratch {

struct Note {
    QString file;
    QString title;
    bool rich = false;
    QDateTime modified;
};

QString defaultFolder();
QString folder(qb::tools::ToolHost *host);
QList<Note> notesIn(const QString &folder);   // newest first
QString imagesDir(const QString &folder);
QString safeFileName(const QString &title);

} // namespace scratch

class NoteEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit NoteEditor(QWidget *parent);
    std::function<QString(const QImage &)> storeImage;   // returns the name to refer to

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void keyPressEvent(QKeyEvent *e) override;
};

class ScratchPadWindow : public QWidget
{
    Q_OBJECT
public:
    ScratchPadWindow(qb::tools::ToolHost *host, QWidget *parent);
    ~ScratchPadWindow() override;
    void applySettings();

public slots:
    // also called by the tests (QMetaObject::invokeMethod)
    void newNote(bool rich);
    void deleteNote(bool ask = true);
    bool selectNote(const QString &title);
    bool renameNote(const QString &title);
    void setNoteText(const QString &text);
    QString noteText() const;
    QStringList noteTitles() const;
    QString currentFile() const { return m_file; }
    bool insertImage(const QImage &img);
    void takeScreenshot();
    bool sendTo(const QString &projectKey, const QString &mode, const QString &targetId);
    void save();
    void refresh();
    QString status() const;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void load(const QString &file);
    void fillSendMenu();
    void updateUi();
    QString storeImage(const QImage &img);

    qb::tools::ToolHost *m_host;
    QString m_folder;
    QString m_file;
    bool m_rich = false;
    bool m_loading = false;
    QListWidget *m_list;
    QLineEdit *m_title;
    NoteEditor *m_edit;
    QToolButton *m_add, *m_remove, *m_camera, *m_send, *m_bold, *m_italic, *m_underline;
    QMenu *m_sendMenu;
    QLabel *m_status;
    QTimer *m_saveTimer;
    QFileSystemWatcher *m_watch;
};

class ScratchPadTool : public QObject, public qb::tools::ToolInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_TOOL_IID)
    Q_INTERFACES(qb::tools::ToolInterface)
public:
    QString id() const override { return QStringLiteral("scratch-pad"); }
    QString name() const override { return tr("Scratch Pad"); }
    QString version() const override { return QStringLiteral("1.0"); }
    QList<qb::tools::Command> commands() const override;
    void setHost(qb::tools::ToolHost *host) override;
    void run(const QString &key) override;
    QString optionsPage() const override { return QStringLiteral("General"); }
    qb::tools::OptionsSection *createOptions(QWidget *parent) override;
    void shutdown() override;
    int openWindows() const override { return m_window && m_window->isVisible() ? 1 : 0; }

private:
    qb::tools::ToolHost *m_host = nullptr;
    QPointer<ScratchPadWindow> m_window;
};
