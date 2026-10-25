#include "ui/Layouts.h"
#include "AppInfo.h"
#include "core/Addons.h"
#include "core/Paths.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace qb {
namespace layouts {

namespace {

struct BuiltIn {
    const char *id;
    const char *name;
    const char *description;
    const char *json;
};

// The same format as layout files ("state" object).
const BuiltIn kBuiltIn[] = {
    {"default", QT_TRANSLATE_NOOP("qb::layouts", "Default"),
     QT_TRANSLATE_NOOP("qb::layouts", "Binder, one editor and the inspector."),
     R"({"binder":true,"inspector":true,"toolbar":true,"formatBar":true,"binderShare":0.2,"inspectorShare":0.22,
         "split":"none","panes":[{"view":"keep"}],"binderAffects":"current"})"},
    {"binder-editor", QT_TRANSLATE_NOOP("qb::layouts", "Binder and Editor"),
     QT_TRANSLATE_NOOP("qb::layouts", "Binder and one editor showing text; the inspector is closed."),
     R"({"binder":true,"inspector":false,"toolbar":true,"formatBar":true,"binderShare":0.2,
         "split":"none","panes":[{"view":"text"}],"binderAffects":"current"})"},
    {"editor-only", QT_TRANSLATE_NOOP("qb::layouts", "Editor Only"),
     QT_TRANSLATE_NOOP("qb::layouts", "Nothing but the text, the toolbar and the format bar."),
     R"({"binder":false,"inspector":false,"toolbar":true,"formatBar":true,
         "split":"none","panes":[{"view":"text"}],"binderAffects":"current"})"},
    {"outliner-editor", QT_TRANSLATE_NOOP("qb::layouts", "Outliner and Editor"),
     QT_TRANSLATE_NOOP("qb::layouts",
                       "Outliner on the left, text on the right. The binder loads groups into the outliner; a click "
                       "on a row of the outliner opens that document on the right."),
     R"({"binder":true,"inspector":false,"toolbar":true,"formatBar":true,"binderShare":0.17,
         "split":"vertical","splitShare":0.42,
         "panes":[{"view":"outliner","affectsOther":true},{"view":"text"}],"binderAffects":"first","active":1})"},
    {"corkboard-editor", QT_TRANSLATE_NOOP("qb::layouts", "Corkboard and Editor"),
     QT_TRANSLATE_NOOP("qb::layouts",
                       "Corkboard above, text below. The binder loads groups into the corkboard; a click on a card "
                       "opens that document below."),
     R"({"binder":true,"inspector":false,"toolbar":true,"formatBar":true,"binderShare":0.17,
         "split":"horizontal","splitShare":0.42,
         "panes":[{"view":"corkboard","cork":"grid","affectsOther":true},{"view":"text"}],"binderAffects":"first","active":1})"},
    {"two-editors", QT_TRANSLATE_NOOP("qb::layouts", "Two Editors"),
     QT_TRANSLATE_NOOP("qb::layouts", "Two texts side by side, for writing next to notes or research."),
     R"({"binder":true,"inspector":false,"toolbar":true,"formatBar":true,"binderShare":0.17,
         "split":"vertical","splitShare":0.5,"panes":[{"view":"text"},{"view":"text"}],"binderAffects":"current"})"},
    {"planning", QT_TRANSLATE_NOOP("qb::layouts", "Planning"),
     QT_TRANSLATE_NOOP("qb::layouts", "Binder, corkboard and the inspector with synopsis and notes."),
     R"({"binder":true,"inspector":true,"inspectorTab":0,"toolbar":true,"formatBar":false,"binderShare":0.18,
         "inspectorShare":0.22,"split":"none","panes":[{"view":"corkboard","cork":"grid"}],"binderAffects":"current"})"},
};

QString fileBase(const QString &name)
{
    QString b = name.toLower();
    b.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (b.startsWith(QLatin1Char('-')))
        b.remove(0, 1);
    while (b.endsWith(QLatin1Char('-')))
        b.chop(1);
    return b.isEmpty() ? QStringLiteral("layout") : b.left(60);
}

bool readFile(const QString &file, Layout *out)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (!o.value(QStringLiteral("state")).isObject())
        return false;
    out->name = o.value(QStringLiteral("name")).toString().trimmed();
    if (out->name.isEmpty())
        out->name = QFileInfo(file).completeBaseName();
    out->description = o.value(QStringLiteral("description")).toString();
    out->state = o.value(QStringLiteral("state")).toObject().toVariantMap();
    out->file = file;
    return true;
}

