#include "ui/ObjectsUi.h"
#include "ui/TextEditor.h"
#include "ui/Zoom.h"
#include "core/BinderItem.h"
#include "core/Project.h"
#include "core/ItemIcons.h"

#include <QAbstractTextDocumentLayout>
#include <memory>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QImageReader>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace qb {

using namespace objects;

// ---------------------------------------------------------------- helpers

QColor readableOn(const QColor &bg)
{
    const double l = 0.299 * bg.redF() + 0.587 * bg.greenF() + 0.114 * bg.blueF();
    return l > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
}

static QIcon dotIcon(const QColor &c, bool folder = false)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c.darker(140), 1));
    p.setBrush(c);
    if (folder)
        p.drawRoundedRect(QRectF(1.5, 3.5, 11, 8), 2, 2);
    else
        p.drawEllipse(QRectF(2.5, 2.5, 9, 9));
    return QIcon(pm);
}

static QString palettedEdge(const QColor &c)
{
    return readableOn(c) == QColor(Qt::white) ? c.darker(220).name() : c.darker(160).name();
}

static QString chipStyle(const QColor &c, bool checked = false)
{
    if (!c.isValid())
        return QStringLiteral("QToolButton { border: 1px solid palette(mid); border-radius: 9px; padding: 1px 8px; }"
                              "QToolButton:hover { background: palette(midlight); }");
    return QStringLiteral("QToolButton { background: %1; color: %2; border: %3px solid %4; border-radius: 9px; padding: 1px 8px; %5 }"
                          "QToolButton:hover { background: %6; }")
        .arg(c.name(), readableOn(c).name())
        .arg(checked ? 3 : 1)
        .arg(checked ? palettedEdge(c) : c.darker(130).name(), checked ? QStringLiteral("font-weight: bold;") : QString(),
             c.lighter(115).name());
}

// ---------------------------------------------------------------- flow layout (chips that wrap)

class FlowLayout : public QLayout
{
public:
    FlowLayout(QWidget *parent, int hSpacing = 4, int vSpacing = 4) : QLayout(parent), m_h(hSpacing), m_v(vSpacing)
    {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() override
    {
        while (QLayoutItem *it = takeAt(0))
            delete it;
    }
    void addItem(QLayoutItem *item) override { m_items << item; }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem *takeAt(int i) override { return i >= 0 && i < m_items.size() ? m_items.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
    void setGeometry(const QRect &r) override
    {
        QLayout::setGeometry(r);
        doLayout(r, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize s;
        for (QLayoutItem *it : m_items)
            s = s.expandedTo(it->minimumSize());
        const QMargins m = contentsMargins();
        return s + QSize(m.left() + m.right(), m.top() + m.bottom());
    }

private:
    int doLayout(const QRect &rect, bool test) const
    {
        const QMargins m = contentsMargins();
        const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        int x = r.x(), y = r.y(), line = 0;
        for (QLayoutItem *it : m_items) {
            if (it->widget() && it->widget()->isHidden())
                continue;
            const QSize sz = it->sizeHint();
            int next = x + sz.width() + m_h;
            if (next - m_h > r.right() && line > 0) {
                x = r.x();
                y = y + line + m_v;
                next = x + sz.width() + m_h;
                line = 0;
            }
            if (!test)
                it->setGeometry(QRect(QPoint(x, y), sz));
            x = next;
            line = qMax(line, sz.height());
        }
        return y + line - rect.y() + m.bottom();
    }
    QList<QLayoutItem *> m_items;
    int m_h, m_v;
};

// ---------------------------------------------------------------- chooser

static QString pathOf(const BinderItem *it)
{
    QStringList parts;
    for (const BinderItem *p = it->parent; p && p->parent; p = p->parent)
        parts.prepend(p->title);
    return parts.join(QLatin1String(" / "));
}

ObjectChooser::ObjectChooser(Project *project, Mode mode, const QStringList &targets, const QStringList &exclude, QWidget *parent)
    : QDialog(parent), m_project(project), m_mode(mode), m_targets(targets), m_exclude(exclude)
{
    setObjectName(QStringLiteral("objectChooser"));
    auto *l = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("chooserSearch"));
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(mode == Objects ? tr("Search, or type the name of a new object") : tr("Search the binder"));
    l->addWidget(m_search);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("chooserList"));
    l->addWidget(m_list, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &ObjectChooser::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_search, &QLineEdit::textChanged, this, &ObjectChooser::fill);
    connect(m_list, &QListWidget::itemActivated, this, &ObjectChooser::accept);
    fill();
    resize(420, 460);
}

void ObjectChooser::fill()
{
    m_list->clear();
    if (!m_project)
        return;
    const QString q = m_search->text().trimmed();
    if (m_mode == Documents) {
        for (BinderItem *it : m_project->allItems()) {
            if (it->isRootFolder() || m_exclude.contains(it->id) || m_project->isInTrash(it))
                continue;
            if (!q.isEmpty() && !it->title.contains(q, Qt::CaseInsensitive))
                continue;
            const QString path = pathOf(it);
            auto *li = new QListWidgetItem(icons::forItem(m_project, it), path.isEmpty() ? it->title : QStringLiteral("%1   (%2)").arg(it->title, path), m_list);
            li->setData(Qt::UserRole, it->id);
        }
    } else {
        Store *st = m_project->objects();
        bool exact = false;
        QList<QListWidgetItem *> found;
        for (const Object *o : st->objects()) {
            if ((!m_targets.isEmpty() && !m_targets.contains(o->category)) || m_exclude.contains(o->id))
                continue;
            if (!q.isEmpty() && !o->name.contains(q, Qt::CaseInsensitive))
                continue;
            exact = exact || o->name.compare(q, Qt::CaseInsensitive) == 0;
            const Category *c = st->category(o->category);
            auto *li = new QListWidgetItem(dotIcon(st->colorOf(*o)), QStringLiteral("%1   (%2)").arg(o->name, c ? c->name : o->category));
            li->setData(Qt::UserRole, o->id);
            found << li;
        }
        // the objects that match first, then "New in <category>" for a name that is not there yet
        for (QListWidgetItem *li : found)
            m_list->addItem(li);
        if (!q.isEmpty() && !exact) {
            for (const Category &c : st->schema().categories)
                if (m_targets.isEmpty() || m_targets.contains(c.id)) {
                    auto *li = new QListWidgetItem(dotIcon(c.color), tr("New in %1: “%2”").arg(c.name, q), m_list);
                    li->setData(Qt::UserRole, QStringLiteral("new:") + c.id);
                    QFont f = li->font();
                    f.setItalic(true);
                    li->setFont(f);
                }
        }
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void ObjectChooser::accept()
{
    QListWidgetItem *it = m_list->currentItem();
    if (!it || !m_project)
        return;
    const QString id = it->data(Qt::UserRole).toString();
    if (id.startsWith(QLatin1String("new:")))
        m_chosen = m_project->objects()->create(id.mid(4), QString(), m_search->text().trimmed());
    else
        m_chosen = id;
    QDialog::accept();
}

QString ObjectChooser::choose(Project *project, Mode mode, const QStringList &targets, const QStringList &exclude, QWidget *parent,
                              const QString &title)
{
    ObjectChooser dlg(project, mode, targets, exclude, parent);
    dlg.setWindowTitle(title);
    return dlg.exec() == QDialog::Accepted ? dlg.chosenId() : QString();
}

// ---------------------------------------------------------------- long text with links

ObjectTextEdit::ObjectTextEdit(QWidget *parent) : QTextEdit(parent)
{
    setAcceptRichText(false);
    setTabChangesFocus(true);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setToolTip(tr("A click on a link follows it; the right-click menu links the selected words to an object or a document."));
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        if (!m_loading)
            emit edited();
    });
    // as high as its text (between 3 and 14 lines)
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this](const QSizeF &s) {
        const int line = fontMetrics().lineSpacing();
        setFixedHeight(qBound(3 * line + 12, int(s.height()) + 12, 14 * line + 12));
    });
    setFixedHeight(3 * fontMetrics().lineSpacing() + 12);
}

void ObjectTextEdit::setMarkdownText(const QString &md)
{
    m_loading = true;
    setMarkdown(md);
    // links look like links
    QTextCursor c(document());
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.charFormat().isAnchor())
                continue;
            QTextCharFormat f;
            f.setForeground(palette().link());
            f.setFontUnderline(true);
            c.setPosition(fr.position());
            c.setPosition(fr.position() + fr.length(), QTextCursor::KeepAnchor);
            c.mergeCharFormat(f);
        }
    document()->setModified(false);
    m_loading = false;
}

// Text inside an HTML span of a Markdown text: characters that HTML or Markdown would
// read as markup are written as character references (Qt reads them back as text).
static QString spanText(const QString &t)
{
    QString r;
    for (const QChar ch : t) {
        switch (ch.unicode()) {
        case '&': r += QLatin1String("&amp;"); break;
        case '<': r += QLatin1String("&lt;"); break;
        case '>': r += QLatin1String("&gt;"); break;
        case '"': r += QLatin1String("&quot;"); break;
        case '*': case '_': case '`': case '~': case '[': case ']': case '\\': case '#': case '!': case '|':
            r += QStringLiteral("&#%1;").arg(ch.unicode());
            break;
        default: r += ch;
        }
    }
    return r;
}

