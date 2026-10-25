#include "MindMapHelp.h"

#include "AppInfo.h"
#include "core/MindMap.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapUi.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace qb {
namespace mindmaphelp {

using namespace mindmap;

namespace {

// the help text: its pictures come from the board ("qbmm:<name>")
class HelpBrowser : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("qbmm"))
            return picture(name.path());
        return QTextBrowser::loadResource(type, name);
    }
};

// a note of the given text at a place; width as narrow as its text
int addNote(Board &b, const QString &text, const QPointF &pos, double width = 0)
{
    Note n;
    n.id = b.nextId();
    n.text = text;
    n.pos = pos;
    n.width = 400;
    n.width = width > 0 ? width : qMax(40.0, b.fittingWidth(n));
    b.notes << n;
    return n.id;
}

void look(Board &b, int id, const QColor &fill, const QColor &border = QColor())
{
    if (Note *n = b.note(id)) {
        n->look.fill = fill;
        if (border.isValid()) {
            n->look.borderColor = border;
            n->look.borderWeight = 1;
        }
    }
}

const QColor kPractice(255, 243, 180);   // notes to drag (light yellow)
const QColor kTarget(205, 228, 250);     // notes to drop on (light blue)
const QColor kTitle(232, 232, 232);

// the board of one picture
void fillPicture(Board &b, const QString &name)
{
    if (name == QLatin1String("notes")) {
        const int a = addNote(b, QObject::tr("A note"), {0, 0});
        const int s1 = addNote(b, QObject::tr("Notes in a stack"), {170, 0});
        addNote(b, QObject::tr("Ctrl+Enter: the next one"), {170, b.rectOf(s1).bottom() + 4});
        look(b, a, kPractice, QColor(200, 170, 60));
    } else if (name == QLatin1String("drag")) {
        const int a = addNote(b, QObject::tr("Drag me"), {0, 0});
        const int t = addNote(b, QObject::tr("onto me"), {230, 30});
        look(b, a, kPractice, QColor(200, 170, 60));
        look(b, t, kTarget, QColor(90, 140, 200));
    } else {
        const int a = addNote(b, QObject::tr("One idea"), {0, 0});
        const int t = addNote(b, QObject::tr("Another idea"), {250, 0});
        if (name == QLatin1String("line"))
            b.connect(a, t);
        else if (name == QLatin1String("arrow"))
            b.connect(a, t, true, false);
        else if (name == QLatin1String("arrowback"))
            b.connect(a, t, false, true);
        else if (name == QLatin1String("both"))
            b.connect(a, t, true, true);
        else if (name == QLatin1String("label")) {
            b.connect(a, t, true, false);
            b.setLineLabel(a, t, QObject::tr("leads to"));
        } else if (name == QLatin1String("shape")) {
            Note *tn = b.note(t);
            tn->pos = QPointF(0, 40);
            Shape s;
            s.id = b.nextId();
            s.rect = QRectF(-14, -14, qMax(b.rectOf(a).right(), b.rectOf(t).right()) + 28, b.rectOf(t).bottom() + 28);
            s.look.fill = QColor(225, 235, 225);
            s.look.borderWeight = 1;
            s.look.borderColor = QColor(150, 175, 150);
            b.shapes << s;
            b.connect(a, t);
        }
    }
}

