#include "ScratchPad.h"
#include "ScreenGrab.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextImageFormat>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

using namespace qb::tools;

static const char *kTool = "scratch-pad";

// ------------------------------------------------------------------ notes on disk

namespace scratch {

QString defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/')
        + QCoreApplication::applicationName() + QStringLiteral(" Scratch Pad");
}

QString folder(ToolHost *host)
{
    QString f = host->setting(QString::fromLatin1(kTool), QStringLiteral("folder")).toString();
    if (f.isEmpty())
        f = defaultFolder();
    QDir().mkpath(f);
    return f;
}

QString imagesDir(const QString &folder)
{
    return folder + QStringLiteral("/.images");
}

QList<Note> notesIn(const QString &folder)
{
    QList<Note> out;
    const QFileInfoList files = QDir(folder).entryInfoList({QStringLiteral("*.txt"), QStringLiteral("*.html")}, QDir::Files);
    for (const QFileInfo &fi : files) {
        Note n;
        n.file = fi.absoluteFilePath();
        n.title = fi.completeBaseName();
        n.rich = fi.suffix().toLower() == QLatin1String("html");
        n.modified = fi.lastModified();
        out << n;
    }
    std::sort(out.begin(), out.end(), [](const Note &a, const Note &b) {
        if (a.modified != b.modified)
            return a.modified > b.modified;
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return out;
}

QString safeFileName(const QString &title)
{
    QString s = title.trimmed();
    s.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]")), QStringLiteral("-"));
    if (s.startsWith(QLatin1Char('.')))
        s.prepend(QLatin1Char('_'));
    if (s.isEmpty())
        s = QObject::tr("Untitled Note");
    return s.left(120);
}

} // namespace scratch

// ------------------------------------------------------------------ editor

NoteEditor::NoteEditor(QWidget *parent) : QTextEdit(parent) {}

bool NoteEditor::canInsertFromMimeData(const QMimeData *source) const
{
    if (acceptRichText() && source->hasImage())
        return true;
    return QTextEdit::canInsertFromMimeData(source);
}

void NoteEditor::insertFromMimeData(const QMimeData *source)
{
    if (acceptRichText() && source->hasImage() && storeImage) {
        const QImage img = qvariant_cast<QImage>(source->imageData());
        const QString name = storeImage(img);
        if (!name.isEmpty()) {
            document()->addResource(QTextDocument::ImageResource, QUrl(name), img);
            QTextImageFormat f;
            f.setName(name);
            if (img.width() > 400) {
                f.setWidth(400);
                f.setHeight(img.height() * 400.0 / img.width());
            }
            textCursor().insertImage(f);
            return;
        }
    }
    if (!acceptRichText()) {
        insertPlainText(source->text());
        return;
    }
    QTextEdit::insertFromMimeData(source);
}

void NoteEditor::keyPressEvent(QKeyEvent *e)
{
    if (acceptRichText() && (e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & (Qt::AltModifier | Qt::ShiftModifier))) {
        QTextCharFormat f;
        const QTextCharFormat cur = textCursor().charFormat();
        if (e->key() == Qt::Key_B) {
            f.setFontWeight(cur.fontWeight() > QFont::Normal ? QFont::Normal : QFont::Bold);
            mergeCurrentCharFormat(f);
            return;
        }
        if (e->key() == Qt::Key_I) {
            f.setFontItalic(!cur.fontItalic());
            mergeCurrentCharFormat(f);
            return;
        }
        if (e->key() == Qt::Key_U) {
            f.setFontUnderline(!cur.fontUnderline());
            mergeCurrentCharFormat(f);
            return;
        }
    }
    QTextEdit::keyPressEvent(e);
}

// ------------------------------------------------------------------ options section

