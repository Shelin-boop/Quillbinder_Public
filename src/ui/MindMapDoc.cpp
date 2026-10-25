#include "ui/MindMapDoc.h"
#include <QRegularExpression>
#include "ui/Theme.h"
#include "core/Project.h"
#include "ui/MindMapScene.h"

#include <QFile>
#include <QSettings>
#include <QUndoCommand>
#include <QUndoStack>

namespace qb {

// ------------------------------------------------------------------ options

MindMapPrefs &MindMapPrefs::instance()
{
    static MindMapPrefs p = [] {
        MindMapPrefs x;
        x.load();
        return x;
    }();
    return p;
}

void MindMapPrefs::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("mindmap"));
    enterEndsEditing = s.value(QStringLiteral("enterEnds"), enterEndsEditing).toBool();
    enterCreatesNotes = s.value(QStringLiteral("enterCreates"), enterCreatesNotes).toBool();
    useSelectedStyle = s.value(QStringLiteral("useSelectedStyle"), useSelectedStyle).toBool();
    dashedLines = s.value(QStringLiteral("dashedLines"), dashedLines).toBool();
    autoSize = s.value(QStringLiteral("autoSize"), autoSize).toBool();
    fadedPercent = qBound(10, s.value(QStringLiteral("faded"), fadedPercent).toInt(), 95);
    newNoteText = s.value(QStringLiteral("newNoteText"), newNoteText).toString();
    newNoteSize = s.value(QStringLiteral("newNoteSize"), newNoteSize).toDouble();
    newNoteWidth = s.value(QStringLiteral("newNoteWidth"), newNoteWidth).toDouble();
    newNoteFill = QColor(s.value(QStringLiteral("newNoteFill")).toString());
    newNoteBorder = s.value(QStringLiteral("newNoteBorder"), newNoteBorder).toInt();
    newNoteShape = qBound(0, s.value(QStringLiteral("newNoteShape"), newNoteShape).toInt(), 3);
    iconSize = qBound(10, s.value(QStringLiteral("iconSize"), iconSize).toInt(), 48);
    if (const QColor c(s.value(QStringLiteral("newShapeFill")).toString()); c.isValid())
        newShapeFill = c;
    newShapeBorder = s.value(QStringLiteral("newShapeBorder"), newShapeBorder).toInt();
    newShapeMagnetic = s.value(QStringLiteral("newShapeMagnetic"), newShapeMagnetic).toBool();
    if (const QColor c(s.value(QStringLiteral("boardBackground")).toString()); c.isValid())
        boardBackground = c;
    if (const QColor c(s.value(QStringLiteral("boardBackgroundDark")).toString()); c.isValid())
        boardBackgroundDark = c;
    boardText = QColor(s.value(QStringLiteral("boardText")).toString());
    boardFont = s.value(QStringLiteral("boardFont")).toString();
    boardRightToLeft = s.value(QStringLiteral("boardRtl"), boardRightToLeft).toBool();
    defaultStylesXml = s.value(QStringLiteral("defaultStyles")).toByteArray();
}

void MindMapPrefs::save() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("mindmap"));
    s.setValue(QStringLiteral("enterEnds"), enterEndsEditing);
    s.setValue(QStringLiteral("enterCreates"), enterCreatesNotes);
    s.setValue(QStringLiteral("useSelectedStyle"), useSelectedStyle);
    s.setValue(QStringLiteral("dashedLines"), dashedLines);
    s.setValue(QStringLiteral("autoSize"), autoSize);
    s.setValue(QStringLiteral("faded"), fadedPercent);
    s.setValue(QStringLiteral("newNoteText"), newNoteText);
    s.setValue(QStringLiteral("newNoteSize"), newNoteSize);
    s.setValue(QStringLiteral("newNoteWidth"), newNoteWidth);
    s.setValue(QStringLiteral("newNoteFill"), newNoteFill.isValid() ? newNoteFill.name() : QString());
    s.setValue(QStringLiteral("newNoteBorder"), newNoteBorder);
    s.setValue(QStringLiteral("newNoteShape"), newNoteShape);
    s.setValue(QStringLiteral("iconSize"), iconSize);
    s.setValue(QStringLiteral("newShapeFill"), newShapeFill.isValid() ? newShapeFill.name() : QString());
    s.setValue(QStringLiteral("newShapeBorder"), newShapeBorder);
    s.setValue(QStringLiteral("newShapeMagnetic"), newShapeMagnetic);
    s.setValue(QStringLiteral("boardBackground"), boardBackground.name());
    s.setValue(QStringLiteral("boardBackgroundDark"), boardBackgroundDark.name());
    s.setValue(QStringLiteral("boardText"), boardText.isValid() ? boardText.name() : QString());
    s.setValue(QStringLiteral("boardFont"), boardFont);
    s.setValue(QStringLiteral("boardRtl"), boardRightToLeft);
    s.setValue(QStringLiteral("defaultStyles"), defaultStylesXml);
}