// Markdown holds bold, italic and strikethrough; Qt writes underline as italic and
// loses colors. Since 0.21.1 the pieces with a text color, a highlight, an underline
// or more than one of bold / italic / strikethrough are written as HTML spans with
// their look, which Qt's Markdown reader (also in 0.20 and 0.21) shows again.
QString ObjectTextEdit::markdownText() const
{
    if (document()->toPlainText().trimmed().isEmpty())
        return QString();
    std::unique_ptr<QTextDocument> d(document()->clone());
    struct Piece {
        int pos, len;
        QStringList spans; // one per line of the piece
    };
    QList<Piece> pieces;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const QTextCharFormat cf = f.charFormat();
            if (cf.isAnchor() || cf.isImageFormat() || f.text().trimmed().isEmpty())
                continue;
            const bool bold = cf.fontWeight() >= QFont::DemiBold, italic = cf.fontItalic(), strike = cf.fontStrikeOut();
            const bool under = cf.fontUnderline();
            const bool color = cf.hasProperty(QTextFormat::ForegroundBrush) && cf.foreground().style() != Qt::NoBrush;
            const bool high = cf.hasProperty(QTextFormat::BackgroundBrush) && cf.background().style() != Qt::NoBrush;
            if (!under && !color && !high && int(bold) + int(italic) + int(strike) < 2)
                continue; // Markdown can hold it
            QStringList style;
            if (bold)
                style << QStringLiteral("font-weight:700");
            if (italic)
                style << QStringLiteral("font-style:italic");
            if (under || strike)
                style << QStringLiteral("text-decoration:%1%2")
                             .arg(under ? QStringLiteral("underline") : QString(),
                                  strike ? (under ? QStringLiteral(" line-through") : QStringLiteral("line-through")) : QString());
            if (color)
                style << QStringLiteral("color:%1").arg(cf.foreground().color().name());
            if (high)
                style << QStringLiteral("background-color:%1").arg(cf.background().color().name());
            Piece p{f.position(), f.length(), {}};
            for (const QString &line : f.text().split(QChar::LineSeparator))
                p.spans << (line.isEmpty() ? QString()
                                           : QStringLiteral("<span style=\"%1;\">%2</span>").arg(style.join(QLatin1String("; ")), spanText(line)));
            pieces << p;
        }
    QStringList html;
    for (int i = int(pieces.size()) - 1; i >= 0; --i) {
        QTextCursor c(d.get());
        c.setPosition(pieces[i].pos);
        c.setPosition(pieces[i].pos + pieces[i].len, QTextCursor::KeepAnchor);
        QStringList tokens;
        for (const QString &span : std::as_const(pieces[i].spans)) {
            tokens << (span.isEmpty() ? QString() : QStringLiteral("\uE000%1\uE001").arg(html.size()));
            if (!span.isEmpty())
                html << span;
        }
        c.insertText(tokens.join(QChar::LineSeparator), QTextCharFormat());
    }
    QString md = d->toMarkdown(QTextDocument::MarkdownDialectGitHub);
    for (int i = 0; i < html.size(); ++i)
        md.replace(QStringLiteral("\uE000%1\uE001").arg(i), html[i]);
    while (md.endsWith(QLatin1Char('\n')))
        md.chop(1);
    return md;
}

void ObjectTextEdit::changeFormat(const std::function<void(QTextCharFormat &)> &fn)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection()) {
        // what is typed next
        QTextCharFormat f = currentCharFormat();
        fn(f);
        setCurrentCharFormat(f);
        return;
    }
    const int s = c.selectionStart(), e = c.selectionEnd();
    struct Run {
        int s, e;
        QTextCharFormat f;
    };
    QList<Run> runs;
    for (QTextBlock b = document()->findBlock(s); b.isValid() && b.position() < e; b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const int a = qMax(s, fr.position()), z = qMin(e, fr.position() + fr.length());
            if (a < z)
                runs.append({a, z, fr.charFormat()});
        }
    QTextCursor w(document());
    w.beginEditBlock();
    for (Run &r : runs) {
        fn(r.f);
        w.setPosition(r.s);
        w.setPosition(r.e, QTextCursor::KeepAnchor);
        w.setCharFormat(r.f);
    }
    w.endEditBlock();
}

bool ObjectTextEdit::isOn(Toggle what) const
{
    // the whole selection, not the character in front of the cursor (0.40.1)
    const ToggleState s = toggleStateOf(this);
    switch (what) {
    case Toggle::Bold: return s.bold;
    case Toggle::Italic: return s.italic;
    case Toggle::Underline: return s.underline;
    case Toggle::Strike: return s.strike;
    }
    return false;
}

void ObjectTextEdit::toggle(Toggle what)
{
    const bool on = !isOn(what);
    changeFormat([what, on](QTextCharFormat &f) {
        switch (what) {
        case Toggle::Bold:
            if (on)
                f.setFontWeight(QFont::Bold);
            else
                f.clearProperty(QTextFormat::FontWeight);
            break;
        case Toggle::Italic: f.setFontItalic(on); break;
        case Toggle::Underline:
            if (!f.isAnchor())
                f.setFontUnderline(on);
            break;
        case Toggle::Strike: f.setFontStrikeOut(on); break;
        }
    });
}

void ObjectTextEdit::applyTextColor(const QColor &c)
{
    changeFormat([c](QTextCharFormat &f) {
        if (f.isAnchor())
            return; // links keep the link color
        if (c.isValid())
            f.setForeground(c);
        else
            f.clearForeground();
    });
}

void ObjectTextEdit::applyHighlight(const QColor &c)
{
    changeFormat([c](QTextCharFormat &f) {
        if (c.isValid())
            f.setBackground(c);
        else
            f.clearBackground();
    });
}

void ObjectTextEdit::clearFormatting()
{
    changeFormat([this](QTextCharFormat &f) {
        QTextCharFormat plain;
        if (f.isAnchor()) {
            plain.setAnchor(true);
            plain.setAnchorHref(f.anchorHref());
            plain.setForeground(palette().link());
            plain.setFontUnderline(true);
        }
        f = plain;
    });
}

QColor ObjectTextEdit::textColorAtCursor() const
{
    const QTextCharFormat f = currentCharFormat();
    return f.hasProperty(QTextFormat::ForegroundBrush) && !f.isAnchor() ? f.foreground().color() : QColor();
}

QColor ObjectTextEdit::highlightAtCursor() const
{
    const QTextCharFormat f = currentCharFormat();
    return f.hasProperty(QTextFormat::BackgroundBrush) ? f.background().color() : QColor();
}

void ObjectTextEdit::linkSelection(const QString &href, const QString &text)
{
    QTextCursor c = textCursor();
    QTextCharFormat f;
    f.setAnchor(true);
    f.setAnchorHref(href);
    f.setForeground(palette().link());
    f.setFontUnderline(true);
    if (c.hasSelection()) {
        c.mergeCharFormat(f);
    } else {
        c.insertText(text, f);
        QTextCharFormat plain;
        plain.setAnchor(false);
        c.setCharFormat(plain);
        setTextCursor(c);
        setCurrentCharFormat(QTextCharFormat());
    }
    emit edited();
}

void ObjectTextEdit::removeLinkAtCursor()
{
    QTextCursor c = textCursor();
    const QTextBlock b = c.block();
    const int pos = c.position();
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment fr = it.fragment();
        if (!fr.charFormat().isAnchor() || pos < fr.position() || pos > fr.position() + fr.length())
            continue;
        QTextCursor s(document());
        s.setPosition(fr.position());
        s.setPosition(fr.position() + fr.length(), QTextCursor::KeepAnchor);
        QTextCharFormat f = fr.charFormat();
        f.setAnchor(false);
        f.setAnchorHref(QString());
        f.setFontUnderline(false);
        f.clearForeground();
        s.setCharFormat(f);
        emit edited();
        return;
    }
}

void ObjectTextEdit::linkToObject()
{
    if (!m_project)
        return;
    const QString id = ObjectChooser::choose(m_project, ObjectChooser::Objects, {}, {}, this, tr("Link to Object"));
    if (const Object *o = id.isEmpty() ? nullptr : m_project->objects()->object(id))
        linkSelection(linkTo(id), o->name);
}

void ObjectTextEdit::linkToDocument()
{
    if (!m_project)
        return;
    const QString id = ObjectChooser::choose(m_project, ObjectChooser::Documents, {}, {}, this, tr("Link to Document"));
    if (BinderItem *it = id.isEmpty() ? nullptr : m_project->item(id))
        linkSelection(QStringLiteral("qb://") + id, it->title);
}

void ObjectTextEdit::mousePressEvent(QMouseEvent *e)
{
    m_pressPos = e->pos();
    QTextEdit::mousePressEvent(e);
}

void ObjectTextEdit::mouseReleaseEvent(QMouseEvent *e)
{
    QTextEdit::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && (e->pos() - m_pressPos).manhattanLength() < 4 && !textCursor().hasSelection()) {
        const QString href = anchorAt(e->pos());
        if (!href.isEmpty())
            emit linkActivated(href);
    }
}

void ObjectTextEdit::mouseMoveEvent(QMouseEvent *e)
{
    viewport()->setCursor(anchorAt(e->pos()).isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
    QTextEdit::mouseMoveEvent(e);
}

void ObjectTextEdit::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu *menu = createStandardContextMenu(e->pos());
    menu->addSeparator();
    const QString href = anchorAt(e->pos());
    QAction *obj = menu->addAction(tr("Link to Object…"), this, &ObjectTextEdit::linkToObject);
    QAction *doc = menu->addAction(tr("Link to Document…"), this, &ObjectTextEdit::linkToDocument);
    obj->setEnabled(m_project != nullptr);
    doc->setEnabled(m_project != nullptr);
    if (!href.isEmpty()) {
        menu->addAction(tr("Follow Link"), this, [this, href] { emit linkActivated(href); });
        menu->addAction(tr("Remove Link"), this, [this, e] {
            setTextCursor(cursorForPosition(e->pos()));
            removeLinkAtCursor();
        });
    }
    menu->exec(e->globalPos());
    delete menu;
}

void ObjectTextEdit::focusOutEvent(QFocusEvent *e) { QTextEdit::focusOutEvent(e); }

// ---------------------------------------------------------------- chips