bool writeFile(const QString &file, const Layout &l, QString *error)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QJsonObject o;
    o.insert(QStringLiteral("format"), QStringLiteral("qblayout"));
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("name"), l.name);
    if (!l.description.isEmpty())
        o.insert(QStringLiteral("description"), l.description);
    o.insert(QStringLiteral("state"), QJsonObject::fromVariantMap(l.state));
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(o).toJson()) < 0) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

QString userFile(const QString &id)
{
    if (!id.startsWith(QLatin1String("user:")) || id.contains(QLatin1Char('/')))
        return QString();
    return userDir() + QLatin1Char('/') + id.mid(5) + QLatin1String(".json");
}

bool ownOnly(QString *error)
{
    if (error)
        *error = QObject::tr("Only your own layouts can be changed. Save the layout under a new name first.");
    return false;
}

} // namespace

QString userDir() { return dataDir() + QLatin1String("/layouts"); }

QList<Layout> available()
{
    QList<Layout> out;
    for (const BuiltIn &b : kBuiltIn) {
        Layout l;
        l.id = QLatin1String(b.id);
        l.name = QCoreApplication::translate("qb::layouts", b.name);
        l.description = QCoreApplication::translate("qb::layouts", b.description);
        l.kind = Kind::BuiltIn;
        l.state = QJsonDocument::fromJson(QByteArray(b.json)).object().toVariantMap();
        out << l;
    }
    for (const QString &dir : addons::contentDirs(QStringLiteral("layouts"))) {
        const QString addon = QFileInfo(QFileInfo(dir).absolutePath()).fileName();
        for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            Layout l;
            if (!readFile(fi.absoluteFilePath(), &l))
                continue;
            l.id = QStringLiteral("addon:") + addon + QLatin1Char('/') + fi.completeBaseName();
            l.kind = Kind::Addon;
            out << l;
        }
    }
    QList<Layout> own;
    for (const QFileInfo &fi : QDir(userDir()).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        Layout l;
        if (!readFile(fi.absoluteFilePath(), &l))
            continue;
        l.id = QStringLiteral("user:") + fi.completeBaseName();
        l.kind = Kind::User;
        own << l;
    }
    std::sort(own.begin(), own.end(), [](const Layout &a, const Layout &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return out + own;
}

bool load(const QString &id, Layout *out)
{
    for (const Layout &l : available())
        if (l.id == id) {
            *out = l;
            return true;
        }
    return false;
}

bool nameInUse(const QString &name, const QString &exceptId)
{
    for (const Layout &l : available())
        if (l.id != exceptId && l.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QString uniqueName(const QString &wanted)
{
    QString name = wanted.trimmed();
    for (int n = 2; nameInUse(name); ++n)
        name = wanted.trimmed() + QLatin1Char(' ') + QString::number(n);
    return name;
}

QString saveUser(const QString &name, const QVariantMap &state, QString *error)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        if (error)
            *error = QObject::tr("The layout needs a name.");
        return QString();
    }
    if (nameInUse(n)) {
        if (error)
            *error = QObject::tr("There is already a layout named “%1”.").arg(n);
        return QString();
    }
    QString base = fileBase(n);
    const QString stem = base;
    for (int i = 2; QFile::exists(userDir() + QLatin1Char('/') + base + QLatin1String(".json")); ++i)
        base = stem + QLatin1Char('-') + QString::number(i);
    Layout l;
    l.name = n;
    l.state = state;
    if (!writeFile(userDir() + QLatin1Char('/') + base + QLatin1String(".json"), l, error))
        return QString();
    emit Notifier::instance().changed();
    return QStringLiteral("user:") + base;
}

bool updateUser(const QString &id, const QVariantMap &state, QString *error)
{
    const QString file = userFile(id);
    Layout l;
    if (file.isEmpty() || !readFile(file, &l))
        return ownOnly(error);
    l.state = state;
    if (!writeFile(file, l, error))
        return false;
    emit Notifier::instance().changed();
    return true;
}

bool removeUser(const QString &id, QString *error)
{
    const QString file = userFile(id);
    if (file.isEmpty())
        return ownOnly(error);
    QFile f(file);
    if (!f.remove()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    emit Notifier::instance().changed();
    return true;
}

bool renameUser(const QString &id, const QString &newName, QString *error)
{
    const QString file = userFile(id);
    Layout l;
    if (file.isEmpty() || !readFile(file, &l))
        return ownOnly(error);
    const QString n = newName.trimmed();
    if (n.isEmpty()) {
        if (error)
            *error = QObject::tr("The layout needs a name.");
        return false;
    }
    if (nameInUse(n, id)) {
        if (error)
            *error = QObject::tr("There is already a layout named “%1”.").arg(n);
        return false;
    }
    // the file keeps its name, so the id (and a shortcut given to the layout) stays
    l.name = n;
    if (!writeFile(file, l, error))
        return false;
    emit Notifier::instance().changed();
    return true;
}

QString importLayout(const QString &file, QString *error)
{
    Layout l;
    if (!readFile(file, &l)) {
        if (error)
            *error = QObject::tr("“%1” is not a layout file.").arg(QFileInfo(file).fileName());
        return QString();
    }
    return saveUser(uniqueName(l.name), l.state, error);
}

bool exportLayout(const QString &id, const QString &file, QString *error)
{
    Layout l;
    if (!load(id, &l)) {
        if (error)
            *error = QObject::tr("The layout was not found.");
        return false;
    }
    return writeFile(file, l, error);
}

Notifier &Notifier::instance()
{
    static Notifier n;
    return n;
}

} // namespace layouts

// ------------------------------------------------------------------ preview

LayoutPreview::LayoutPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(240, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void LayoutPreview::setState(const QVariantMap &state)
{
    m_state = state;
    update();
}

void LayoutPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QPalette pal = palette();
    const QColor frame = pal.color(QPalette::Mid);
    const QColor chrome = pal.color(QPalette::Window);
    const QColor paper = pal.color(QPalette::Base);
    QColor ink = pal.color(QPalette::Text);
    ink.setAlpha(110);
    QColor faint = ink;
    faint.setAlpha(55);
    const QColor accent = pal.color(QPalette::Highlight);

    // keep the proportions of a window
    QRect r = rect().adjusted(6, 6, -6, -6);
    const double ratio = 1.6;
    if (r.width() > r.height() * ratio) {
        const int w = int(r.height() * ratio);
        r = QRect(r.center().x() - w / 2, r.top(), w, r.height());
    } else {
        const int h = int(r.width() / ratio);
        r = QRect(r.left(), r.center().y() - h / 2, r.width(), h);
    }
    p.fillRect(r, chrome);
    p.setPen(frame);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    if (m_state.isEmpty())
        return;

    auto lines = [&](const QRect &area, int indentEvery = 0) {
        p.setPen(Qt::NoPen);
        int i = 0;
        for (int y = area.top() + 4; y + 2 <= area.bottom() - 2; y += 6, ++i) {
            const int indent = indentEvery ? (i % indentEvery ? 8 : 0) : 0;
            const int w = area.width() - 8 - indent - ((i * 37) % 5) * area.width() / 16;
            p.fillRect(QRect(area.left() + 4 + indent, y, qMax(6, w), 2), i % 7 == 0 && indentEvery ? ink : faint);
        }
    };

    int top = r.top() + 1;
    if (m_state.value(QStringLiteral("toolbar"), true).toBool()) {
        p.fillRect(QRect(r.left() + 1, top, r.width() - 2, 10), chrome.darker(108));
        for (int i = 0; i < 6; ++i)
            p.fillRect(QRect(r.left() + 6 + i * 9, top + 3, 5, 4), ink);
        p.fillRect(QRect(r.center().x() - 22, top + 3, 44, 4), paper);
        top += 10;
    }
    if (m_state.value(QStringLiteral("formatBar"), true).toBool()) {
        p.fillRect(QRect(r.left() + 1, top, r.width() - 2, 7), chrome.darker(103));
        for (int i = 0; i < 9; ++i)
            p.fillRect(QRect(r.left() + 6 + i * 7, top + 2, 4, 3), faint);
        top += 7;
    }
    p.setPen(frame);
    p.drawLine(r.left(), top, r.right(), top);
    QRect body(r.left() + 1, top + 1, r.width() - 2, r.bottom() - top - 1);

    if (m_state.value(QStringLiteral("binder"), true).toBool()) {
        const int w = int(body.width() * qBound(0.08, m_state.value(QStringLiteral("binderShare"), 0.2).toDouble(), 0.5));
        const QRect b(body.left(), body.top(), w, body.height());
        p.fillRect(b, paper.darker(104));
        lines(b, 3);
        p.setPen(frame);
        p.drawLine(b.right(), b.top(), b.right(), b.bottom());
        body.setLeft(b.right() + 1);
    }
    if (m_state.value(QStringLiteral("inspector"), false).toBool()) {
        const int w = int(r.width() * qBound(0.08, m_state.value(QStringLiteral("inspectorShare"), 0.22).toDouble(), 0.5));
        const QRect b(body.right() - w + 1, body.top(), w, body.height());
        p.fillRect(b, chrome);
        const QRect card(b.left() + 4, b.top() + 5, b.width() - 8, qMin(26, b.height() / 3));
        p.fillRect(card, paper);
        p.setPen(frame);
        p.drawRect(card.adjusted(0, 0, -1, -1));
        p.drawLine(b.left(), b.top(), b.left(), b.bottom());
        lines(QRect(b.left(), card.bottom() + 2, b.width(), b.bottom() - card.bottom() - 2));
        body.setRight(b.left() - 1);
    }

    const QString split = m_state.value(QStringLiteral("split"), QStringLiteral("none")).toString();
    const QVariantList panes = m_state.value(QStringLiteral("panes")).toList();
    const double share = qBound(0.1, m_state.value(QStringLiteral("splitShare"), 0.5).toDouble(), 0.9);
    QList<QRect> rects;
    if (split == QLatin1String("vertical")) {
        const int w = int(body.width() * share);
        rects << QRect(body.left(), body.top(), w, body.height())
              << QRect(body.left() + w + 1, body.top(), body.width() - w - 1, body.height());
    } else if (split == QLatin1String("horizontal")) {
        const int h = int(body.height() * share);
        rects << QRect(body.left(), body.top(), body.width(), h)
              << QRect(body.left(), body.top() + h + 1, body.width(), body.height() - h - 1);
    } else {
        rects << body;
    }
    const int active = rects.size() > 1 ? m_state.value(QStringLiteral("active"), 0).toInt() : -1;
    for (int i = 0; i < rects.size(); ++i) {
        const QRect pr = rects[i];
        const QVariantMap pane = panes.value(i).toMap();
        const QString view = pane.value(QStringLiteral("view"), QStringLiteral("keep")).toString();
        p.fillRect(pr, paper);
        const QRect header(pr.left(), pr.top(), pr.width(), 6);
        p.fillRect(header, i == active ? accent : chrome.darker(106));
        const QRect content(pr.left(), header.bottom() + 1, pr.width(), pr.height() - 7);
        if (view == QLatin1String("corkboard")) {
            p.fillRect(content, QColor(196, 164, 120));
            const int cw = 26, ch = 17;
            for (int y = content.top() + 5; y + ch <= content.bottom() - 2; y += ch + 5)
                for (int x = content.left() + 5; x + cw <= content.right() - 2; x += cw + 5) {
                    p.fillRect(QRect(x, y, cw, ch), QColor(252, 250, 242));
                    p.fillRect(QRect(x, y + 4, cw, 1), QColor(200, 110, 110));
                }
        } else if (view == QLatin1String("outliner")) {
            p.setPen(Qt::NoPen);
            int row = 0;
            for (int y = content.top() + 3; y + 3 <= content.bottom() - 2; y += 7, ++row) {
                if (row % 2)
                    p.fillRect(QRect(content.left(), y - 2, content.width(), 7), paper.darker(104));
                const int indent = (row % 4 ? 8 : 0);
                p.fillRect(QRect(content.left() + 4 + indent, y, content.width() * 4 / 10 - indent, 2), ink);
                p.fillRect(QRect(content.left() + content.width() * 55 / 100, y, content.width() / 8, 2), faint);
                p.fillRect(QRect(content.left() + content.width() * 75 / 100, y, content.width() / 6, 2), faint);
            }
        } else {
            // text in a column
            const int margin = qMax(6, content.width() / 7);
            lines(content.adjusted(margin, 2, -margin, 0));
        }
        if (pane.value(QStringLiteral("affectsOther")).toBool()) {
            // small arrow: a click here opens the document in the other editor
            p.setPen(QPen(accent, 2));
            const QPoint c(pr.right() - 9, pr.bottom() - 8);
            p.drawLine(c.x() - 5, c.y(), c.x() + 4, c.y());
            p.drawLine(c.x() + 1, c.y() - 3, c.x() + 4, c.y());
            p.drawLine(c.x() + 1, c.y() + 3, c.x() + 4, c.y());
        }
        p.setPen(frame);
        if (i > 0) {
            if (split == QLatin1String("vertical"))
                p.drawLine(pr.left() - 1, pr.top(), pr.left() - 1, pr.bottom());
            else
                p.drawLine(pr.left(), pr.top() - 1, pr.right(), pr.top() - 1);
        }
    }
}

// ------------------------------------------------------------------ Manage Layouts

LayoutsDialog::LayoutsDialog(Capture capture, Apply apply, QWidget *parent)
    : QDialog(parent), m_capture(std::move(capture)), m_apply(std::move(apply))
{
    setWindowTitle(tr("Layouts"));
    resize(760, 430);
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("A layout remembers which parts of the window are shown, how the editor is split and which "
                                "view each editor uses. It works in every project."),
                             this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto *main = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("layoutList"));
    m_list->setMinimumWidth(230);
    left->addWidget(m_list, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(tr("Save Current…"), this);
    add->setObjectName(QStringLiteral("layoutSave"));
    add->setToolTip(tr("Save the window as it is now as a new layout"));
    m_delete = new QPushButton(tr("Delete"), this);
    m_delete->setObjectName(QStringLiteral("layoutDelete"));
    row->addWidget(add);
    row->addWidget(m_delete);
    row->addStretch(1);
    left->addLayout(row);
    main->addLayout(left);

    auto *right = new QVBoxLayout;
    m_preview = new LayoutPreview(this);
    right->addWidget(m_preview, 1);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(48);
    m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(m_info);
    auto *buttons = new QHBoxLayout;
    m_use = new QPushButton(tr("Use"), this);
    m_use->setObjectName(QStringLiteral("layoutUse"));
    m_use->setDefault(true);
    m_update = new QPushButton(tr("Update"), this);
    m_update->setObjectName(QStringLiteral("layoutUpdate"));
    m_update->setToolTip(tr("Replace the selected layout with the window as it is now"));
    m_rename = new QPushButton(tr("Rename…"), this);
    m_rename->setObjectName(QStringLiteral("layoutRename"));
    m_export = new QPushButton(tr("Export…"), this);
    auto *import = new QPushButton(tr("Import…"), this);
    for (QPushButton *b : {m_use, m_update, m_rename, m_export, import})
        buttons->addWidget(b);
    buttons->addStretch(1);
    right->addLayout(buttons);
    main->addLayout(right, 1);
    lay->addLayout(main, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { selectionChanged(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { useSelected(); });
    connect(m_use, &QPushButton::clicked, this, [this] { useSelected(); });
    connect(add, &QPushButton::clicked, this, [this] {
        QDialog ask(this);
        ask.setWindowTitle(tr("Save Layout"));
        auto *v = new QVBoxLayout(&ask);
        v->addWidget(new QLabel(tr("Name of the new layout:"), &ask));
        auto *name = new QLineEdit(layouts::uniqueName(tr("My Layout")), &ask);
        v->addWidget(name);
        auto *size = new QCheckBox(tr("Also remember the size of the window"), &ask);
        v->addWidget(size);
        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
        v->addWidget(box);
        connect(box, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
        name->selectAll();
        if (ask.exec() != QDialog::Accepted)
            return;
        QString err;
        if (saveCurrentAs(name->text(), size->isChecked(), &err).isEmpty())
            QMessageBox::warning(this, tr("Save Layout"), err);
    });
    connect(m_update, &QPushButton::clicked, this, [this] {
        layouts::Layout l;
        if (!layouts::load(selectedId(), &l))
            return;
        if (QMessageBox::question(this, tr("Update Layout"),
                                  tr("Replace the layout “%1” with the window as it is now?").arg(l.name))
            != QMessageBox::Yes)
            return;
        QString err;
        if (!updateSelected(&err))
            QMessageBox::warning(this, tr("Update Layout"), err);
    });
    connect(m_rename, &QPushButton::clicked, this, [this] {
        layouts::Layout l;
        if (!layouts::load(selectedId(), &l))
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename Layout"), tr("Name:"), QLineEdit::Normal, l.name, &ok);
        QString err;
        if (ok && !renameSelected(name, &err))
            QMessageBox::warning(this, tr("Rename Layout"), err);
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        layouts::Layout l;
        if (!layouts::load(selectedId(), &l))
            return;
        if (QMessageBox::question(this, tr("Delete Layout"), tr("Delete the layout “%1”?").arg(l.name)) != QMessageBox::Yes)
            return;
        QString err;
        if (!removeSelected(&err))
            QMessageBox::warning(this, tr("Delete Layout"), err);
    });
    connect(m_export, &QPushButton::clicked, this, [this] {
        layouts::Layout l;
        if (!layouts::load(selectedId(), &l))
            return;
        const QString f = QFileDialog::getSaveFileName(this, tr("Export Layout"),
                                                       QDir::homePath() + QLatin1Char('/') + l.name + QLatin1String(".qblayout"),
                                                       tr("%1 layouts (*.qblayout)").arg(QLatin1String(QB_APP_NAME)));
        QString err;
        if (!f.isEmpty() && !layouts::exportLayout(l.id, f, &err))
            QMessageBox::warning(this, tr("Export Layout"), err);
    });
    connect(import, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Import Layout"), QDir::homePath(),
                                                       tr("%1 layouts (*.qblayout *.json)").arg(QLatin1String(QB_APP_NAME)));
        if (f.isEmpty())
            return;
        QString err;
        const QString id = layouts::importLayout(f, &err);
        if (id.isEmpty())
            QMessageBox::warning(this, tr("Import Layout"), err);
        else
            reload(id);
    });
    reload(QString());
}

void LayoutsDialog::reload(const QString &selectId)
{
    const QString keep = selectId.isEmpty() ? selectedId() : selectId;
    QSignalBlocker block(m_list);
    m_list->clear();
    for (const layouts::Layout &l : layouts::available()) {
        auto *it = new QListWidgetItem(l.name, m_list);
        it->setData(Qt::UserRole, l.id);
        if (l.kind == layouts::Kind::Addon)
            it->setToolTip(tr("From an add-on"));
        else if (l.kind == layouts::Kind::User) {
            QFont f = it->font();
            f.setItalic(true);
            it->setFont(f);
            it->setToolTip(tr("Your own layout"));
        }
        if (l.id == keep)
            m_list->setCurrentItem(it);
    }
    if (!m_list->currentItem() && m_list->count())
        m_list->setCurrentRow(0);
    selectionChanged();
}

QString LayoutsDialog::selectedId() const
{
    return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
}

void LayoutsDialog::select(const QString &id)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == id)
            m_list->setCurrentRow(i);
}