namespace {
class ScratchOptions : public OptionsSection
{
public:
    ScratchOptions(ToolHost *host, QWidget *parent) : m_host(host)
    {
        m_w = new QWidget(parent);
        auto *f = new QFormLayout(m_w);
        f->setContentsMargins(0, 0, 0, 0);
        auto *row = new QHBoxLayout;
        m_folder = new QLineEdit(scratch::folder(host), m_w);
        m_folder->setObjectName(QStringLiteral("scratchFolder"));
        auto *browse = new QPushButton(QObject::tr("Choose…"), m_w);
        row->addWidget(m_folder, 1);
        row->addWidget(browse);
        f->addRow(QObject::tr("Notes are kept in:"), row);
        QObject::connect(browse, &QPushButton::clicked, m_w, [this] {
            const QString d = QFileDialog::getExistingDirectory(m_w, QObject::tr("Folder for Scratch Pad Notes"), m_folder->text());
            if (!d.isEmpty())
                m_folder->setText(d);
        });
        m_format = new QComboBox(m_w);
        m_format->setObjectName(QStringLiteral("scratchFormat"));
        m_format->addItem(QObject::tr("Formatted text (bold, italics, pictures)"), true);
        m_format->addItem(QObject::tr("Plain text"), false);
        m_format->setCurrentIndex(host->setting(QString::fromLatin1(kTool), QStringLiteral("rich"), true).toBool() ? 0 : 1);
        f->addRow(QObject::tr("New notes:"), m_format);
        m_top = new QCheckBox(QObject::tr("Keep the scratch pad above other windows"), m_w);
        m_top->setObjectName(QStringLiteral("scratchOnTop"));
        m_top->setChecked(host->setting(QString::fromLatin1(kTool), QStringLiteral("onTop"), false).toBool());
        f->addRow(QString(), m_top);
        auto *hint = new QLabel(QObject::tr("Shortcut in the program: Window > Scratch Pad (Options > Keyboard). "
                                            "In every program: let the desktop run <code>%1</code> "
                                            "(see Options > Keyboard).")
                                    .arg(host->commandLine(QStringLiteral("scratch-pad.show")).toHtmlEscaped()),
                                m_w);
        hint->setWordWrap(true);
        hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
        f->addRow(hint);
    }
    QWidget *widget() override { return m_w; }
    void apply() override
    {
        const QString t = QString::fromLatin1(kTool);
        QString dir = m_folder->text().trimmed();
        if (dir == scratch::defaultFolder())
            dir.clear(); // follows the documents folder
        m_host->setSetting(t, QStringLiteral("folder"), dir);
        m_host->setSetting(t, QStringLiteral("rich"), m_format->currentData().toBool());
        m_host->setSetting(t, QStringLiteral("onTop"), m_top->isChecked());
    }

private:
    ToolHost *m_host;
    QWidget *m_w;
    QLineEdit *m_folder;
    QComboBox *m_format;
    QCheckBox *m_top;
};
} // namespace

// ------------------------------------------------------------------ plugin

QList<Command> ScratchPadTool::commands() const
{
    Command c;
    c.key = QStringLiteral("scratch-pad.show");
    c.text = tr("Scratch &Pad");
    c.place = QStringLiteral("window");
    c.shortcut = QStringLiteral("Alt+Shift+Return");
    c.icon = QStringLiteral("document-edit");
    c.toolTip = tr("Notes outside every project, to be sent to a project later");
    c.standalone = true;
    return {c};
}

void ScratchPadTool::setHost(ToolHost *host)
{
    m_host = host;
}

void ScratchPadTool::run(const QString &key)
{
    if (key != QLatin1String("scratch-pad.show") || !m_host)
        return;
    if (!m_window) {
        m_window = new ScratchPadWindow(m_host, m_host->window());
        QObject::connect(m_host->events(), SIGNAL(settingsChanged()), m_window, SLOT(refresh()));
    }
    m_window->applySettings();
    m_window->show();
    m_window->showNormal();
    m_window->raise();
    m_window->activateWindow();
}