ChipList::ChipList(bool addButton, QWidget *parent) : QWidget(parent)
{
    m_flow = new FlowLayout(this);
    if (addButton) {
        m_add = new QToolButton(this);
        m_add->setObjectName(QStringLiteral("chipAdd"));
        m_add->setText(QStringLiteral("+"));
        m_add->setToolTip(tr("Add a link…"));
        m_add->setAutoRaise(true);
        connect(m_add, &QToolButton::clicked, this, &ChipList::addRequested);
        m_flow->addWidget(m_add);
    }
}

void ChipList::setChips(const QList<Chip> &chips)
{
    m_chips = chips;
    while (QLayoutItem *it = m_flow->takeAt(0))
        delete it;
    qDeleteAll(m_buttons);
    m_buttons.clear();
    for (const Chip &c : chips) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("chip:") + c.id);
        b->setText(c.text);
        b->setStyleSheet(chipStyle(c.color));
        b->setToolTip(c.tip.isEmpty() ? tr("Click to open; right-click to remove") : c.tip);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        const QString id = c.id;
        connect(b, &QToolButton::clicked, this, [this, id] { emit activated(id); });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, b, id](const QPoint &p) {
            QMenu m;
            m.addAction(tr("Open"), this, [this, id] { emit activated(id); });
            if (m_add)
                m.addAction(tr("Remove the Link"), this, [this, id] { emit removeRequested(id); });
            m.exec(b->mapToGlobal(p));
        });
        m_flow->addWidget(b);
        m_buttons << b;
    }
    if (m_add)
        m_flow->addWidget(m_add);
    updateGeometry();
}

QToolButton *ChipList::chipButton(const QString &id) const
{
    for (QToolButton *b : m_buttons)
        if (b->objectName() == QLatin1String("chip:") + id)
            return b;
    return nullptr;
}

// ---------------------------------------------------------------- picture field

namespace {

class PictureLabel : public QLabel
{
public:
    std::function<void(const QString &)> onFile;
    std::function<void()> onRemove;
    explicit PictureLabel(QWidget *parent) : QLabel(parent)
    {
        setAcceptDrops(true);
        setAlignment(Qt::AlignCenter);
        setFrameShape(QFrame::StyledPanel);
        setMinimumHeight(70);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QObject::tr("Click to choose a picture, or drop one here; right-click to remove it"));
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        const QString f = QFileDialog::getOpenFileName(this, QObject::tr("Choose a Picture"), QString(),
                                                       QObject::tr("Pictures (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg)"));
        if (!f.isEmpty() && onFile)
            onFile(f);
    }
    void contextMenuEvent(QContextMenuEvent *e) override
    {
        QMenu m;
        m.addAction(QObject::tr("Choose a Picture…"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QObject::tr("Choose a Picture"));
            if (!f.isEmpty() && onFile)
                onFile(f);
        });
        QAction *rm = m.addAction(QObject::tr("Remove the Picture"), [this] {
            if (onRemove)
                onRemove();
        });
        rm->setEnabled(pixmap().isNull() == false);
        m.exec(e->globalPos());
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        const QList<QUrl> urls = e->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile() && onFile)
            onFile(urls.first().toLocalFile());
    }
};

// lists folders and objects; drops move them (the store does it, the list is rebuilt)
const int kIdRole = Qt::UserRole;
const int kKindRole = Qt::UserRole + 1; // "object" or "folder"

class ObjectTree : public QTreeWidget
{
public:
    std::function<void(QTreeWidgetItem *, QTreeWidgetItem *)> onDrop;
    using QTreeWidget::QTreeWidget;

protected:
    void dropEvent(QDropEvent *e) override
    {
        QTreeWidgetItem *dragged = currentItem();
        const QPoint pos = e->position().toPoint();
        QTreeWidgetItem *target = itemAt(pos);
        if (target) {
            // onto the middle of a folder: into it; anywhere else on an item: into that item's folder
            const QRect r = visualItemRect(target);
            const bool folder = target->data(0, kKindRole).toString() == QLatin1String("folder");
            const bool onto = folder && pos.y() > r.top() + r.height() / 4 && pos.y() < r.bottom() - r.height() / 4;
            if (!onto)
                target = target->parent();
        }
        e->setDropAction(Qt::IgnoreAction);
        e->accept();
        if (dragged && onDrop)
            onDrop(dragged, target);
    }
};

} // namespace

// ---------------------------------------------------------------- form

ObjectForm::ObjectForm(bool wide, QWidget *parent) : QWidget(parent), m_wide(wide)
{
    setObjectName(QStringLiteral("objectForm"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);
    m_empty = new QLabel(tr("Choose an object in the list, or make one with +."), m_stack);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_stack->addWidget(m_empty);
    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_body = new QWidget(scroll);
    scroll->setWidget(m_body);
    m_stack->addWidget(scroll);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(6, 6, 6, 6);
    // header: color stripe, category, name, color
    m_header = new QWidget(m_body);
    m_header->setObjectName(QStringLiteral("objectHeader"));
    m_header->setAutoFillBackground(true);
    auto *hl = new QVBoxLayout(m_header);
    hl->setContentsMargins(8, 4, 8, 6);
    auto *top = new QHBoxLayout;
    m_category = new QLabel(m_header);
    m_category->setObjectName(QStringLiteral("objectCategory"));
    top->addWidget(m_category, 1);
    m_colorButton = new QToolButton(m_header);
    m_colorButton->setObjectName(QStringLiteral("objectColor"));
    m_colorButton->setText(tr("Color"));
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
    m_colorButton->setAutoRaise(true);
    auto *cm = new QMenu(m_colorButton);
    cm->addAction(tr("Use the Category's Color"), this, [this] {
        m_obj.color = QColor();
        commit();
        reloadValues();
    });
    cm->addAction(tr("Choose a Color…"), this, [this] {
        const QColor c = QColorDialog::getColor(store() ? store()->colorOf(m_obj) : QColor(Qt::gray), this, tr("Color of the Object"));
        if (!c.isValid())
            return;
        m_obj.color = c;
        commit();
        reloadValues();
    });
    m_colorButton->setMenu(cm);
    top->addWidget(m_colorButton);
    hl->addLayout(top);
    m_name = new QLineEdit(m_header);
    m_name->setObjectName(QStringLiteral("objectName"));
    QFont nf = m_name->font();
    nf.setPointSizeF(nf.pointSizeF() * 1.35);
    nf.setBold(true);
    m_name->setFont(nf);
    m_name->setPlaceholderText(tr("Name"));
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (m_loading || t.trimmed().isEmpty())
            return;
        m_obj.name = t.trimmed();
        commit();
    });
    hl->addWidget(m_name);
    m_bodyLayout->addWidget(m_header);
    m_fieldBox = new QWidget(m_body);
    m_bodyLayout->addWidget(m_fieldBox);
    m_backlinkTitle = new QLabel(m_body);
    m_backlinkTitle->setObjectName(QStringLiteral("backlinkTitle"));
    QFont bf = m_backlinkTitle->font();
    bf.setBold(true);
    m_backlinkTitle->setFont(bf);
    m_bodyLayout->addSpacing(8);
    m_bodyLayout->addWidget(m_backlinkTitle);
    m_backlinks = new ChipList(false, m_body);
    m_backlinks->setObjectName(QStringLiteral("backlinks"));
    connect(m_backlinks, &ChipList::activated, this, &ObjectForm::navigate);
    m_bodyLayout->addWidget(m_backlinks);
    m_bodyLayout->addStretch(1);
}

Store *ObjectForm::store() const { return m_project ? m_project->objects() : nullptr; }

void ObjectForm::setProject(Project *p)
{
    if (Store *old = store())
        disconnect(old, nullptr, this, nullptr);
    m_project = p;
    m_id.clear();
    m_builtFor.clear();
    m_stack->setCurrentIndex(0);
    Store *st = store();
    if (!st)
        return;
    connect(st, &Store::objectChanged, this, [this](const QString &id, QObject *source) {
        if (id == m_id && source != this) {
            if (const Object *o = store()->object(id))
                m_obj = *o;
            reloadValues();
        } else {
            refreshChips();
        }
        refreshBacklinks();
    });
    connect(st, &Store::objectAdded, this, [this] { refreshChips(); });
    connect(st, &Store::objectRemoved, this, [this](const QString &id) {
        if (id == m_id)
            showObject(QString());
        else {
            if (const Object *o = store()->object(m_id))
                m_obj = *o;
            refreshChips();
            refreshBacklinks();
        }
    });
    connect(st, &Store::schemaChanged, this, [this] {
        m_builtFor.clear();
        showObject(m_id);
    });
}

void ObjectForm::showObject(const QString &id)
{
    Store *st = store();
    const Object *o = st && !id.isEmpty() ? st->object(id) : nullptr;
    m_id = o ? id : QString();
    if (!o) {
        m_stack->setCurrentIndex(0);
        return;
    }
    const bool other = m_obj.id != id;
    m_obj = *o;
    if (m_builtFor != m_obj.category)
        rebuild();
    reloadValues();
    refreshBacklinks();
    m_stack->setCurrentIndex(1);
    // another object starts at its top
    if (auto *sa = qobject_cast<QScrollArea *>(m_stack->widget(1)); sa && other)
        sa->verticalScrollBar()->setValue(0);
}