void LayoutsDialog::selectionChanged()
{
    layouts::Layout l;
    const bool ok = layouts::load(selectedId(), &l);
    const bool own = ok && l.kind == layouts::Kind::User;
    m_use->setEnabled(ok);
    m_export->setEnabled(ok);
    m_update->setEnabled(own);
    m_rename->setEnabled(own);
    m_delete->setEnabled(own);
    m_preview->setState(ok ? l.state : QVariantMap());
    if (!ok) {
        m_info->clear();
        return;
    }
    QString kind = l.kind == layouts::Kind::BuiltIn ? tr("Built-in layout.")
                   : l.kind == layouts::Kind::Addon ? tr("Layout from an add-on.")
                                                    : tr("Your own layout.");
    if (l.state.contains(QStringLiteral("window")))
        kind += QLatin1Char(' ') + tr("Also sets the size of the window.");
    m_info->setText(l.description.isEmpty() ? kind : l.description + QLatin1Char('\n') + kind);
}

QString LayoutsDialog::saveCurrentAs(const QString &name, bool withWindowSize, QString *error)
{
    const QString id = layouts::saveUser(name, m_capture(withWindowSize), error);
    if (!id.isEmpty())
        reload(id);
    return id;
}

bool LayoutsDialog::updateSelected(QString *error)
{
    layouts::Layout l;
    if (!layouts::load(selectedId(), &l))
        return false;
    if (!layouts::updateUser(l.id, m_capture(l.state.contains(QStringLiteral("window"))), error))
        return false;
    reload(l.id);
    return true;
}

bool LayoutsDialog::renameSelected(const QString &name, QString *error)
{
    const QString id = selectedId();
    if (!layouts::renameUser(id, name, error))
        return false;
    reload(id);
    return true;
}

bool LayoutsDialog::removeSelected(QString *error)
{
    if (!layouts::removeUser(selectedId(), error))
        return false;
    reload(QString());
    return true;
}

void LayoutsDialog::useSelected()
{
    layouts::Layout l;
    if (layouts::load(selectedId(), &l))
        m_apply(l.state);
}

} // namespace qb