OptionsSection *ScratchPadTool::createOptions(QWidget *parent)
{
    return m_host ? new ScratchOptions(m_host, parent) : nullptr;
}

void ScratchPadTool::shutdown()
{
    if (m_window)
        m_window->save();
}

// ------------------------------------------------------------------ window

static QToolButton *footButton(QWidget *parent, const QString &icon, const QString &tip, const QString &name)
{
    auto *b = new QToolButton(parent);
    b->setIcon(QIcon::fromTheme(icon));
    b->setToolTip(tip);
    b->setObjectName(name);
    b->setAutoRaise(true);
    return b;
}

ScratchPadWindow::ScratchPadWindow(ToolHost *host, QWidget *parent) : QWidget(parent, Qt::Window), m_host(host)
{
    setObjectName(QStringLiteral("scratchPad"));
    setWindowTitle(tr("Scratch Pad"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setObjectName(QStringLiteral("scratchSplit"));
    m_list = new QListWidget(split);
    m_list->setObjectName(QStringLiteral("scratchNotes"));
    m_list->setMinimumWidth(140);
    auto *right = new QWidget(split);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    auto *top = new QHBoxLayout;
    m_title = new QLineEdit(right);
    m_title->setObjectName(QStringLiteral("scratchTitle"));
    m_title->setPlaceholderText(tr("Title"));
    top->addWidget(m_title, 1);
    m_bold = footButton(right, QStringLiteral("format-text-bold"), tr("Bold (Ctrl+B)"), QStringLiteral("scratchBold"));
    m_italic = footButton(right, QStringLiteral("format-text-italic"), tr("Italic (Ctrl+I)"), QStringLiteral("scratchItalic"));
    m_underline = footButton(right, QStringLiteral("format-text-underline"), tr("Underline (Ctrl+U)"), QStringLiteral("scratchUnderline"));
    m_bold->setText(QStringLiteral("B"));
    m_italic->setText(QStringLiteral("I"));
    m_underline->setText(QStringLiteral("U"));
    top->addWidget(m_bold);
    top->addWidget(m_italic);
    top->addWidget(m_underline);
    rv->addLayout(top);
    m_edit = new NoteEditor(right);
    m_edit->setObjectName(QStringLiteral("scratchEditor"));
    m_edit->storeImage = [this](const QImage &img) { return storeImage(img); };
    rv->addWidget(m_edit, 1);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({180, 420});
    v->addWidget(split, 1);

    auto *foot = new QHBoxLayout;
    m_add = footButton(this, QStringLiteral("list-add"), tr("New note"), QStringLiteral("scratchAdd"));
    m_add->setText(QStringLiteral("+"));
    m_add->setPopupMode(QToolButton::MenuButtonPopup);
    auto *addMenu = new QMenu(m_add);
    addMenu->addAction(tr("New Formatted Note"), this, [this] { newNote(true); });
    addMenu->addAction(tr("New Plain Text Note"), this, [this] { newNote(false); });
    m_add->setMenu(addMenu);
    m_remove = footButton(this, QStringLiteral("list-remove"), tr("Delete the note"), QStringLiteral("scratchRemove"));
    m_remove->setText(QStringLiteral("−"));
    m_camera = footButton(this, QStringLiteral("camera-photo"), tr("Take a picture of a part of the screen (formatted notes)"),
                          QStringLiteral("scratchCamera"));
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("scratchStatus"));
    m_send = new QToolButton(this);
    m_send->setObjectName(QStringLiteral("scratchSend"));
    m_send->setText(tr("Send to Project"));
    m_send->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_send->setPopupMode(QToolButton::InstantPopup);
    m_sendMenu = new QMenu(m_send);
    m_sendMenu->setObjectName(QStringLiteral("scratchSendMenu"));
    m_send->setMenu(m_sendMenu);
    foot->addWidget(m_add);
    foot->addWidget(m_remove);
    foot->addWidget(m_camera);
    foot->addWidget(m_status, 1);
    foot->addWidget(m_send);
    v->addLayout(foot);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(800);
    m_watch = new QFileSystemWatcher(this);

    connect(m_saveTimer, &QTimer::timeout, this, &ScratchPadWindow::save);
    connect(m_edit, &QTextEdit::textChanged, this, [this] {
        if (!m_loading && !m_file.isEmpty())
            m_saveTimer->start();
    });
    connect(m_add, &QToolButton::clicked, this, [this] {
        newNote(m_host->setting(QString::fromLatin1(kTool), QStringLiteral("rich"), true).toBool());
    });
    connect(m_remove, &QToolButton::clicked, this, [this] { deleteNote(true); });
    connect(m_camera, &QToolButton::clicked, this, &ScratchPadWindow::takeScreenshot);
    connect(m_bold, &QToolButton::clicked, this, [this] {
        QTextCharFormat f;
        f.setFontWeight(m_edit->textCursor().charFormat().fontWeight() > QFont::Normal ? QFont::Normal : QFont::Bold);
        m_edit->mergeCurrentCharFormat(f);
    });
    connect(m_italic, &QToolButton::clicked, this, [this] {
        QTextCharFormat f;
        f.setFontItalic(!m_edit->textCursor().charFormat().fontItalic());
        m_edit->mergeCurrentCharFormat(f);
    });
    connect(m_underline, &QToolButton::clicked, this, [this] {
        QTextCharFormat f;
        f.setFontUnderline(!m_edit->textCursor().charFormat().fontUnderline());
        m_edit->mergeCurrentCharFormat(f);
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur) {
        if (m_loading)
            return;
        save();
        load(cur ? cur->data(Qt::UserRole).toString() : QString());
    });
    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        if (!m_file.isEmpty() && m_title->text().trimmed() != QFileInfo(m_file).completeBaseName())
            renameNote(m_title->text());
    });
    connect(m_sendMenu, &QMenu::aboutToShow, this, &ScratchPadWindow::fillSendMenu);
    connect(m_watch, &QFileSystemWatcher::directoryChanged, this, [this] { QTimer::singleShot(200, this, &ScratchPadWindow::refresh); });
    connect(m_watch, &QFileSystemWatcher::fileChanged, this, [this](const QString &f) {
        // changed by another scratch pad window (another instance of the program)
        if (f == m_file && !m_edit->document()->isModified() && QFileInfo::exists(f))
            QTimer::singleShot(200, this, [this, f] {
                if (f == m_file && !m_edit->document()->isModified())
                    load(f);
            });
    });

    const QByteArray geo = m_host->setting(QString::fromLatin1(kTool), QStringLiteral("geometry")).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo))
        resize(640, 420);
    applySettings();
}