void ObjectForm::rebuild()
{
    m_fields.clear();
    delete m_fieldBox->layout();
    for (QWidget *w : m_fieldBox->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        delete w;
    m_builtFor = m_obj.category;
    const Category *c = store() ? store()->category(m_obj.category) : nullptr;
    if (!c)
        return;
    QFormLayout *form = nullptr;
    QVBoxLayout *col = nullptr;
    if (m_wide) {
        form = new QFormLayout(m_fieldBox);
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setContentsMargins(2, 4, 2, 4);
    } else {
        col = new QVBoxLayout(m_fieldBox);
        col->setContentsMargins(2, 4, 2, 4);
        col->setSpacing(2);
    }
    for (const FieldDef &f : c->fields) {
        QWidget *ed = makeEditor(f);
        auto *label = new QLabel(f.label + (f.type == FieldType::YesNo ? QString() : QStringLiteral(":")), m_fieldBox);
        label->setObjectName(QStringLiteral("label:") + f.id);
        if (m_wide) {
            form->addRow(label, ed);
        } else {
            QFont lf = label->font();
            lf.setBold(true);
            label->setFont(lf);
            col->addSpacing(4);
            col->addWidget(label);
            col->addWidget(ed);
        }
    }
}

QWidget *ObjectForm::makeEditor(const FieldDef &f)
{
    const QString fid = f.id;
    QWidget *w = nullptr;
    switch (f.type) {
    case FieldType::Text:
    case FieldType::Date: {
        auto *e = new QLineEdit(m_fieldBox);
        if (f.type == FieldType::Date)
            e->setPlaceholderText(tr("a date or a time, in any form"));
        connect(e, &QLineEdit::textEdited, this, [this, fid](const QString &t) { setValue(fid, t); });
        m_fields.insert(fid, e);
        w = e;
        break;
    }
    case FieldType::Number: {
        auto *box = new QWidget(m_fieldBox);
        auto *l = new QHBoxLayout(box);
        l->setContentsMargins(0, 0, 0, 0);
        auto *e = new QLineEdit(box);
        e->setMaximumWidth(140);
        connect(e, &QLineEdit::textEdited, this, [this, fid](const QString &t) { setValue(fid, t); });
        l->addWidget(e);
        if (!f.unit.isEmpty())
            l->addWidget(new QLabel(f.unit, box));
        l->addStretch(1);
        m_fields.insert(fid, e);
        w = box;
        break;
    }
    case FieldType::LongText: {
        auto *e = new ObjectTextEdit(m_fieldBox);
        e->setProject(m_project);
        connect(e, &ObjectTextEdit::edited, this, [this, fid, e] { setValue(fid, e->markdownText()); });
        connect(e, &ObjectTextEdit::linkActivated, this, &ObjectForm::linkActivated);
        m_fields.insert(fid, e);
        w = e;
        break;
    }
    case FieldType::Choice: {
        auto *e = new QComboBox(m_fieldBox);
        e->setEditable(true);
        e->addItem(QString());
        e->addItems(f.choices);
        e->setInsertPolicy(QComboBox::NoInsert);
        connect(e, &QComboBox::currentTextChanged, this, [this, fid](const QString &t) { setValue(fid, t); });
        m_fields.insert(fid, e);
        w = e;
        break;
    }
    case FieldType::YesNo: {
        auto *e = new QCheckBox(tr("yes"), m_fieldBox);
        connect(e, &QCheckBox::toggled, this, [this, fid](bool on) { setValue(fid, on); });
        m_fields.insert(fid, e);
        w = e;
        break;
    }
    case FieldType::Picture: {
        auto *e = new PictureLabel(m_fieldBox);
        e->onFile = [this, fid](const QString &file) { setPicture(fid, file); };
        e->onRemove = [this, fid] { setValue(fid, QString()); reloadValues(); };
        m_fields.insert(fid, e);
        w = e;
        break;
    }
    case FieldType::Objects:
    case FieldType::Documents: {
        auto *e = new ChipList(true, m_fieldBox);
        const bool docs = f.type == FieldType::Documents;
        const QStringList targets = f.targets;
        connect(e, &ChipList::addRequested, this, [this, fid, docs, targets] {
            const QString id = ObjectChooser::choose(m_project, docs ? ObjectChooser::Documents : ObjectChooser::Objects, targets,
                                                     m_obj.ids(fid) << m_id, this, docs ? tr("Link to Document") : tr("Link to Object"));
            if (!id.isEmpty())
                addLink(fid, id);
        });
        connect(e, &ChipList::activated, this, [this, docs](const QString &id) {
            if (!docs)
                emit navigate(id);
            else if (BinderItem *it = m_project ? m_project->item(id) : nullptr)
                emit openDocument(it);
        });
        connect(e, &ChipList::removeRequested, this, [this, fid](const QString &id) { removeLink(fid, id); });
        m_fields.insert(fid, e);
        w = e;
        break;
    }
    }
    w->setObjectName(QStringLiteral("field:") + fid);
    if (m_fields.value(fid) != w)
        m_fields.value(fid)->setObjectName(QStringLiteral("field:") + fid);
    return w;
}

void ObjectForm::reloadValues()
{
    Store *st = store();
    const Category *c = st ? st->category(m_obj.category) : nullptr;
    if (!c)
        return;
    m_loading = true;
    if (m_name->text() != m_obj.name)
        m_name->setText(m_obj.name);
    const QColor col = st->colorOf(m_obj);
    m_category->setText(c->name + (m_obj.color.isValid() ? tr(" (own color)") : QString()));
    QPalette p = m_header->palette();
    p.setColor(QPalette::Window, col);
    p.setColor(QPalette::WindowText, readableOn(col));
    m_header->setPalette(p);
    m_category->setPalette(p);
    m_colorButton->setPalette(p);
    for (const FieldDef &f : c->fields) {
        QWidget *w = m_fields.value(f.id);
        if (!w)
            continue;
        switch (f.type) {
        case FieldType::Text:
        case FieldType::Date:
        case FieldType::Number:
            if (auto *e = qobject_cast<QLineEdit *>(w); e && e->text() != m_obj.text(f.id))
                e->setText(m_obj.text(f.id));
            break;
        case FieldType::LongText:
            if (auto *e = qobject_cast<ObjectTextEdit *>(w); e && e->markdownText() != m_obj.text(f.id))
                e->setMarkdownText(m_obj.text(f.id));
            break;
        case FieldType::Choice:
            if (auto *e = qobject_cast<QComboBox *>(w); e && e->currentText() != m_obj.text(f.id))
                e->setCurrentText(m_obj.text(f.id));
            break;
        case FieldType::YesNo:
            if (auto *e = qobject_cast<QCheckBox *>(w))
                e->setChecked(m_obj.values.value(f.id).toBool());
            break;
        case FieldType::Picture:
            if (auto *e = static_cast<QLabel *>(w)) {
                const QString name = m_obj.text(f.id);
                QPixmap pm(st->picturePath(name));
                if (pm.isNull()) {
                    e->setPixmap(QPixmap());
                    e->setText(tr("(no picture: click or drop one here)"));
                } else {
                    const int maxW = m_wide ? 380 : 240;
                    e->setPixmap(pm.width() > maxW ? pm.scaledToWidth(maxW, Qt::SmoothTransformation) : pm);
                }
            }
            break;
        case FieldType::Objects:
        case FieldType::Documents:
            break;
        }
    }
    m_loading = false;
    refreshChips();
}

void ObjectForm::refreshChips()
{
    Store *st = store();
    const Category *c = st ? st->category(m_obj.category) : nullptr;
    if (!c || m_id.isEmpty())
        return;
    for (const FieldDef &f : c->fields) {
        auto *chips = qobject_cast<ChipList *>(m_fields.value(f.id));
        if (!chips)
            continue;
        QList<ChipList::Chip> list;
        for (const QString &id : m_obj.ids(f.id)) {
            if (f.type == FieldType::Objects) {
                const Object *o = st->object(id);
                if (!o)
                    continue;
                const Category *oc = st->category(o->category);
                list << ChipList::Chip{id, o->name, st->colorOf(*o), oc ? oc->name : QString()};
            } else {
                BinderItem *it = m_project ? m_project->item(id) : nullptr;
                list << ChipList::Chip{id, it ? it->title : tr("(missing document)"), QColor(), it ? pathOf(it) : QString()};
            }
        }
        chips->setChips(list);
    }
}

void ObjectForm::refreshBacklinks()
{
    Store *st = store();
    if (!st || m_id.isEmpty())
        return;
    QList<ChipList::Chip> list;
    for (const Backlink &b : st->backlinks(m_id))
        if (const Object *o = st->object(b.objectId))
            list << ChipList::Chip{b.objectId, QStringLiteral("%1 – %2").arg(o->name, b.field), st->colorOf(*o), QString()};
    m_backlinks->setChips(list);
    m_backlinkTitle->setText(list.isEmpty() ? tr("Linked from: no other object links here yet") : tr("Linked from"));
}

void ObjectForm::setValue(const QString &fieldId, const QVariant &v)
{
    if (m_loading || m_id.isEmpty())
        return;
    const bool empty = (v.typeId() == QMetaType::QString && v.toString().isEmpty())
                       || (v.typeId() == QMetaType::QStringList && v.toStringList().isEmpty())
                       || (v.typeId() == QMetaType::Bool && !v.toBool());
    if (empty)
        m_obj.values.remove(fieldId);
    else
        m_obj.values.insert(fieldId, v);
    commit();
}

void ObjectForm::commit()
{
    if (Store *st = store(); st && !m_id.isEmpty())
        st->update(m_obj, this);
}

void ObjectForm::addLink(const QString &fieldId, const QString &id)
{
    QStringList l = m_obj.ids(fieldId);
    if (l.contains(id) || id == m_id)
        return;
    l << id;
    setValue(fieldId, l);
    refreshChips();
}

void ObjectForm::removeLink(const QString &fieldId, const QString &id)
{
    QStringList l = m_obj.ids(fieldId);
    l.removeAll(id);
    setValue(fieldId, l);
    refreshChips();
}

void ObjectForm::setPicture(const QString &fieldId, const QString &file)
{
    Store *st = store();
    if (!st || m_id.isEmpty())
        return;
    QString err;
    const QString name = st->importPicture(m_id, fieldId, file, &err);
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Picture"), err);
        return;
    }
    setValue(fieldId, name);
    reloadValues();
}