// what the help page explains next to a picture: a hand pulling the note over
void paintDragHint(QPainter &p, const QRectF &from, const QRectF &to)
{
    p.setRenderHint(QPainter::Antialiasing);
    // a pale copy of the note on its way
    const QRectF ghost = from.translated(to.center() - from.center() + QPointF(-14, -10));
    p.setPen(QPen(QColor(200, 170, 60, 150), 1, Qt::DashLine));
    p.setBrush(QColor(255, 243, 180, 110));
    p.drawRoundedRect(ghost, 4, 4);
    // the way, curved, with a head
    QPainterPath path(QPointF(from.right() + 6, from.center().y()));
    const QPointF end(ghost.left() - 4, ghost.center().y());
    path.cubicTo(QPointF(from.right() + 60, from.top() - 30), QPointF(end.x() - 60, end.y() - 40), end);
    p.setPen(QPen(QColor(110, 110, 110), 1.6, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    const QPointF dir = end - path.pointAtPercent(0.95);
    const double len = std::hypot(dir.x(), dir.y());
    if (len > 0) {
        const QPointF u = dir / len, n(-u.y(), u.x());
        QPolygonF head;
        head << end << end - u * 9 + n * 4.5 << end - u * 9 - n * 4.5;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(110, 110, 110));
        p.drawPolygon(head);
    }
    // the mouse pointer
    const QPointF m = ghost.center() + QPointF(6, 2);
    QPolygonF arrow;
    arrow << m << m + QPointF(0, 17) << m + QPointF(4.5, 13) << m + QPointF(8, 20) << m + QPointF(11, 18.5) << m + QPointF(7.5, 11.5)
          << m + QPointF(13, 11.5);
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    p.drawPolygon(arrow);
}

} // namespace