ScratchPadWindow::~ScratchPadWindow()
{
    save();
}

void ScratchPadWindow::applySettings()
{
    const QString f = scratch::folder(m_host);
    const bool onTop = m_host->setting(QString::fromLatin1(kTool), QStringLiteral("onTop"), false).toBool();
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) != onTop) {
        const bool vis = isVisible();
        setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
        if (vis)
            show();
    }
    if (f == m_folder)
        return;
    save();
    if (!m_folder.isEmpty())
        m_watch->removePath(m_folder);
    m_folder = f;
    m_watch->addPath(m_folder);
    m_file.clear();
    refresh();
    const QString last = m_host->setting(QString::fromLatin1(kTool), QStringLiteral("lastNote")).toString();
    if (!last.isEmpty())
        selectNote(last);
    if (m_file.isEmpty() && m_list->count() > 0)
        m_list->setCurrentRow(0);
    if (m_list->count() == 0)
        load(QString());
}

void ScratchPadWindow::refresh()
{
    if (m_folder != scratch::folder(m_host)) {
        applySettings();
        return;
    }
    const QString keep = m_file;
    m_loading = true;
    m_list->clear();
    QListWidgetItem *current = nullptr;
    for (const scratch::Note &n : scratch::notesIn(m_folder)) {
        auto *it = new QListWidgetItem(n.title, m_list);
        it->setData(Qt::UserRole, n.file);
        it->setToolTip(tr("%1 · changed %2").arg(n.rich ? tr("Formatted note") : tr("Plain text note"),
                                                  QLocale().toString(n.modified, QLocale::ShortFormat)));
        if (n.file == keep)
            current = it;
    }
    if (current)
        m_list->setCurrentItem(current);
    m_loading = false;
    if (!keep.isEmpty() && !current) // removed by someone else
        load(QString());
    updateUi();
}