// ---------------------------------------------------------------- panel

ObjectsPanel::ObjectsPanel(bool wide, QWidget *parent) : QWidget(parent), m_wide(wide)
{
    setObjectName(wide ? QStringLiteral("objectsPanelWide") : QStringLiteral("objectsPanel"));
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(4);
    m_categoryBar = new QWidget(this);
    m_categoryBar->setObjectName(QStringLiteral("objectCategories"));
    m_categoryFlow = new FlowLayout(m_categoryBar, 4, 4);
    l->addWidget(m_categoryBar);

    auto *row = new QHBoxLayout;
    row->setSpacing(2);
    auto tool = [this, row](const QString &name, const QString &icon, const QString &text, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setObjectName(name);
        b->setIcon(icons::action(icon));
        if (b->icon().isNull())
            b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        row->addWidget(b);
        return b;
    };
    m_back = tool(QStringLiteral("objectsBack"), QStringLiteral("go-previous"), QStringLiteral("<"), tr("Back"));
    m_forward = tool(QStringLiteral("objectsForward"), QStringLiteral("go-next"), QStringLiteral(">"), tr("Forward"));
    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("objectsFilter"));
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);
    row->addWidget(m_filter, 1);
    m_add = tool(QStringLiteral("objectsNew"), QStringLiteral("list-add"), QStringLiteral("+"), tr("New object in this category"));
    m_addFolder = tool(QStringLiteral("objectsNewFolder"), QStringLiteral("folder-new"), tr("Folder"), tr("New folder"));
    m_menu = tool(QStringLiteral("objectsMenu"), QStringLiteral("application-menu"), QStringLiteral("≡"), tr("More"));
    m_menu->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_menu);
    menu->addAction(tr("Duplicate"), this, [this] { duplicateCurrent(); });
    menu->addAction(tr("Delete…"), this, [this] { deleteSelected(true); });
    menu->addSeparator();
    menu->addAction(tr("Templates and Colors…"), this, [this] { editTemplates(); });
    m_menu->setMenu(menu);
    if (!wide) {
        m_window = tool(QStringLiteral("objectsOpenWindow"), QStringLiteral("window-new"), tr("Window"), tr("Open in a window of its own"));
        connect(m_window, &QToolButton::clicked, this, [this] { emit openWindowRequested(currentObject()); });
    }
    l->addLayout(row);

    m_docRow = new QWidget(this);
    m_docRow->setObjectName(QStringLiteral("objectsInDocument"));
    auto *dl = new QHBoxLayout(m_docRow);
    dl->setContentsMargins(0, 0, 0, 0);
    auto *dlab = new QLabel(tr("In this document:"), m_docRow);
    dl->addWidget(dlab, 0, Qt::AlignTop);
    m_docChips = new ChipList(false, m_docRow);
    dl->addWidget(m_docChips, 1);
    m_docRow->hide();
    connect(m_docChips, &ChipList::activated, this, [this](const QString &id) { showObject(id); });
    l->addWidget(m_docRow);

    m_split = new QSplitter(wide ? Qt::Horizontal : Qt::Vertical, this);
    auto *tree = new ObjectTree(m_split);
    m_tree = tree;
    m_tree->setObjectName(QStringLiteral("objectsTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->onDrop = [this](QTreeWidgetItem *d, QTreeWidgetItem *t) { moveByDrop(d, t); };
    m_form = new ObjectForm(wide, m_split);
    m_split->addWidget(m_tree);
    m_split->addWidget(m_form);
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 1);
    m_split->setChildrenCollapsible(false);
    l->addWidget(m_split, 1);

    connect(m_back, &QToolButton::clicked, this, &ObjectsPanel::back);
    connect(m_forward, &QToolButton::clicked, this, &ObjectsPanel::forward);
    connect(m_add, &QToolButton::clicked, this, [this] {
        newObject();
        m_form->nameField()->setFocus();
        m_form->nameField()->selectAll();
    });
    connect(m_addFolder, &QToolButton::clicked, this, [this] {
        const QString id = newFolder();
        QTreeWidgetItemIterator it(m_tree);
        for (; *it; ++it)
            if ((*it)->data(0, kIdRole).toString() == id)
                m_tree->editItem(*it);
    });
    connect(m_filter, &QLineEdit::textChanged, this, &ObjectsPanel::rebuildTree);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ObjectsPanel::onTreeSelection);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ObjectsPanel::treeContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int) {
        Store *st = store();
        if (m_updating || !st)
            return;
        const QString id = it->data(0, kIdRole).toString(), name = it->text(0).trimmed();
        if (name.isEmpty())
            return;
        if (it->data(0, kKindRole).toString() == QLatin1String("folder")) {
            st->renameFolder(id, name);
        } else if (const Object *o = st->object(id); o && o->name != name) {
            Object n = *o;
            n.name = name;
            st->update(n, this);
        }
    });
    connect(m_form, &ObjectForm::navigate, this, [this](const QString &id) { showObject(id); });
    connect(m_form, &ObjectForm::openDocument, this, &ObjectsPanel::openDocumentRequested);
    connect(m_form, &ObjectForm::linkActivated, this, [this](const QString &href) {
        const QString id = idFromLink(href);
        if (!id.isEmpty())
            showObject(id);
        else
            emit linkActivated(href);
    });
    updateNav();
    setEnabled(false);
}

Store *ObjectsPanel::store() const { return m_project ? m_project->objects() : nullptr; }

void ObjectsPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_sized)
        return;
    // the list gets a quarter (narrow, above the form) or 28 % (wide, beside it); the form the rest
    m_sized = true;
    QTimer::singleShot(0, this, [this] {
        if (!m_pendingSizes.isEmpty()) {
            m_split->setSizes(m_pendingSizes); // as the writer left them (0.21.1)
            m_pendingSizes.clear();
            return;
        }
        const int total = m_wide ? m_split->width() : m_split->height();
        if (total < 100)
            return;
        const int list = m_wide ? qBound(220, total * 28 / 100, 360) : qBound(110, total / 4, 260);
        m_split->setSizes({list, total - list});
    });
}

void ObjectsPanel::setZoomKeys(const QString &listKey, const QString &sheetKey)
{
    zoom::mark(this, listKey);
    zoom::attachFonts(this, listKey);
    zoom::mark(m_form, sheetKey);
    zoom::attachFonts(m_form, sheetKey);
}

QVariantMap ObjectsPanel::saveState() const
{
    QVariantMap m;
    m[QStringLiteral("category")] = m_category;
    m[QStringLiteral("object")] = currentObject();
    QVariantList sizes;
    for (int v : m_split->sizes())
        sizes << v;
    if (m_sized)
        m[QStringLiteral("split")] = sizes;
    return m;
}

void ObjectsPanel::restoreState(const QVariantMap &m)
{
    if (m.isEmpty() || !m_project)
        return;
    const QString cat = m.value(QStringLiteral("category")).toString();
    const QString obj = m.value(QStringLiteral("object")).toString();
    Store *st = store();
    if (st && !obj.isEmpty() && st->object(obj))
        showObject(obj, true);
    else if (!cat.isEmpty())
        setCategory(cat);
    QList<int> sizes;
    for (const QVariant &v : m.value(QStringLiteral("split")).toList())
        sizes << qMax(0, v.toInt());
    if (sizes.size() == 2 && sizes[0] + sizes[1] > 0) {
        if (m_sized && isVisible())
            m_split->setSizes(sizes);
        else
            m_pendingSizes = sizes;
    }
}

void ObjectsPanel::setProject(Project *p)
{
    if (m_store)
        disconnect(m_store, nullptr, this, nullptr);
    m_project = p;
    m_store = p ? p->objects() : nullptr;
    m_history.clear();
    m_pos = -1;
    m_documentId.clear();
    m_form->setProject(p);
    setEnabled(p != nullptr);
    if (m_store) {
        connectStore();
        if (!m_store->category(m_category))
            m_category = m_store->schema().categories.isEmpty() ? QString() : m_store->schema().categories.first().id;
    }
    rebuildCategories();
    rebuildTree();
    refreshDocumentChips();
    updateNav();
}

void ObjectsPanel::connectStore()
{
    connect(m_store, &Store::schemaChanged, this, [this] {
        if (!m_store->category(m_category))
            m_category = m_store->schema().categories.isEmpty() ? QString() : m_store->schema().categories.first().id;
        rebuildCategories();
        rebuildTree();
    });
    connect(m_store, &Store::objectAdded, this, [this] {
        rebuildCategories();
        rebuildTree();
    });
    connect(m_store, &Store::objectRemoved, this, [this] {
        rebuildCategories();
        rebuildTree();
        refreshDocumentChips();
    });
    connect(m_store, &Store::foldersChanged, this, [this] { rebuildTree(); });
    connect(m_store, &Store::objectChanged, this, [this](const QString &id, QObject *) {
        const Object *o = m_store->object(id);
        if (!o)
            return;
        m_updating = true;
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
            if ((*it)->data(0, kIdRole).toString() == id) {
                (*it)->setText(0, o->name);
                (*it)->setIcon(0, dotIcon(m_store->colorOf(*o)));
            }
        m_updating = false;
        refreshDocumentChips();
    });
}

QToolButton *ObjectsPanel::categoryButton(const QString &id) const
{
    for (QToolButton *b : m_categoryButtons)
        if (b->objectName() == QLatin1String("category:") + id)
            return b;
    return nullptr;
}