QString MindMapPrefs::placeholder() const
{
    return newNoteText.isEmpty() ? QObject::tr("New Note") : newNoteText;
}

// ------------------------------------------------------------------ undo

namespace {
class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(MindMapDoc *doc, const QString &text, QByteArray before, QByteArray after)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void undo() override { m_doc->restore(m_before); }
    void redo() override
    {
        if (m_first) {
            m_first = false; // the change is already made
            return;
        }
        m_doc->restore(m_after);
    }

private:
    MindMapDoc *m_doc;
    QByteArray m_before, m_after;
    bool m_first = true;
};
} // namespace

// ------------------------------------------------------------------ doc

MindMapDoc::MindMapDoc(Project *project, const QString &itemId, QObject *parent)
    : QObject(parent), m_project(project), m_itemId(itemId)
{
    const MindMapPrefs &prefs = MindMapPrefs::instance();
    board.settings.background = prefs.boardBackground;
    board.settings.textColor = prefs.boardText;
    board.settings.font = prefs.boardFont;
    board.settings.rightToLeft = prefs.boardRightToLeft;
    if (!prefs.defaultStylesXml.isEmpty()) {
        mindmap::Board tmp;
        if (tmp.fromXml(prefs.defaultStylesXml))
            board.styles = tmp.styles;
    }
    if (!path().isEmpty() && QFile::exists(path())) {
        QString err;
        board.load(path(), &err);
    }
    // a file of a version before 0.21.1 has one background color: the board's own
    // when it differs from the default of light themes, else the default
    if (board.settings.legacyBackground.isValid()) {
        if (board.settings.legacyBackground.rgb() != prefs.boardBackground.rgb())
            board.settings.ownLight = board.settings.legacyBackground;
        board.settings.legacyBackground = QColor();
    }
    m_undo = new QUndoStack(this);
    m_undo->setUndoLimit(200);
    applyBackground();
    m_scene = new MindMapScene(this);
    m_scene->rebuild();
    // a light or a dark theme: the background of that kind (0.21.1)
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, [this] { applyBackground(); });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1200);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
}

MindMapDoc::~MindMapDoc()
{
    // (a project that is being destroyed is no longer reachable here: nothing is written then)
    if (m_dirty && m_project)
        save();
}

QString MindMapDoc::path() const { return m_project ? mindmap::pathFor(m_project->contentDir(m_itemId)) : QString(); }

void MindMapDoc::begin(const QString &text)
{
    if (m_depth++ == 0) {
        m_before = board.toXml();
        m_text = text;
    }
}

void MindMapDoc::commit()
{
    if (m_depth == 0)
        return;
    if (--m_depth > 0)
        return;
    QByteArray after = board.toXml();
    if (after == m_before) {
        m_scene->syncAll();
        return;
    }
    m_undo->push(new SnapshotCommand(this, m_text, m_before, after));
    m_scene->syncAll();
    touched();
}

void MindMapDoc::cancel()
{
    if (m_depth == 0)
        return;
    m_depth = 0;
    board.fromXml(m_before);
    m_scene->rebuild();
    emit boardChanged();
}