void ScratchPadWindow::load(const QString &file)
{
    if (!m_file.isEmpty())
        m_watch->removePath(m_file);
    m_loading = true;
    m_file = file;
    m_rich = file.endsWith(QLatin1String(".html"), Qt::CaseInsensitive);
    m_edit->clear();
    m_edit->setAcceptRichText(m_rich);
    if (!file.isEmpty()) {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(f.readAll());
            if (m_rich) {
                m_edit->document()->setBaseUrl(QUrl::fromLocalFile(m_folder + QLatin1Char('/')));
                m_edit->setHtml(text);
            } else {
                m_edit->setPlainText(text);
            }
        }
        m_watch->addPath(file);
        m_host->setSetting(QString::fromLatin1(kTool), QStringLiteral("lastNote"), QFileInfo(file).completeBaseName());
    }
    m_title->setText(file.isEmpty() ? QString() : QFileInfo(file).completeBaseName());
    m_edit->document()->setModified(false);
    m_loading = false;
    updateUi();
}

void ScratchPadWindow::updateUi()
{
    const bool has = !m_file.isEmpty();
    m_edit->setEnabled(has);
    m_title->setEnabled(has);
    m_remove->setEnabled(has);
    m_send->setEnabled(has);
    for (QToolButton *b : {m_bold, m_italic, m_underline, m_camera})
        b->setVisible(has && m_rich); // the camera only for formatted notes
    if (!has)
        m_edit->setPlaceholderText(tr("Click + to start a note."));
    else
        m_edit->setPlaceholderText(QString());
}

void ScratchPadWindow::save()
{
    m_saveTimer->stop();
    if (m_file.isEmpty() || !m_edit->document()->isModified())
        return;
    m_watch->removePath(m_file); // our own change is no news
    QSaveFile f(m_file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write((m_rich ? m_edit->toHtml() : m_edit->toPlainText()).toUtf8());
        if (f.commit())
            m_edit->document()->setModified(false);
        else
            m_status->setText(tr("The note could not be saved: %1").arg(f.errorString()));
    }
    m_watch->addPath(m_file);
}

void ScratchPadWindow::newNote(bool rich)
{
    save();
    QString title = tr("Untitled Note");
    const QString ext = rich ? QStringLiteral(".html") : QStringLiteral(".txt");
    QString file = m_folder + QLatin1Char('/') + title + ext;
    for (int i = 2; QFileInfo::exists(m_folder + QLatin1Char('/') + title + QStringLiteral(".html"))
                    || QFileInfo::exists(m_folder + QLatin1Char('/') + title + QStringLiteral(".txt"));
         ++i) {
        title = tr("Untitled Note %1").arg(i);
        file = m_folder + QLatin1Char('/') + title + ext;
    }
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        m_status->setText(tr("The note could not be created: %1").arg(f.errorString()));
        return;
    }
    f.close();
    m_file.clear();
    refresh();
    selectNote(title);
    m_title->setFocus();
    m_title->selectAll();
}

bool ScratchPadWindow::selectNote(const QString &title)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->text() == title) {
            m_list->setCurrentRow(i);
            if (m_file != m_list->item(i)->data(Qt::UserRole).toString())
                load(m_list->item(i)->data(Qt::UserRole).toString());
            return true;
        }
    return false;
}