void ObjectsPanel::rebuildCategories()
{
    while (QLayoutItem *it = m_categoryFlow->takeAt(0))
        delete it;
    qDeleteAll(m_categoryButtons);
    m_categoryButtons.clear();
    Store *st = store();
    if (!st)
        return;
    for (const Category &c : st->schema().categories) {
        auto *b = new QToolButton(m_categoryBar);
        b->setObjectName(QStringLiteral("category:") + c.id);
        b->setText(c.name);
        b->setCheckable(true);
        b->setChecked(c.id == m_category);
        b->setStyleSheet(chipStyle(c.color, c.id == m_category));
        b->setToolTip(tr("%1: %2").arg(c.name).arg(st->objects(c.id).size()));
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        const QString id = c.id;
        connect(b, &QToolButton::clicked, this, [this, id] { setCategory(id); });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, b, id](const QPoint &p) {
            QMenu m;
            m.addAction(tr("Template and Color of This Category…"), this, [this, id] {
                setCategory(id);
                editTemplates();
            });
            m.exec(b->mapToGlobal(p));
        });
        m_categoryFlow->addWidget(b);
        m_categoryButtons << b;
    }
    m_categoryBar->updateGeometry();
}

void ObjectsPanel::setCategory(const QString &id)
{
    Store *st = store();
    if (!st || !st->category(id))
        return;
    if (m_category != id) {
        m_category = id;
        const Category *c = st->category(id);
        for (QToolButton *b : m_categoryButtons) {
            const bool on = b->objectName() == QLatin1String("category:") + id;
            b->setChecked(on);
            if (const Category *bc = st->category(b->objectName().mid(9)))
                b->setStyleSheet(chipStyle(bc->color, on));
        }
        Q_UNUSED(c);
        rebuildTree();
        if (const Object *o = st->object(m_form->objectId()); !o || o->category != id)
            m_form->showObject(QString());
    }
}

QString ObjectsPanel::currentObject() const { return m_form->objectId(); }

void ObjectsPanel::rebuildTree()
{
    Store *st = store();
    QString selected;
    if (QTreeWidgetItem *cur = m_tree->currentItem())
        selected = cur->data(0, kIdRole).toString();
    if (selected.isEmpty())
        selected = m_form->objectId();
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->isExpanded())
            expanded.insert((*it)->data(0, kIdRole).toString());
    m_updating = true;
    m_tree->clear();
    if (!st || m_category.isEmpty()) {
        m_updating = false;
        return;
    }
    const Category *cat = st->category(m_category);
    const QString q = m_filter->text().trimmed();
    auto matches = [&](const Object *o) {
        if (q.isEmpty() || o->name.contains(q, Qt::CaseInsensitive))
            return true;
        for (auto it = o->values.cbegin(); it != o->values.cend(); ++it)
            if (it->typeId() == QMetaType::QString && it->toString().contains(q, Qt::CaseInsensitive))
                return true;
        return false;
    };
    const QList<Folder> folders = st->folders(m_category);
    const QList<const Object *> objs = st->objects(m_category);
    QHash<QString, QTreeWidgetItem *> folderItems;
    std::function<void(QTreeWidgetItem *, const QString &)> addFolder = [&](QTreeWidgetItem *parent, const QString &parentId) {
        for (const Folder &f : folders) {
            if (f.parent != parentId)
                continue;
            auto *fi = new QTreeWidgetItem({f.name});
            fi->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
            fi->setData(0, kIdRole, f.id);
            fi->setData(0, kKindRole, QStringLiteral("folder"));
            fi->setFlags(fi->flags() | Qt::ItemIsEditable | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled);
            if (parent)
                parent->addChild(fi);
            else
                m_tree->addTopLevelItem(fi);
            folderItems.insert(f.id, fi);
            addFolder(fi, f.id);
        }
    };
    addFolder(nullptr, QString());
    for (const Object *o : objs) {
        if (!matches(o))
            continue;
        auto *oi = new QTreeWidgetItem({o->name});
        oi->setIcon(0, dotIcon(st->colorOf(*o)));
        oi->setData(0, kIdRole, o->id);
        oi->setData(0, kKindRole, QStringLiteral("object"));
        oi->setFlags((oi->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
        if (QTreeWidgetItem *fi = folderItems.value(o->folder))
            fi->addChild(oi);
        else
            m_tree->addTopLevelItem(oi);
    }
    // with a filter: folders without matches go, the rest is open
    if (!q.isEmpty()) {
        std::function<bool(QTreeWidgetItem *)> prune = [&](QTreeWidgetItem *it) -> bool {
            bool any = it->data(0, kKindRole).toString() == QLatin1String("object");
            for (int i = it->childCount() - 1; i >= 0; --i)
                if (!prune(it->child(i)))
                    delete it->takeChild(i);
                else
                    any = true;
            it->setExpanded(true);
            return any;
        };
        for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i)
            if (!prune(m_tree->topLevelItem(i)))
                delete m_tree->takeTopLevelItem(i);
    }
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if (expanded.contains((*it)->data(0, kIdRole).toString()))
            (*it)->setExpanded(true);
        if ((*it)->data(0, kIdRole).toString() == selected) {
            m_tree->setCurrentItem(*it);
            for (QTreeWidgetItem *p = (*it)->parent(); p; p = p->parent())
                p->setExpanded(true);
        }
    }
    if (!cat)
        m_tree->clear();
    m_updating = false;
}

void ObjectsPanel::onTreeSelection()
{
    if (m_updating)
        return;
    QTreeWidgetItem *it = m_tree->currentItem();
    if (!it)
        return;
    if (it->data(0, kKindRole).toString() == QLatin1String("object"))
        showObject(it->data(0, kIdRole).toString());
}

void ObjectsPanel::showObject(const QString &id, bool record)
{
    Store *st = store();
    const Object *o = st ? st->object(id) : nullptr;
    if (!o)
        return;
    if (o->category != m_category)
        setCategory(o->category);
    m_updating = true;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, kIdRole).toString() == id) {
            m_tree->setCurrentItem(*it);
            for (QTreeWidgetItem *p = (*it)->parent(); p; p = p->parent())
                p->setExpanded(true);
            m_tree->scrollToItem(*it);
        }
    m_updating = false;
    m_form->showObject(id);
    if (record && (m_pos < 0 || m_history.value(m_pos) != id)) {
        while (m_history.size() > m_pos + 1)
            m_history.removeLast();
        m_history << id;
        if (m_history.size() > 100)
            m_history.removeFirst();
        m_pos = int(m_history.size()) - 1;
    }
    updateNav();
}

void ObjectsPanel::back()
{
    Store *st = store();
    while (st && m_pos > 0) {
        --m_pos;
        if (st->object(m_history.value(m_pos))) {
            showObject(m_history.value(m_pos), false);
            break;
        }
    }
    updateNav();
}

void ObjectsPanel::forward()
{
    Store *st = store();
    while (st && m_pos + 1 < m_history.size()) {
        ++m_pos;
        if (st->object(m_history.value(m_pos))) {
            showObject(m_history.value(m_pos), false);
            break;
        }
    }
    updateNav();
}

void ObjectsPanel::updateNav()
{
    m_back->setEnabled(canGoBack());
    m_forward->setEnabled(canGoForward());
}

static QString folderOf(QTreeWidgetItem *it, objects::Store *st)
{
    if (!it)
        return QString();
    if (it->data(0, kKindRole).toString() == QLatin1String("folder"))
        return it->data(0, kIdRole).toString();
    const Object *o = st ? st->object(it->data(0, kIdRole).toString()) : nullptr;
    return o ? o->folder : QString();
}

QString ObjectsPanel::newObject(const QString &name)
{
    Store *st = store();
    if (!st || m_category.isEmpty())
        return QString();
    const QString id = st->create(m_category, folderOf(m_tree->currentItem(), st), name);
    showObject(id);
    return id;
}

QString ObjectsPanel::newFolder(const QString &name)
{
    Store *st = store();
    if (!st || m_category.isEmpty())
        return QString();
    return st->addFolder(m_category, folderOf(m_tree->currentItem(), st), name);
}

void ObjectsPanel::deleteSelected(bool ask)
{
    Store *st = store();
    QTreeWidgetItem *it = m_tree->currentItem();
    if (!st || !it)
        return;
    const QString id = it->data(0, kIdRole).toString();
    const bool folder = it->data(0, kKindRole).toString() == QLatin1String("folder");
    if (ask) {
        const QString q = folder ? tr("Delete the folder “%1”? Its objects stay; they move one level up.").arg(it->text(0))
                                 : tr("Delete “%1”? Links to it in other objects are removed as well.").arg(it->text(0));
        if (QMessageBox::question(this, tr("Delete"), q) != QMessageBox::Yes)
            return;
    }
    if (folder)
        st->removeFolder(id);
    else
        st->remove(id);
}

QString ObjectsPanel::duplicateCurrent()
{
    Store *st = store();
    if (!st || m_form->objectId().isEmpty())
        return QString();
    const QString id = st->duplicate(m_form->objectId());
    showObject(id);
    return id;
}

void ObjectsPanel::treeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *it = m_tree->itemAt(pos);
    if (it)
        m_tree->setCurrentItem(it);
    QMenu m;
    m.addAction(tr("New Object"), this, [this] { newObject(); });
    m.addAction(tr("New Folder"), this, [this] { newFolder(); });
    if (it) {
        m.addSeparator();
        m.addAction(tr("Rename"), this, [this, it] { m_tree->editItem(it); });
        if (it->data(0, kKindRole).toString() == QLatin1String("object")) {
            m.addAction(tr("Duplicate"), this, [this] { duplicateCurrent(); });
            if (!m_wide)
                m.addAction(tr("Open in Window"), this, [this] { emit openWindowRequested(currentObject()); });
        }
        m.addAction(tr("Delete…"), this, [this] { deleteSelected(true); });
    }
    m.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ObjectsPanel::moveByDrop(QTreeWidgetItem *dragged, QTreeWidgetItem *target)
{
    Store *st = store();
    if (!st || !dragged)
        return;
    QString folder;
    if (target)
        folder = target->data(0, kKindRole).toString() == QLatin1String("folder") ? target->data(0, kIdRole).toString()
                                                                                    : folderOf(target, st);
    const QString id = dragged->data(0, kIdRole).toString();
    if (dragged->data(0, kKindRole).toString() == QLatin1String("folder"))
        QTimer::singleShot(0, this, [st, id, folder] { st->moveFolder(id, folder); });
    else
        QTimer::singleShot(0, this, [st, id, folder] { st->moveObject(id, folder); });
}

