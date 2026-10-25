#include "ui/ColorPalette.h"
#include "core/Project.h"

#include <QActionGroup>
#include <QColorDialog>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPointer>

namespace qb {
namespace palette {

namespace {
// A QPointer cannot hold a const object; the palette only reads the labels.
QPointer<Project> g_project;
} // namespace

QList<Entry> standard()
{
    // The same values as the format bar's popup of 0.17 (ColorPickButton::palette()).
    return {{QObject::tr("Red"), QColor(214, 39, 40)},     {QObject::tr("Blue"), QColor(31, 100, 200)},
            {QObject::tr("Yellow"), QColor(255, 221, 0)},  {QObject::tr("Green"), QColor(44, 160, 44)},
            {QObject::tr("White"), QColor(255, 255, 255)}, {QObject::tr("Black"), QColor(0, 0, 0)}};
}

QList<Entry> labels(const Project *project)
{
    if (!project)
        project = g_project.data();
    QList<Entry> out;
    if (!project)
        return out;
    for (const Label &l : project->labels) {
        // id 0 is "No Label"; a label without a color has nothing to offer
        if (l.id == 0 || !l.color.isValid() || l.name.trimmed().isEmpty())
            continue;
        Entry e;
        e.name = l.name;
        e.color = l.color;
        e.fromLabel = true;
        out << e;
    }
    return out;
}

QList<Entry> all(const Project *project)
{
    QList<Entry> out = labels(project);
    for (const Entry &s : standard()) {
        bool dup = false;
        for (const Entry &e : out)
            dup |= e.color.rgb() == s.color.rgb();
        if (!dup)
            out << s;
    }
    return out;
}

void setProject(const Project *project) { g_project = const_cast<Project *>(project); }

const Project *currentProject() { return g_project.data(); }

QIcon swatch(const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.setBrush(color.isValid() ? QBrush(color) : QBrush(Qt::white));
    p.drawRoundedRect(QRectF(1.5, 1.5, size - 3, size - 3), 3, 3);
    if (!color.isValid()) {
        // "no color": a red slash, as in the format bar's popup
        p.setPen(QPen(QColor(200, 40, 40), 1.6));
        p.drawLine(QPointF(4, size - 4), QPointF(size - 4, 4));
    }
    p.end();
    return QIcon(pm);
}

void fillMenu(QMenu *menu, const Project *project, const QColor &current, const std::function<void(const QColor &)> &chosen,
              QWidget *dialogParent, const QString &noneText, const QString &dialogTitle)
{
    auto *group = new QActionGroup(menu);
    bool any = false;
    auto add = [&](const QString &name, const QColor &c) {
        QAction *a = menu->addAction(swatch(c), name);
        a->setCheckable(true);
        a->setChecked(c.isValid() ? (current.isValid() && current.rgb() == c.rgb()) : !current.isValid());
        a->setData(c);
        group->addAction(a);
        QObject::connect(a, &QAction::triggered, menu, [chosen, c] { chosen(c); });
        any = true;
    };
    const QList<Entry> fromLabels = labels(project);
    for (const Entry &e : fromLabels)
        add(e.name, e.color);
    if (!fromLabels.isEmpty())
        menu->addSeparator();
    for (const Entry &e : standard()) {
        bool dup = false;
        for (const Entry &l : fromLabels)
            dup |= l.color.rgb() == e.color.rgb();
        if (!dup)
            add(e.name, e.color);
    }
    if (!noneText.isEmpty()) {
        menu->addSeparator();
        add(noneText, QColor());
    }
    Q_UNUSED(any);
    menu->addSeparator();
    QPointer<QWidget> parent(dialogParent);
    const QString title = dialogTitle.isEmpty() ? QObject::tr("Color") : dialogTitle;
    QAction *other = menu->addAction(QObject::tr("Other..."));
    other->setObjectName(QStringLiteral("paletteOther"));
    QObject::connect(other, &QAction::triggered, menu, [chosen, current, parent, title] {
        const QColor c = QColorDialog::getColor(current.isValid() ? current : Qt::white, parent.data(), title);
        if (c.isValid())
            chosen(c);
    });
}

} // namespace palette
} // namespace qb