QStringList ScratchPadWindow::noteTitles() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
        out << m_list->item(i)->text();
    return out;
}

bool ScratchPadWindow::renameNote(const QString &newTitle)
{
    if (m_file.isEmpty())
        return false;
    const QString title = scratch::safeFileName(newTitle);
    const QFileInfo fi(m_file);
    const QString target = fi.absolutePath() + QLatin1Char('/') + title + QLatin1Char('.') + fi.suffix();
    if (target == m_file)
        return true;
    if (QFileInfo::exists(target)) {
        m_status->setText(tr("A note called “%1” exists already.").arg(title));
        m_title->setText(fi.completeBaseName());
        return false;
    }
    save();
    m_watch->removePath(m_file);
    if (!QFile::rename(m_file, target)) {
        m_status->setText(tr("The note could not be renamed."));
        return false;
    }
    m_file = target;
    m_watch->addPath(m_file);
    refresh();
    m_title->setText(title);
    m_host->setSetting(QString::fromLatin1(kTool), QStringLiteral("lastNote"), title);
    return true;
}

void ScratchPadWindow::deleteNote(bool ask)
{
    if (m_file.isEmpty())
        return;
    const QString title = QFileInfo(m_file).completeBaseName();
    if (ask && QMessageBox::question(this, tr("Delete Note"), tr("Delete the note “%1”? This cannot be undone.").arg(title))
                   != QMessageBox::Yes)
        return;
    // pictures that only this note uses
    if (m_rich) {
        const QString html = m_edit->toHtml();
        const QRegularExpression re(QStringLiteral("\\.images/([0-9a-f\\-]+\\.png)"));
        for (auto it = re.globalMatch(html); it.hasNext();)
            QFile::remove(scratch::imagesDir(m_folder) + QLatin1Char('/') + it.next().captured(1));
    }
    m_watch->removePath(m_file);
    QFile::remove(m_file);
    m_edit->document()->setModified(false);
    m_file.clear();
    refresh();
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    else
        load(QString());
}

void ScratchPadWindow::setNoteText(const QString &text)
{
    if (m_rich)
        m_edit->setHtml(text);
    else
        m_edit->setPlainText(text);
    m_edit->document()->setModified(true);
    save();
}

QString ScratchPadWindow::noteText() const
{
    return m_edit->toPlainText();
}

QString ScratchPadWindow::status() const
{
    return m_status->text();
}

QString ScratchPadWindow::storeImage(const QImage &img)
{
    if (img.isNull())
        return QString();
    const QString dir = scratch::imagesDir(m_folder);
    QDir().mkpath(dir);
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
    if (!img.save(dir + QLatin1Char('/') + name, "PNG"))
        return QString();
    return QStringLiteral(".images/") + name;
}

bool ScratchPadWindow::insertImage(const QImage &img)
{
    if (m_file.isEmpty() || !m_rich || img.isNull())
        return false;
    const QString name = storeImage(img);
    if (name.isEmpty())
        return false;
    m_edit->document()->addResource(QTextDocument::ImageResource, QUrl(name), img);
    QTextImageFormat f;
    f.setName(name);
    if (img.width() > 400) {
        f.setWidth(400);
        f.setHeight(img.height() * 400.0 / img.width());
    }
    m_edit->textCursor().insertImage(f);
    save();
    return true;
}

void ScratchPadWindow::takeScreenshot()
{
    if (m_file.isEmpty() || !m_rich)
        return;
    const bool wasVisible = isVisible();
    hide(); // out of the picture
    QTimer::singleShot(350, this, [this, wasVisible] {
        auto *grab = new ScreenGrab(this);
        connect(grab, &ScreenGrab::finished, this, [this, grab, wasVisible](const QImage &img, const QString &error) {
            if (wasVisible) {
                show();
                raise();
                activateWindow();
            }
            if (!error.isEmpty())
                m_status->setText(error);
            else if (!img.isNull())
                insertImage(img);
            grab->deleteLater();
        });
        grab->start();
    });
}