void ObjectsPanel::setDocument(BinderItem *item)
{
    m_documentId = item ? item->id : QString();
    refreshDocumentChips();
}

void ObjectsPanel::refreshDocumentChips()
{
    Store *st = store();
    QList<ChipList::Chip> chips;
    if (st && !m_documentId.isEmpty())
        for (const Object *o : st->objectsForDocument(m_documentId)) {
            const Category *c = st->category(o->category);
            chips << ChipList::Chip{o->id, o->name, st->colorOf(*o), c ? c->name : QString()};
        }
    m_docChips->setChips(chips);
    m_docRow->setVisible(!chips.isEmpty());
}

void ObjectsPanel::editTemplates()
{
    if (m_project)
        ObjectTemplatesDialog::run(m_project, m_category, this);
}

// ---------------------------------------------------------------- window

static QHash<Project *, QPointer<ObjectsWindow>> &windows()
{
    static QHash<Project *, QPointer<ObjectsWindow>> w;
    return w;
}

ObjectsWindow::ObjectsWindow(Project *project, QWidget *parent) : QMainWindow(parent, Qt::Window), m_project(project)
{
    setObjectName(QStringLiteral("objectsWindow"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Objects — %1").arg(project->title()));
    m_panel = new ObjectsPanel(true, this);
    setCentralWidget(m_panel);
    // Ctrl + mouse wheel (0.21); since 0.21.1 the sheet has a zoom of its own
    m_panel->setZoomKeys(QLatin1String(zoom::kObjects), QLatin1String(zoom::kObjectsSheet));
    m_panel->setProject(project);
    resize(1100, 720);
}

ObjectsWindow *ObjectsWindow::existing(Project *project) { return windows().value(project); }

ObjectsWindow *ObjectsWindow::open(Project *project, const QString &objectId, QWidget *parent)
{
    if (!project)
        return nullptr;
    ObjectsWindow *w = existing(project);
    if (!w) {
        w = new ObjectsWindow(project, parent);
        windows().insert(project, w);
        QObject::connect(project, &QObject::destroyed, [project] { windows().remove(project); });
    }
    w->show();
    w->raise();
    w->activateWindow();
    if (!objectId.isEmpty())
        w->m_panel->showObject(objectId);
    return w;
}

void ObjectsWindow::closeFor(Project *project)
{
    if (ObjectsWindow *w = existing(project))
        w->close();
    windows().remove(project);
}

// ---------------------------------------------------------------- templates and colors

ObjectTemplatesDialog::ObjectTemplatesDialog(Project *project, const QString &category, QWidget *parent)
    : QDialog(parent), m_project(project)
{
    setObjectName(QStringLiteral("objectTemplatesDialog"));
    setWindowTitle(tr("Object Templates and Colors"));
    m_schema = project->objects()->schema();
    auto *outer = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Each category of objects has a color and a template: the fields its objects have. Changes "
                                "apply to this project; “Use for New Projects” keeps them for projects made later."),
                             this);
    intro->setWordWrap(true);
    outer->addWidget(intro);
    auto *main = new QHBoxLayout;
    outer->addLayout(main, 1);

    // categories
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Categories"), this));
    m_cats = new QListWidget(this);
    m_cats->setObjectName(QStringLiteral("templateCategories"));
    left->addWidget(m_cats, 1);
    auto *cb = new QHBoxLayout;
    auto button = [this](QBoxLayout *l, const QString &name, const QString &text) {
        auto *b = new QPushButton(text, this);
        b->setObjectName(name);
        l->addWidget(b);
        return b;
    };
    QPushButton *addCat = button(cb, QStringLiteral("addCategory"), tr("Add"));
    QPushButton *rmCat = button(cb, QStringLiteral("removeCategory"), tr("Remove"));
    QPushButton *upCat = button(cb, QStringLiteral("categoryUp"), QStringLiteral("↑"));
    QPushButton *downCat = button(cb, QStringLiteral("categoryDown"), QStringLiteral("↓"));
    left->addLayout(cb);
    main->addLayout(left, 2);

    // the category: name, color, fields
    auto *right = new QVBoxLayout;
    auto *catForm = new QFormLayout;
    m_catName = new QLineEdit(this);
    m_catName->setObjectName(QStringLiteral("categoryName"));
    catForm->addRow(tr("Name:"), m_catName);
    m_catColor = new QToolButton(this);
    m_catColor->setObjectName(QStringLiteral("categoryColor"));
    m_catColor->setText(tr("Choose…"));
    catForm->addRow(tr("Color:"), m_catColor);
    right->addLayout(catForm);
    right->addWidget(new QLabel(tr("Fields of the template"), this));
    auto *fl = new QHBoxLayout;
    m_fieldList = new QListWidget(this);
    m_fieldList->setObjectName(QStringLiteral("templateFields"));
    fl->addWidget(m_fieldList, 1);
    auto *fb = new QVBoxLayout;
    QPushButton *addField = button(fb, QStringLiteral("addField"), tr("Add Field"));
    QPushButton *rmField = button(fb, QStringLiteral("removeField"), tr("Remove"));
    QPushButton *upField = button(fb, QStringLiteral("fieldUp"), tr("Up"));
    QPushButton *downField = button(fb, QStringLiteral("fieldDown"), tr("Down"));
    fb->addStretch(1);
    fl->addLayout(fb);
    right->addLayout(fl, 1);
    m_fieldBox = new QWidget(this);
    auto *ff = new QFormLayout(m_fieldBox);
    ff->setContentsMargins(0, 0, 0, 0);
    m_label = new QLineEdit(m_fieldBox);
    m_label->setObjectName(QStringLiteral("fieldLabel"));
    ff->addRow(tr("Field name:"), m_label);
    m_type = new QComboBox(m_fieldBox);
    m_type->setObjectName(QStringLiteral("fieldType"));
    for (FieldType t : allTypes())
        m_type->addItem(typeLabel(t), int(t));
    ff->addRow(tr("Kind:"), m_type);
    m_unit = new QLineEdit(m_fieldBox);
    m_unit->setObjectName(QStringLiteral("fieldUnit"));
    m_unit->setPlaceholderText(tr("cm, kg, years, °C ..."));
    m_unitLabel = new QLabel(tr("Unit:"), m_fieldBox);
    ff->addRow(m_unitLabel, m_unit);
    m_choices = new QPlainTextEdit(m_fieldBox);
    m_choices->setObjectName(QStringLiteral("fieldChoices"));
    m_choices->setPlaceholderText(tr("one choice per line"));
    m_choices->setMaximumHeight(110);
    m_choicesLabel = new QLabel(tr("Choices:"), m_fieldBox);
    ff->addRow(m_choicesLabel, m_choices);
    m_targets = new QListWidget(m_fieldBox);
    m_targets->setObjectName(QStringLiteral("fieldTargets"));
    m_targets->setMaximumHeight(120);
    m_targetsLabel = new QLabel(tr("Can link to:"), m_fieldBox);
    m_targetsLabel->setToolTip(tr("Nothing ticked: objects of every category"));
    ff->addRow(m_targetsLabel, m_targets);
    right->addWidget(m_fieldBox);
    main->addLayout(right, 5);

    auto *bottom = new QHBoxLayout;
    QPushButton *forNew = button(bottom, QStringLiteral("useForNewProjects"), tr("Use for New Projects"));
    auto *standard = new QToolButton(this);
    standard->setObjectName(QStringLiteral("standardTemplates"));
    standard->setText(tr("Standard Templates"));
    standard->setPopupMode(QToolButton::InstantPopup);
    auto *sm = new QMenu(standard);
    sm->addAction(tr("Reset This Category to the Standard"), this, [this] {
        const Schema std = builtInSchema();
        Category *c = current();
        const Category *s = c ? std.category(c->id) : nullptr;
        if (!s) {
            QMessageBox::information(this, windowTitle(), tr("This category is one of your own; it has no standard template."));
            return;
        }
        *c = *s;
        loadCategory();
    });
    sm->addAction(tr("Add the Missing Standard Categories"), this, [this] {
        for (const Category &s : builtInSchema().categories)
            if (!m_schema.category(s.id)) {
                m_schema.categories << s;
                m_removed.removeAll(s.id);
            }
        const int row = m_cats->currentRow();
        m_loading = true;
        m_cats->clear();
        for (const Category &c : m_schema.categories)
            m_cats->addItem(new QListWidgetItem(dotIcon(c.color), c.name));
        m_loading = false;
        m_cats->setCurrentRow(qMax(0, row));
    });
    standard->setMenu(sm);
    bottom->addWidget(standard);
    bottom->addStretch(1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bottom->addWidget(bb);
    outer->addLayout(bottom);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(forNew, &QPushButton::clicked, this, [this] {
        QString err;
        if (saveSchemaForNewProjects(m_schema, &err))
            QMessageBox::information(this, windowTitle(), tr("New projects get these categories and templates from now on."));
        else
            QMessageBox::warning(this, windowTitle(), err);
    });

    // edits go straight into the schema of the dialog
    connect(m_cats, &QListWidget::currentRowChanged, this, [this] { loadCategory(); });
    connect(addCat, &QPushButton::clicked, this, [this] { addCategory(tr("New Category")); });
    connect(rmCat, &QPushButton::clicked, this, [this] { removeCurrentCategory(); });
    auto moveCat = [this](int d) {
        const int r = m_cats->currentRow(), to = r + d;
        if (r < 0 || to < 0 || to >= m_schema.categories.size())
            return;
        m_schema.categories.move(r, to);
        m_loading = true;
        m_cats->insertItem(to, m_cats->takeItem(r));
        m_loading = false;
        m_cats->setCurrentRow(to);
    };
    connect(upCat, &QPushButton::clicked, this, [moveCat] { moveCat(-1); });
    connect(downCat, &QPushButton::clicked, this, [moveCat] { moveCat(1); });
    connect(m_catName, &QLineEdit::textEdited, this, [this](const QString &t) {
        if (Category *c = current(); c && !t.trimmed().isEmpty()) {
            c->name = t.trimmed();
            m_cats->currentItem()->setText(c->name);
        }
    });
    connect(m_catColor, &QToolButton::clicked, this, [this] {
        Category *c = current();
        if (!c)
            return;
        const QColor col = QColorDialog::getColor(c->color, this, tr("Color of %1").arg(c->name));
        if (col.isValid())
            setCategoryColor(col);
    });
    connect(m_fieldList, &QListWidget::currentRowChanged, this, [this] { loadField(); });
    connect(addField, &QPushButton::clicked, this, [this] { ObjectTemplatesDialog::addField(tr("New Field"), FieldType::Text); });
    connect(rmField, &QPushButton::clicked, this, [this] { removeCurrentField(); });
    connect(upField, &QPushButton::clicked, this, [this] { moveField(-1); });
    connect(downField, &QPushButton::clicked, this, [this] { moveField(1); });
    connect(m_label, &QLineEdit::textEdited, this, [this] { storeField(); });
    connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        storeField();
        loadField();
    });
    connect(m_unit, &QLineEdit::textEdited, this, [this] { storeField(); });
    connect(m_choices, &QPlainTextEdit::textChanged, this, [this] { storeField(); });
    connect(m_targets, &QListWidget::itemChanged, this, [this] { storeField(); });

    for (const Category &c : m_schema.categories)
        m_cats->addItem(new QListWidgetItem(dotIcon(c.color), c.name));
    int row = 0;
    for (int i = 0; i < m_schema.categories.size(); ++i)
        if (m_schema.categories.at(i).id == category)
            row = i;
    m_cats->setCurrentRow(m_schema.categories.isEmpty() ? -1 : row);
    resize(900, 640);
}