QImage picture(const QString &name)
{
    MindMapDoc doc(nullptr, QString());
    doc.board.notes.clear();
    doc.board.shapes.clear();
    fillPicture(doc.board, name);
    doc.scene()->rebuild();
    QRectF src = doc.scene()->contentRect();
    if (name == QLatin1String("drag"))
        src = src.united(src.adjusted(0, -36, 0, 18));
    src.adjust(-16, -16, 16, 16);
    QImage img(src.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    img.fill(doc.board.settings.background);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    doc.scene()->render(&p, QRectF(QPointF(0, 0), src.size()), src, true, false);
    if (name == QLatin1String("drag") && doc.board.notes.size() >= 2) {
        const QPointF off = -src.topLeft();
        paintDragHint(p, doc.board.rectOf(doc.board.notes.at(0).id).translated(off), doc.board.rectOf(doc.board.notes.at(1).id).translated(off));
    }
    p.setPen(QColor(200, 200, 200));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(img.rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    return img;
}

void fillExampleBoard(MindMapDoc *doc)
{
    Board &b = doc->board;
    b.notes.clear();
    b.shapes.clear();
    b.lineLabels.clear();
    const int title = addNote(b, QObject::tr("Example Board: try everything here. Nothing on it belongs to your project."), {20, 10}, 520);
    if (Note *n = b.note(title)) {
        n->fontSize = 15;
        n->bold = true;
    }
    look(b, title, kTitle);
    // 1 a line
    addNote(b, QObject::tr("1  A line: drag the yellow note onto the blue one and let go. Again, and the line is gone."), {20, 80}, 520);
    const int l1 = addNote(b, QObject::tr("Drag me"), {40, 130});
    const int l2 = addNote(b, QObject::tr("Drop here"), {300, 130});
    look(b, l1, kPractice, QColor(200, 170, 60));
    look(b, l2, kTarget, QColor(90, 140, 200));
    // 2 arrows
    addNote(b, QObject::tr("2  Arrows: hold Alt while you let go (an arrow to the note you drop on), Ctrl+Alt (an arrow back), "
                           "Ctrl+Alt+Shift (both ends). Do it again to take the arrow away."),
            {20, 190}, 520);
    const int a1 = addNote(b, QObject::tr("Drag me with Alt"), {40, 260});
    const int a2 = addNote(b, QObject::tr("Drop here"), {300, 260});
    look(b, a1, kPractice, QColor(200, 170, 60));
    look(b, a2, kTarget, QColor(90, 140, 200));
    // examples of finished lines and arrows
    const int e1 = addNote(b, QObject::tr("A line"), {620, 80});
    const int e2 = addNote(b, QObject::tr("joins two ideas"), {800, 80});
    b.connect(e1, e2);
    const int e3 = addNote(b, QObject::tr("An arrow"), {620, 140});
    const int e4 = addNote(b, QObject::tr("shows a direction"), {800, 140});
    b.connect(e3, e4, true, false);
    const int e5 = addNote(b, QObject::tr("Both ends"), {620, 200});
    const int e6 = addNote(b, QObject::tr("go both ways"), {800, 200});
    b.connect(e5, e6, true, true);
    const int e7 = addNote(b, QObject::tr("Cause"), {620, 260});
    const int e8 = addNote(b, QObject::tr("Effect"), {800, 260});
    b.connect(e7, e8, true, false);
    b.setLineLabel(e7, e8, QObject::tr("leads to"));
    // 3 more ways
    addNote(b, QObject::tr("3  With the keyboard: select a note and press Alt+Right (a new note joined by a line) or Alt+Shift+Right "
                           "(joined by an arrow). Select two notes and press Ctrl+. (line) or Ctrl+Alt+. (arrow from the first one)."),
            {20, 330}, 520);
    addNote(b, QObject::tr("4  Words on a line: select both notes and press Ctrl+Shift+L (Notes > Connection Label). A double-click "
                           "on a line puts a new note into it."),
            {20, 420}, 520);
    // 5 a shape
    const int s1 = addNote(b, QObject::tr("5  A background shape: drag its edge,"), {640, 360});
    const int s2 = addNote(b, QObject::tr("the notes on it come along"), {640, 400});
    Shape sh;
    sh.id = b.nextId();
    sh.rect = QRectF(620, 340, qMax(b.rectOf(s1).right(), b.rectOf(s2).right()) - 600, b.rectOf(s2).bottom() - 320);
    sh.look.fill = QColor(225, 235, 225);
    sh.look.borderWeight = 1;
    sh.look.borderColor = QColor(150, 175, 150);
    b.shapes << sh;
    // 6 new notes
    addNote(b, QObject::tr("6  A double-click on an empty place makes a new note. Ctrl+Enter makes one under the selected note."),
            {20, 500}, 520);
    addNote(b, QObject::tr("Help: the ? button of the board, or Help > Mind Map Help"), {640, 500}, 300);
    doc->scene()->rebuild();
}

QWidget *showHelp(QWidget *parent)
{
    static QPointer<QDialog> win;
    if (!win) {
        win = new QDialog(parent ? parent->window() : nullptr);
        win->setAttribute(Qt::WA_DeleteOnClose);
        win->setObjectName(QStringLiteral("mindMapHelp"));
        win->setWindowTitle(QObject::tr("Mind Map Help"));
        win->resize(760, 820);
        auto *l = new QVBoxLayout(win);
        auto *tb = new HelpBrowser(win);
        tb->setObjectName(QStringLiteral("mindMapHelpText"));
        tb->setOpenExternalLinks(true);
        QFile f(QStringLiteral(":/docs/mindmap-help.md"));
        f.open(QIODevice::ReadOnly);
        QString text = QString::fromUtf8(f.readAll());
        text.replace(QLatin1String("{{APP}}"), QLatin1String(QB_APP_NAME));
        tb->setMarkdown(text);
        l->addWidget(tb);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, win);
        QPushButton *ex = bb->addButton(QObject::tr("Open Example Board"), QDialogButtonBox::ActionRole);
        ex->setObjectName(QStringLiteral("openExampleBoard"));
        QObject::connect(ex, &QPushButton::clicked, win, [] { openExampleBoard(win); });
        QObject::connect(bb, &QDialogButtonBox::rejected, win, &QDialog::close);
        l->addWidget(bb);
    }
    win->show();
    win->raise();
    win->activateWindow();
    return win;
}

MindMapWindow *openExampleBoard(QWidget *parent)
{
    auto *doc = new MindMapDoc(nullptr, QString(), qApp);
    doc->setTitle(QObject::tr("Example Board"));
    fillExampleBoard(doc);
    MindMapWindow *w = MindMapWindow::open(doc, parent ? parent->window() : nullptr);
    // the board goes with its window
    QObject::connect(w, &QObject::destroyed, doc, &QObject::deleteLater);
    return w;
}

} // namespace mindmaphelp
} // namespace qb