// ------------------------------------------------------------------ sending

void ScratchPadWindow::fillSendMenu()
{
    m_sendMenu->clear();
    const QList<ProjectInfo> projects = m_host->openProjects();
    if (projects.isEmpty()) {
        m_sendMenu->addAction(tr("No project is open"))->setEnabled(false);
        return;
    }
    for (const ProjectInfo &p : projects) {
        QString title = p.title;
        title.replace(QLatin1Char('&'), QStringLiteral("&&"));
        if (p.thisWindow && projects.size() > 1)
            title += tr(" (this window)");
        QMenu *pm = m_sendMenu->addMenu(title);
        pm->setToolTip(p.file);
        const QString key = p.key;
        pm->addAction(tr("New Document in the Research Folder"), this, [this, key] { sendTo(key, QStringLiteral("import"), QString()); });
        QMenu *into = pm->addMenu(tr("New Document in"));
        QMenu *append = pm->addMenu(tr("Add to the End of"));
        std::function<void(QMenu *, QMenu *, const QJsonArray &)> fill = [&](QMenu *folders, QMenu *texts, const QJsonArray &nodes) {
            for (const QJsonValue &v : nodes) {
                const QJsonObject n = v.toObject();
                const QString id = n.value(QStringLiteral("id")).toString();
                QString t = n.value(QStringLiteral("title")).toString();
                t.replace(QLatin1Char('&'), QStringLiteral("&&"));
                const QJsonArray kids = n.value(QStringLiteral("children")).toArray();
                const bool folder = n.value(QStringLiteral("folder")).toBool();
                const bool text = n.value(QStringLiteral("text")).toBool();
                if (folder || !kids.isEmpty()) {
                    QMenu *sf = folders->addMenu(t);
                    sf->addAction(tr("Into “%1”").arg(t), this, [this, key, id] { sendTo(key, QStringLiteral("import"), id); });
                    QMenu *st = nullptr;
                    if (text || !kids.isEmpty()) {
                        st = texts->addMenu(t);
                        if (text) {
                            st->addAction(tr("“%1” itself").arg(t), this, [this, key, id] { sendTo(key, QStringLiteral("append"), id); });
                            st->addSeparator();
                        }
                    }
                    if (!kids.isEmpty()) {
                        sf->addSeparator();
                        fill(sf, st ? st : texts, kids);
                    }
                } else if (text) {
                    texts->addAction(t, this, [this, key, id] { sendTo(key, QStringLiteral("append"), id); });
                }
            }
        };
        fill(into, append, p.binder);
    }
}

bool ScratchPadWindow::sendTo(const QString &projectKey, const QString &mode, const QString &targetId)
{
    if (m_file.isEmpty())
        return false;
    save();
    SendRequest r;
    r.file = m_file; // the note itself: the receiving window reads it (pictures relative to the notes folder)
    r.title = QFileInfo(m_file).completeBaseName();
    r.mode = mode;
    r.targetId = targetId;
    QString error;
    QString projectTitle;
    for (const ProjectInfo &p : m_host->openProjects())
        if (p.key == projectKey)
            projectTitle = p.title;
    if (!m_host->sendToProject(projectKey, r, &error)) {
        m_status->setText(error);
        QApplication::beep();
        return false;
    }
    m_status->setText(mode == QLatin1String("append") ? tr("Added to a document of “%1”.").arg(projectTitle)
                                                      : tr("Sent to “%1”.").arg(projectTitle));
    return true;
}

void ScratchPadWindow::closeEvent(QCloseEvent *e)
{
    save();
    m_host->setSetting(QString::fromLatin1(kTool), QStringLiteral("geometry"), saveGeometry());
    QWidget::closeEvent(e);
}