Category *ObjectTemplatesDialog::current()
{
    const int r = m_cats->currentRow();
    return r >= 0 && r < m_schema.categories.size() ? &m_schema.categories[r] : nullptr;
}

void ObjectTemplatesDialog::loadCategory()
{
    if (m_loading)
        return;
    m_loading = true;
    Category *c = current();
    m_catName->setText(c ? c->name : QString());
    m_catName->setEnabled(c);
    m_catColor->setEnabled(c);
    m_catColor->setStyleSheet(c ? chipStyle(c->color) : QString());
    m_fieldList->clear();
    if (c)
        for (const FieldDef &f : c->fields)
            m_fieldList->addItem(QStringLiteral("%1   (%2)").arg(f.label, typeLabel(f.type)));
    m_loading = false;
    m_fieldList->setCurrentRow(c && !c->fields.isEmpty() ? 0 : -1);
    loadField();
}

void ObjectTemplatesDialog::loadField()
{
    if (m_loading)
        return;
    m_loading = true;
    Category *c = current();
    const int r = m_fieldList->currentRow();
    const FieldDef *f = c && r >= 0 && r < c->fields.size() ? &c->fields.at(r) : nullptr;
    m_fieldBox->setEnabled(f);
    m_label->setText(f ? f->label : QString());
    m_type->setCurrentIndex(qMax(0, m_type->findData(f ? int(f->type) : 0)));
    m_unit->setText(f ? f->unit : QString());
    m_choices->setPlainText(f ? f->choices.join(QLatin1Char('\n')) : QString());
    m_targets->clear();
    for (const Category &cat : m_schema.categories) {
        auto *it = new QListWidgetItem(dotIcon(cat.color), cat.name, m_targets);
        it->setData(Qt::UserRole, cat.id);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(f && f->targets.contains(cat.id) ? Qt::Checked : Qt::Unchecked);
    }
    const FieldType t = f ? f->type : FieldType::Text;
    m_unit->setVisible(t == FieldType::Number);
    m_unitLabel->setVisible(t == FieldType::Number);
    m_choices->setVisible(t == FieldType::Choice);
    m_choicesLabel->setVisible(t == FieldType::Choice);
    m_targets->setVisible(t == FieldType::Objects);
    m_targetsLabel->setVisible(t == FieldType::Objects);
    m_loading = false;
}

void ObjectTemplatesDialog::storeField()
{
    if (m_loading)
        return;
    Category *c = current();
    const int r = m_fieldList->currentRow();
    if (!c || r < 0 || r >= c->fields.size())
        return;
    FieldDef &f = c->fields[r];
    if (!m_label->text().trimmed().isEmpty())
        f.label = m_label->text().trimmed();
    f.type = FieldType(m_type->currentData().toInt());
    f.unit = m_unit->text().trimmed();
    f.choices.clear();
    for (const QString &line : m_choices->toPlainText().split(QLatin1Char('\n')))
        if (!line.trimmed().isEmpty())
            f.choices << line.trimmed();
    f.targets.clear();
    for (int i = 0; i < m_targets->count(); ++i)
        if (m_targets->item(i)->checkState() == Qt::Checked)
            f.targets << m_targets->item(i)->data(Qt::UserRole).toString();
    m_fieldList->item(r)->setText(QStringLiteral("%1   (%2)").arg(f.label, typeLabel(f.type)));
}

void ObjectTemplatesDialog::storeCategory() {}

void ObjectTemplatesDialog::setCategoryColor(const QColor &col)
{
    Category *c = current();
    if (!c || !col.isValid())
        return;
    c->color = col;
    m_cats->currentItem()->setIcon(dotIcon(col));
    m_catColor->setStyleSheet(chipStyle(col));
}

void ObjectTemplatesDialog::addCategory(const QString &name)
{
    static const QList<QColor> palette = {QColor(QStringLiteral("#c0392b")), QColor(QStringLiteral("#16a085")),
                                          QColor(QStringLiteral("#8e6c3a")), QColor(QStringLiteral("#d4ac0d")),
                                          QColor(QStringLiteral("#7f8c8d")), QColor(QStringLiteral("#2c3e94"))};
    Category c;
    c.id = QStringLiteral("cat-") + objects::newId().left(8);
    c.name = name;
    c.color = palette.value(int(m_schema.categories.size()) % palette.size());
    FieldDef d;
    d.id = QStringLiteral("description");
    d.label = tr("Description");
    d.type = FieldType::LongText;
    FieldDef o = d;
    o.id = QStringLiteral("other");
    o.label = tr("Other");
    c.fields = {d, o};
    m_schema.categories << c;
    m_cats->addItem(new QListWidgetItem(dotIcon(c.color), c.name));
    m_cats->setCurrentRow(m_cats->count() - 1);
    m_catName->setFocus();
    m_catName->selectAll();
}

void ObjectTemplatesDialog::removeCurrentCategory()
{
    const int r = m_cats->currentRow();
    if (r < 0 || m_schema.categories.size() <= 1)
        return; // one category stays
    m_removed << m_schema.categories.at(r).id;
    m_schema.categories.removeAt(r);
    for (Category &c : m_schema.categories)
        for (FieldDef &f : c.fields)
            f.targets.removeAll(m_removed.last());
    m_loading = true;
    delete m_cats->takeItem(r);
    m_loading = false;
    m_cats->setCurrentRow(qMin(r, m_cats->count() - 1));
    loadCategory();
}

void ObjectTemplatesDialog::addField(const QString &label, FieldType type)
{
    Category *c = current();
    if (!c)
        return;
    FieldDef f;
    f.id = QStringLiteral("f-") + objects::newId().left(8);
    f.label = label;
    f.type = type;
    c->fields << f;
    m_fieldList->addItem(QStringLiteral("%1   (%2)").arg(f.label, typeLabel(f.type)));
    m_fieldList->setCurrentRow(m_fieldList->count() - 1);
    m_label->setFocus();
    m_label->selectAll();
}

void ObjectTemplatesDialog::removeCurrentField()
{
    Category *c = current();
    const int r = m_fieldList->currentRow();
    if (!c || r < 0 || r >= c->fields.size())
        return;
    c->fields.removeAt(r);
    m_loading = true;
    delete m_fieldList->takeItem(r);
    m_loading = false;
    m_fieldList->setCurrentRow(qMin(r, m_fieldList->count() - 1));
    loadField();
}

void ObjectTemplatesDialog::moveField(int delta)
{
    Category *c = current();
    const int r = m_fieldList->currentRow(), to = r + delta;
    if (!c || r < 0 || to < 0 || to >= c->fields.size())
        return;
    c->fields.move(r, to);
    m_loading = true;
    m_fieldList->insertItem(to, m_fieldList->takeItem(r));
    m_loading = false;
    m_fieldList->setCurrentRow(to);
}

bool ObjectTemplatesDialog::run(Project *project, const QString &category, QWidget *parent)
{
    if (!project)
        return false;
    ObjectTemplatesDialog dlg(project, category, parent);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    Store *st = project->objects();
    // objects of removed categories go only after a question
    QStringList doomed;
    for (const Object *o : st->objects())
        if (dlg.removedCategories().contains(o->category))
            doomed << o->id;
    if (!doomed.isEmpty()
        && QMessageBox::question(parent, dlg.windowTitle(),
                                 tr("%n object(s) belong to the categories you removed. Delete them as well? (No: nothing is changed.)",
                                    nullptr, int(doomed.size())))
               != QMessageBox::Yes)
        return false;
    for (const QString &id : doomed)
        st->remove(id);
    st->setSchema(dlg.schema());
    return true;
}

} // namespace qb