void MindMapDoc::change(const QString &text, const std::function<void()> &fn)
{
    if (locked())
        return;
    begin(text);
    fn();
    commit();
}

void MindMapDoc::restore(const QByteArray &xml)
{
    board.fromXml(xml);
    applyBackground();
    m_scene->rebuild();
    touched();
}

bool MindMapDoc::darkMode() { return theme::isDark(); }

QColor MindMapDoc::ownBackground() const { return darkMode() ? board.settings.ownDark : board.settings.ownLight; }

void MindMapDoc::setOwnBackground(const QColor &c)
{
    if (locked())
        return;
    begin(tr("Background Color"));
    (darkMode() ? board.settings.ownDark : board.settings.ownLight) = c;
    board.settings.texture = QImage(); // a color takes the place of a texture
    applyBackground();
    commit();
}

void MindMapDoc::applyBackground()
{
    const MindMapPrefs &p = MindMapPrefs::instance();
    const QColor own = ownBackground();
    const QColor shown = own.isValid() ? own : p.defaultBackground(darkMode());
    board.settings.defaultLight = p.boardBackground;
    if (board.settings.background.rgb() == shown.rgb())
        return;
    board.settings.background = shown;
    if (m_scene) {
        m_scene->syncAll();
        m_scene->update();
        emit boardChanged();
    }
}

void MindMapDoc::touched()
{
    m_dirty = true;
    m_saveTimer.start();
    emit boardChanged();
}

bool MindMapDoc::save(QString *error)
{
    m_saveTimer.stop();
    if (!m_project || !m_project->item(m_itemId)) {
        m_dirty = false; // the document is gone (emptied trash)
        return true;
    }
    const bool hadFile = QFile::exists(path());
    if (!hadFile && !board.worthSaving()) { // (a board with a color of its own is kept, 0.21.1)
        m_dirty = false;
        return true;
    }
    if (!board.save(path(), error))
        return false;
    m_dirty = false;
    // a project in Scrivener format takes the file into the .scriv at its next save
    m_project->markIndexDirty();
    emit saved();
    return true;
}

// ------------------------------------------------------------------ registry

namespace mindmaps {

static QHash<Project *, QHash<QString, QPointer<MindMapDoc>>> &registry()
{
    static QHash<Project *, QHash<QString, QPointer<MindMapDoc>>> r;
    return r;
}

MindMapDoc *docFor(Project *project, const QString &itemId)
{
    if (!project || itemId.isEmpty())
        return nullptr;
    const bool first = !registry().contains(project);
    auto &docs = registry()[project];
    if (MindMapDoc *d = docs.value(itemId))
        return d;
    auto *d = new MindMapDoc(project, itemId, project);
    docs.insert(itemId, d);
    if (first)
        QObject::connect(project, &QObject::destroyed, [project] { registry().remove(project); });
    return d;
}

void refreshAll()
{
    for (const auto &docs : registry())
        for (const QPointer<MindMapDoc> &d : docs)
            if (d && d->scene()) {
                d->applyBackground(); // a new default background (0.21.1)
                d->scene()->syncAll();
                d->scene()->update();
            }
}

bool hasMap(Project *project, const QString &itemId)
{
    if (!project || itemId.isEmpty())
        return false;
    if (MindMapDoc *d = registry().value(project).value(itemId))
        return !d->board.isEmpty();
    // a file can hold only a background color (0.21.1): a map has notes or shapes
    QFile f(mindmap::pathFor(project->contentDir(itemId)));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    static const QRegularExpression item(QStringLiteral("<(Note|Shape)[\\s/>]"));
    return item.match(QString::fromUtf8(f.readAll())).hasMatch();
}

void saveAll(Project *project)
{
    for (const QPointer<MindMapDoc> &d : registry().value(project))
        if (d && d->isDirty())
            d->save();
}

void closeAll(Project *project)
{
    saveAll(project);
    for (const QPointer<MindMapDoc> &d : registry().value(project))
        if (d)
            delete d.data();
    registry().remove(project);
}

} // namespace mindmaps

} // namespace qb
