#include "ui/MindMapUi.h"
#include "ui/ColorPalette.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "ui/MindMapController.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBox>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>

namespace qb {

using namespace mindmap;

// ------------------------------------------------------------------ color well

ColorWell::ColorWell(const QString &noColorText, QWidget *parent) : QToolButton(parent)
{
    setPopupMode(QToolButton::MenuButtonPopup);
    auto *menu = new QMenu(this);
    menu->addAction(noColorText, this, [this] {
        setColor(QColor());
        emit colorChosen(QColor());
    });
    setMenu(menu);
    // a click shows the shared palette (label colors, six colors, Other...; 0.21.1, before
    // the system's color window)
    connect(this, &QToolButton::clicked, this, [this, noColorText] {
        QMenu m;
        m.setObjectName(QStringLiteral("colorWellPalette"));
        palette::fillMenu(&m, nullptr, m_color, [this](const QColor &c) {
            setColor(c);
            emit colorChosen(c);
        }, this, noColorText);
        m.exec(mapToGlobal(QPoint(0, height())));
    });
    setIconSize(QSize(28, 14));
    refresh();
}

void ColorWell::setColor(const QColor &c)
{
    m_color = c;
    refresh();
}

void ColorWell::refresh()
{
    QPixmap pm(28, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(90, 90, 90));
    p.setBrush(m_color.isValid() ? QBrush(m_color) : QBrush(Qt::white));
    p.drawRect(QRectF(0.5, 0.5, 27, 13));
    if (!m_color.isValid()) {
        p.setPen(QPen(QColor(210, 40, 40), 1.4));
        p.drawLine(QPointF(2, 12), QPointF(26, 2));
    }
    p.end();
    setIcon(QIcon(pm));
}

// ------------------------------------------------------------------ find bar

MindMapFindBar::MindMapFindBar(MindMapController *ctl, QWidget *parent) : QWidget(parent), m_ctl(ctl)
{
    setObjectName(QStringLiteral("mindMapFindBar"));
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(6, 4, 6, 4);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);
    m_find = new QLineEdit(this);
    m_find->setObjectName(QStringLiteral("mmFindField"));
    m_find->setPlaceholderText(tr("Find in the notes"));
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit(this);
    m_replace->setObjectName(QStringLiteral("mmReplaceField"));
    m_replace->setPlaceholderText(tr("Replace with"));
    auto btn = [this](const QString &text, const QString &tip = QString()) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton *prev = btn(tr("Previous"), tr("Shift+F3"));
    QToolButton *next = btn(tr("Next"), tr("F3"));
    QToolButton *rep = btn(tr("Replace"));
    QToolButton *repFind = btn(tr("Replace && Find"));
    QToolButton *repAll = btn(tr("Replace All"));
    repAll->setObjectName(QStringLiteral("mmReplaceAll"));
    QToolButton *opts = btn(tr("Options"));
    opts->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(opts);
    const QStringList modes = {tr("Contains"), tr("Starts With"), tr("Whole Word"), tr("Ends With")};
    auto *modeGroup = new QActionGroup(menu);
    for (int i = 0; i < modes.size(); ++i) {
        QAction *a = menu->addAction(modes[i], this, [this, i] { m_mode = i; });
        a->setCheckable(true);
        a->setChecked(i == 0);
        modeGroup->addAction(a);
    }
    menu->addSeparator();
    QAction *ic = menu->addAction(tr("Ignore Case"), this, [this](bool b) { m_ignoreCase = b; });
    ic->setCheckable(true);
    ic->setChecked(true);
    menu->addSeparator();
    QAction *selOnly = menu->addAction(tr("Replace All in Selected Notes Only"), this, [this](bool b) { m_selectedOnly = b; });
    selOnly->setCheckable(true);
    opts->setMenu(menu);
    QToolButton *done = btn(tr("Done"));
    m_status = new QLabel(this);
    grid->addWidget(m_find, 0, 0);
    grid->addWidget(prev, 0, 1);
    grid->addWidget(next, 0, 2);
    grid->addWidget(opts, 0, 3);
    grid->addWidget(done, 0, 4);
    grid->addWidget(m_replace, 1, 0);
    grid->addWidget(rep, 1, 1);
    grid->addWidget(repFind, 1, 2);
    grid->addWidget(repAll, 1, 3);
    grid->addWidget(m_status, 1, 4);
    grid->setColumnStretch(0, 1);
    connect(next, &QToolButton::clicked, this, [this] { findNext(false); });
    connect(prev, &QToolButton::clicked, this, [this] { findNext(true); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] {
        findNext(false);
        // Enter: done searching, the found text is selected for editing
        if (m_ctl->scene()->editingNote())
            m_ctl->view()->setFocus();
    });
    connect(rep, &QToolButton::clicked, this, [this] { m_ctl->replaceCurrent(m_replace->text()); });
    connect(repFind, &QToolButton::clicked, this, [this] {
        m_ctl->replaceCurrent(m_replace->text());
        findNext(false);
    });
    connect(repAll, &QToolButton::clicked, this, [this] {
        const int n = m_ctl->replaceAll(m_find->text(), m_replace->text(), m_mode, m_ignoreCase, m_selectedOnly);
        m_status->setText(tr("%n replaced", nullptr, n));
    });
    connect(done, &QToolButton::clicked, this, [this] {
        hide();
        m_ctl->view()->setFocus();
    });
    hide();
}

void MindMapFindBar::open()
{
    show();
    m_find->setFocus();
    m_find->selectAll();
}

void MindMapFindBar::findNext(bool backward)
{
    if (m_find->text().isEmpty()) {
        open();
        return;
    }
    const bool ok = m_ctl->find(m_find->text(), backward, m_mode, m_ignoreCase);
    m_status->setText(ok ? QString() : tr("Not found"));
}

void MindMapFindBar::setFindText(const QString &t) { m_find->setText(t); }

// ------------------------------------------------------------------ options

static QComboBox *borderCombo(QWidget *parent, int value)
{
    auto *c = new QComboBox(parent);
    c->addItem(QObject::tr("No Border"), 0);
    for (int w = 1; w <= 5; ++w)
        c->addItem(QObject::tr("%1 px").arg(w), w);
    c->setCurrentIndex(qBound(0, value, 5));
    return c;
}

MindMapOptionsDialog::MindMapOptionsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Mind Map Options"));
    const MindMapPrefs &p = MindMapPrefs::instance();
    auto *lay = new QVBoxLayout(this);
    auto *general = new QGroupBox(tr("General"), this);
    auto *gl = new QVBoxLayout(general);
    auto check = [&](QCheckBox *&b, const QString &text, bool on, QLayout *l) {
        b = new QCheckBox(text, this);
        b->setChecked(on);
        l->addWidget(b);
    };
    check(m_enterEnds, tr("“Enter” ends editing (Shift+Enter: a new line)"), p.enterEndsEditing, gl);
    check(m_enterCreates, tr("“Enter” creates new notes (when no note is being edited)"), p.enterCreatesNotes, gl);
    check(m_useSelected, tr("Use selected note style for new notes"), p.useSelectedStyle, gl);
    check(m_dashed, tr("Use dashes for lines without arrows"), p.dashedLines, gl);
    check(m_autoSize, tr("Auto-size notes (short notes become as narrow as their text)"), p.autoSize, gl);
    auto *fr = new QHBoxLayout;
    fr->addWidget(new QLabel(tr("Faded transparency:"), this));
    m_faded = new QSlider(Qt::Horizontal, this);
    m_faded->setRange(10, 95);
    m_faded->setValue(p.fadedPercent);
    fr->addWidget(m_faded);
    gl->addLayout(fr);
    lay->addWidget(general);

    auto *notes = new QGroupBox(tr("New Notes"), this);
    auto *nf = new QFormLayout(notes);
    m_text = new QLineEdit(p.newNoteText, this);
    m_text->setPlaceholderText(tr("New Note"));
    nf->addRow(tr("Default text:"), m_text);
    m_size = new QDoubleSpinBox(this);
    m_size->setRange(6, 96);
    m_size->setValue(p.newNoteSize);
    m_size->setSuffix(tr(" pt"));
    nf->addRow(tr("Default font size:"), m_size);
    m_width = new QSpinBox(this);
    m_width->setRange(40, 800);
    m_width->setValue(int(p.newNoteWidth));
    m_width->setSuffix(tr(" px"));
    nf->addRow(tr("Default width:"), m_width);
    m_noteFill = new ColorWell(tr("No Fill"), this);
    m_noteFill->setColor(p.newNoteFill);
    nf->addRow(tr("Fill color:"), m_noteFill);
    m_noteBorder = borderCombo(this, p.newNoteBorder);
    nf->addRow(tr("Border:"), m_noteBorder);
    lay->addWidget(notes);

    auto *shapes = new QGroupBox(tr("New Shapes"), this);
    auto *sf = new QFormLayout(shapes);
    m_shapeFill = new ColorWell(tr("No Fill"), this);
    m_shapeFill->setColor(p.newShapeFill);
    sf->addRow(tr("Fill color:"), m_shapeFill);
    m_shapeBorder = borderCombo(this, p.newShapeBorder);
    sf->addRow(tr("Border:"), m_shapeBorder);
    check(m_shapeMagnetic, tr("Magnetic"), p.newShapeMagnetic, sf);
    lay->addWidget(shapes);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

void MindMapOptionsDialog::accept()
{
    MindMapPrefs &p = MindMapPrefs::instance();
    p.enterEndsEditing = m_enterEnds->isChecked();
    p.enterCreatesNotes = m_enterCreates->isChecked();
    p.useSelectedStyle = m_useSelected->isChecked();
    p.dashedLines = m_dashed->isChecked();
    p.autoSize = m_autoSize->isChecked();
    p.fadedPercent = m_faded->value();
    p.newNoteText = m_text->text();
    p.newNoteSize = m_size->value();
    p.newNoteWidth = m_width->value();
    p.newNoteFill = m_noteFill->color();
    p.newNoteBorder = m_noteBorder->currentData().toInt();
    p.newShapeFill = m_shapeFill->color();
    p.newShapeBorder = m_shapeBorder->currentData().toInt();
    p.newShapeMagnetic = m_shapeMagnetic->isChecked();
    p.save();
    QDialog::accept();
}

// ------------------------------------------------------------------ style list

namespace {

QPixmap stylePreview(const Board &b, const NoteStyle &s, int w, int h)
{
    QPixmap pm(w, h);
    pm.fill(b.settings.background);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(4, 3, w - 8, h - 6);
    const QPainterPath body = mindmapdraw::bodyPath(r, s.affectsBody ? s.look.borderStyle : BorderStyle::Rounded);
    if (s.affectsBody && s.look.shadow) {
        p.save();
        p.translate(2, 2);
        p.fillPath(body, QColor(0, 0, 0, 60));
        p.restore();
    }
    if (s.affectsBody && s.look.fill.isValid())
        p.fillPath(body, s.look.fill);
    if (s.affectsBody && s.look.borderWeight > 0) {
        p.setPen(QPen(s.look.borderColor.isValid() ? s.look.borderColor : b.settings.effectiveTextColor(), s.look.borderWeight));
        p.drawPath(body);
    }
    QFont f(b.fontFamily());
    f.setPointSizeF(qMin(13.0, s.affectsFont ? s.fontSize : 11.0));
    f.setBold(s.affectsFont && s.bold);
    f.setItalic(s.affectsFont && s.italic);
    f.setUnderline(s.affectsFont && s.underline);
    f.setStrikeOut(s.affectsFont && s.strike);
    p.setFont(f);
    p.setPen(s.affectsTextColor && s.look.textColor.isValid() ? s.look.textColor : b.settings.effectiveTextColor());
    if (s.affectsFade && s.look.faded)
        p.setOpacity(0.45);
    p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, s.name);
    return pm;
}

class StyleList : public QListWidget
{
public:
    explicit StyleList(QWidget *parent) : QListWidget(parent)
    {
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
    }

protected:
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem *it = currentItem();
        if (!it || it->data(Qt::UserRole).toString().isEmpty())
            return;
        auto *md = new QMimeData;
        md->setData(QStringLiteral("application/x-quillbinder-mindmap-style"), it->data(Qt::UserRole).toString().toUtf8());
        auto *drag = new QDrag(this);
        drag->setMimeData(md);
        drag->setPixmap(it->icon().pixmap(iconSize()));
        drag->exec(Qt::CopyAction);
    }
};

QWidget *section(QWidget *parent, QVBoxLayout *&inner)
{
    auto *w = new QWidget(parent);
    inner = new QVBoxLayout(w);
    inner->setContentsMargins(6, 4, 6, 6);
    inner->setSpacing(4);
    return w;
}

} // namespace

// ------------------------------------------------------------------ format panel

MindMapFormatPanel::MindMapFormatPanel(MindMapController *ctl, QWidget *parent) : QWidget(parent), m_ctl(ctl)
{
    setObjectName(QStringLiteral("mindMapFormatPanel"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *box = new QToolBox(this);
    lay->addWidget(box);

    // ---- Note Format
    QVBoxLayout *nf;
    QWidget *fmt = section(box, nf);
    auto *styleRow = new QHBoxLayout;
    auto tbtn = [&](const QString &key) {
        auto *b = new QToolButton(fmt);
        b->setDefaultAction(ctl->action(key));
        b->setAutoRaise(true);
        styleRow->addWidget(b);
        return b;
    };
    m_b = tbtn(QStringLiteral("mm_bold"));
    m_i = tbtn(QStringLiteral("mm_italic"));
    m_u = tbtn(QStringLiteral("mm_underline"));
    m_s = tbtn(QStringLiteral("mm_strike"));
    styleRow->addStretch(1);
    nf->addWidget(new QLabel(tr("Text Style"), fmt));
    nf->addLayout(styleRow);
    auto *form = new QFormLayout;
    m_size = new QDoubleSpinBox(fmt);
    m_size->setObjectName(QStringLiteral("mmFontSize"));
    m_size->setRange(6, 96);
    m_size->setDecimals(1);
    m_size->setSuffix(tr(" pt"));
    m_size->setKeyboardTracking(false);
    form->addRow(tr("Font size:"), m_size);
    m_textColor = new ColorWell(tr("Board Text Color"), fmt);
    m_textColor->setObjectName(QStringLiteral("mmTextColor"));
    form->addRow(tr("Text color:"), m_textColor);
    m_align = new QComboBox(fmt);
    m_align->addItems({tr("Left"), tr("Center"), tr("Right"), tr("Justified")});
    form->addRow(tr("Alignment:"), m_align);
    m_fill = new ColorWell(tr("No Fill"), fmt);
    m_fill->setObjectName(QStringLiteral("mmFill"));
    form->addRow(tr("Fill color:"), m_fill);
    m_faded = new QCheckBox(tr("Faded"), fmt);
    form->addRow(QString(), m_faded);
    m_borderColor = new ColorWell(tr("Board Text Color"), fmt);
    form->addRow(tr("Border color:"), m_borderColor);
    m_thickness = borderCombo(fmt, 0);
    m_thickness->setObjectName(QStringLiteral("mmThickness"));
    form->addRow(tr("Thickness:"), m_thickness);
    m_borderStyle = new QComboBox(fmt);
    m_borderStyle->setObjectName(QStringLiteral("mmBorderStyle"));
    m_borderStyle->addItems({tr("Rounded"), tr("Square"), tr("Jagged"), tr("Cloud")});
    form->addRow(tr("Style:"), m_borderStyle);
    m_shadow = new QCheckBox(tr("Shadow"), fmt);
    form->addRow(QString(), m_shadow);
    m_magnetic = new QCheckBox(tr("Magnetic (shapes)"), fmt);
    form->addRow(QString(), m_magnetic);
    nf->addLayout(form);
    nf->addStretch(1);
    box->addItem(fmt, tr("Note Format"));

    connect(m_size, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating)
            return;
        m_ctl->forNotes(tr("Font Size"), [v](Note &n) { n.fontSize = v; });
    });
    auto lookChange = [this](const QString &text, std::function<void(Appearance &)> fn) {
        if (m_updating)
            return;
        const QList<int> ids = m_ctl->scene()->selectedIds();
        m_ctl->doc()->change(text, [&] {
            for (int id : ids) {
                if (Note *n = m_ctl->board().note(id))
                    fn(n->look);
                else if (Shape *s = m_ctl->board().shape(id))
                    fn(s->look);
            }
        });
    };
    connect(m_textColor, &ColorWell::colorChosen, this, [lookChange](const QColor &c) { lookChange(tr("Text Color"), [c](Appearance &a) { a.textColor = c; }); });
    connect(m_fill, &ColorWell::colorChosen, this, [lookChange](const QColor &c) { lookChange(tr("Fill Color"), [c](Appearance &a) { a.fill = c; }); });
    connect(m_borderColor, &ColorWell::colorChosen, this, [lookChange](const QColor &c) {
        lookChange(tr("Border Color"), [c](Appearance &a) {
            a.borderColor = c;
            if (c.isValid() && a.borderWeight == 0)
                a.borderWeight = 1;
        });
    });
    connect(m_faded, &QCheckBox::toggled, this, [lookChange](bool b) { lookChange(tr("Fade"), [b](Appearance &a) { a.faded = b; }); });
    connect(m_shadow, &QCheckBox::toggled, this, [lookChange](bool b) { lookChange(tr("Shadow"), [b](Appearance &a) { a.shadow = b; }); });
    connect(m_thickness, qOverload<int>(&QComboBox::activated), this,
            [lookChange](int i) { lookChange(tr("Border"), [i](Appearance &a) { a.borderWeight = i; }); });
    connect(m_borderStyle, qOverload<int>(&QComboBox::activated), this,
            [lookChange](int i) { lookChange(tr("Border Style"), [i](Appearance &a) { a.borderStyle = BorderStyle(i); }); });
    connect(m_align, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        const QList<int> ids = m_ctl->scene()->selectedNotes();
        const Align al = i == 1 ? Align::Center : i == 2 ? Align::Right : i == 3 ? Align::Justify : Align::Left;
        m_ctl->doc()->change(tr("Alignment"), [&] {
            for (int id : ids)
                m_ctl->board().note(id)->align = al;
        });
    });
    connect(m_magnetic, &QCheckBox::toggled, this, [this](bool b) {
        if (m_updating)
            return;
        const QList<int> ids = m_ctl->scene()->selectedShapes();
        m_ctl->doc()->change(tr("Magnetic"), [&] {
            for (int id : ids)
                m_ctl->board().shape(id)->magnetic = b;
        });
    });

    // ---- Note Styles
    QVBoxLayout *sl;
    QWidget *styles = section(box, sl);
    m_styles = new StyleList(styles);
    m_styles->setObjectName(QStringLiteral("mmStyleList"));
    m_styles->setIconSize(QSize(180, 30));
    m_styles->setSpacing(2);
    sl->addWidget(new QLabel(tr("Click: apply to the selection. Drag onto the board: a new note in that style."), styles));
    sl->itemAt(0)->widget()->setProperty("wordWrap", true);
    sl->addWidget(m_styles, 1);
    auto *srow = new QHBoxLayout;
    auto *newStyle = new QToolButton(styles);
    newStyle->setDefaultAction(ctl->action(QStringLiteral("mm_newStyle")));
    newStyle->setText(tr("New…"));
    auto *redef = new QToolButton(styles);
    redef->setText(tr("Redefine…"));
    auto *del = new QToolButton(styles);
    del->setText(tr("Delete"));
    srow->addWidget(newStyle);
    srow->addWidget(redef);
    srow->addWidget(del);
    srow->addStretch(1);
    sl->addLayout(srow);
    box->addItem(styles, tr("Note Styles"));
    connect(m_styles, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString id = it->data(Qt::UserRole).toString();
        if (id.isEmpty()) {
            m_ctl->applyStyleByIndex(0);
            return;
        }
        for (const NoteStyle &s : m_ctl->board().styles)
            if (s.id == id)
                m_ctl->applyStyle(s, m_ctl->scene()->selectedIds());
    });
    connect(redef, &QToolButton::clicked, this, [this] {
        if (QListWidgetItem *it = m_styles->currentItem(); it && !it->data(Qt::UserRole).toString().isEmpty())
            m_ctl->redefineStyle(it->data(Qt::UserRole).toString());
    });
    connect(del, &QToolButton::clicked, this, [this] {
        if (QListWidgetItem *it = m_styles->currentItem(); it && !it->data(Qt::UserRole).toString().isEmpty())
            m_ctl->deleteStyle(it->data(Qt::UserRole).toString());
    });

    // ---- Document Settings
    QVBoxLayout *dl;
    QWidget *docs = section(box, dl);
    auto *df = new QFormLayout;
    m_background = new ColorWell(tr("Use Default"), docs);
    m_background->setObjectName(QStringLiteral("mmBackground"));
    // the board's own color for the kind of theme in use (light or dark; 0.21.1)
    auto *bgLabel = new QLabel(docs);
    bgLabel->setObjectName(QStringLiteral("mmBackgroundLabel"));
    m_backgroundLabel = bgLabel;
    df->addRow(bgLabel, m_background);
    auto *texture = new QPushButton(tr("Use Texture…"), docs);
    df->addRow(QString(), texture);
    m_defaultText = new ColorWell(tr("Clear (black or white)"), docs);
    df->addRow(tr("Default color:"), m_defaultText);
    m_font = new QFontComboBox(docs);
    m_font->setObjectName(QStringLiteral("mmFont"));
    df->addRow(tr("Font:"), m_font);
    m_direction = new QComboBox(docs);
    m_direction->addItems({tr("Left to Right"), tr("Right to Left")});
    df->addRow(tr("Writing direction:"), m_direction);
    dl->addLayout(df);
    auto *setDefaults = new QPushButton(tr("Set As Defaults"), docs);
    setDefaults->setToolTip(tr("New boards get this background, text color, font and writing direction, and the note styles of this board"));
    dl->addWidget(setDefaults);
    dl->addStretch(1);
    box->addItem(docs, tr("Document Settings"));
    auto settingsChange = [this](const QString &text, std::function<void(Settings &)> fn) {
        if (m_updating)
            return;
        m_ctl->doc()->change(text, [&] { fn(m_ctl->board().settings); });
    };
    connect(m_background, &ColorWell::colorChosen, this, [this](const QColor &c) {
        if (!m_updating)
            m_ctl->doc()->setOwnBackground(c); // invalid: the default (of light or dark themes)
    });
    connect(texture, &QPushButton::clicked, this, [this, settingsChange] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Use Texture"), QDir::homePath(), tr("Pictures (*.png *.jpg *.jpeg *.bmp *.gif)"));
        if (f.isEmpty())
            return;
        QImage img(f);
        if (img.isNull())
            return;
        if (img.width() > 512 || img.height() > 512)
            img = img.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        settingsChange(tr("Texture"), [img](Settings &s) { s.texture = img; });
    });
    connect(m_defaultText, &ColorWell::colorChosen, this, [settingsChange](const QColor &c) {
        settingsChange(tr("Default Color"), [c](Settings &s) { s.textColor = c; });
    });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [settingsChange](const QFont &f) {
        settingsChange(tr("Font"), [f](Settings &s) { s.font = f.family(); });
    });
    connect(m_direction, qOverload<int>(&QComboBox::activated), this, [settingsChange](int i) {
        settingsChange(tr("Writing Direction"), [i](Settings &s) { s.rightToLeft = i == 1; });
    });
    connect(setDefaults, &QPushButton::clicked, this, [this] {
        MindMapPrefs &p = MindMapPrefs::instance();
        const Settings &s = m_ctl->board().settings;
        // the background: the default of the kind of theme in use only (0.21.1)
        (MindMapDoc::darkMode() ? p.boardBackgroundDark : p.boardBackground) = s.background;
        p.boardText = s.textColor;
        p.boardFont = s.font;
        p.boardRightToLeft = s.rightToLeft;
        Board styles;
        styles.styles = m_ctl->board().styles;
        p.defaultStylesXml = styles.toXml();
        p.save();
    });
    connect(ctl->scene(), &MindMapScene::selectionOrderChanged, this, &MindMapFormatPanel::refresh);
    connect(ctl->doc(), &MindMapDoc::boardChanged, this, &MindMapFormatPanel::refresh);
    refresh();
}

void MindMapFormatPanel::refreshStyles()
{
    const Board &b = m_ctl->board();
    m_styles->clear();
    auto *def = new QListWidgetItem(m_styles);
    NoteStyle d;
    d.name = tr("Default");
    def->setIcon(QIcon(stylePreview(b, d, 180, 30)));
    def->setToolTip(tr("Ctrl+Alt+0"));
    const QList<NoteStyle> list = m_ctl->sortedStyles();
    for (int i = 0; i < list.size(); ++i) {
        auto *it = new QListWidgetItem(m_styles);
        it->setIcon(QIcon(stylePreview(b, list[i], 180, 30)));
        it->setData(Qt::UserRole, list[i].id);
        it->setToolTip(i < 9 ? QStringLiteral("%1 (Ctrl+Alt+%2)").arg(list[i].name).arg(i + 1) : list[i].name);
    }
}

void MindMapFormatPanel::refresh()
{
    m_updating = true;
    const Board &b = m_ctl->board();
    const QList<int> sel = m_ctl->scene()->selectedIds();
    const int first = sel.isEmpty() ? 0 : sel.first();
    const Note *n = b.note(first);
    const Shape *s = b.shape(first);
    const bool locked = m_ctl->doc()->locked();
    const Appearance look = n ? n->look : s ? s->look : Appearance();
    m_size->setEnabled(n && !locked);
    m_size->setValue(n ? n->fontSize : 12);
    m_textColor->setEnabled(n && !locked);
    m_textColor->setColor(look.textColor);
    m_align->setEnabled(n && !locked);
    m_align->setCurrentIndex(n ? (n->align == Align::Center ? 1 : n->align == Align::Right ? 2 : n->align == Align::Justify ? 3 : 0) : 0);
    for (QWidget *w : std::initializer_list<QWidget *>{m_fill, m_faded, m_borderColor, m_thickness, m_borderStyle, m_shadow})
        w->setEnabled(first && !locked);
    m_fill->setColor(look.fill);
    m_faded->setChecked(look.faded);
    m_borderColor->setColor(look.borderColor);
    m_thickness->setCurrentIndex(qBound(0, look.borderWeight, 5));
    m_borderStyle->setCurrentIndex(int(look.borderStyle));
    m_shadow->setChecked(look.shadow);
    m_magnetic->setEnabled(s && !locked);
    m_magnetic->setChecked(s && s->magnetic);
    m_background->setColor(b.settings.texture.isNull() ? m_ctl->doc()->ownBackground() : QColor());
    m_background->setToolTip(tr("The board's own background for %1 themes; crossed out: the default of File > Options > Extras > "
                                "Mind Map (a click shows the colors)")
                                 .arg(MindMapDoc::darkMode() ? tr("dark") : tr("light")));
    if (m_backgroundLabel)
        m_backgroundLabel->setText(MindMapDoc::darkMode() ? tr("Background (dark themes):") : tr("Background (light themes):"));
    m_defaultText->setColor(b.settings.textColor);
    {
        const QSignalBlocker block(m_font);
        m_font->setCurrentFont(QFont(b.fontFamily()));
    }
    m_direction->setCurrentIndex(b.settings.rightToLeft ? 1 : 0);
    for (QWidget *w : std::initializer_list<QWidget *>{m_background, m_defaultText, m_font, m_direction})
        w->setEnabled(!locked);
    refreshStyles();
    m_updating = false;
}

// ------------------------------------------------------------------ footer

MindMapFooter::MindMapFooter(MindMapController *ctl, bool compact, QWidget *parent) : QWidget(parent), m_ctl(ctl)
{
    setObjectName(QStringLiteral("mindMapFooter"));
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 2, 6, 2);
    m_move = new QLabel(this);
    m_move->setToolTip(tr("Arrow keys move notes (M switches)"));
    lay->addWidget(m_move);
    lay->addStretch(1);
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("mmCount"));
    lay->addWidget(m_count);
    lay->addStretch(1);
    m_zoom = new QSlider(Qt::Horizontal, this);
    m_zoom->setObjectName(QStringLiteral("mmZoomSlider"));
    m_zoom->setRange(10, 400);
    m_zoom->setFixedWidth(compact ? 80 : 140);
    lay->addWidget(m_zoom);
    auto *reset = new QToolButton(this);
    reset->setText(tr("Reset"));
    reset->setAutoRaise(true);
    lay->addWidget(reset);
    connect(m_zoom, &QSlider::valueChanged, this, [this](int v) {
        if (qAbs(m_ctl->view()->zoom() * 100 - v) > 0.5)
            m_ctl->view()->setZoom(v / 100.0);
    });
    connect(reset, &QToolButton::clicked, this, [this] { m_ctl->view()->actualSize(); });
    connect(ctl->view(), &MindMapView::zoomChanged, this, &MindMapFooter::refresh);
    connect(ctl->view(), &MindMapView::movementModeChanged, this, &MindMapFooter::refresh);
    connect(ctl->scene(), &MindMapScene::selectionOrderChanged, this, &MindMapFooter::refresh);
    connect(ctl->doc(), &MindMapDoc::boardChanged, this, &MindMapFooter::refresh);
    refresh();
}

void MindMapFooter::refresh()
{
    const int n = m_ctl->board().notes.size();
    const int sel = m_ctl->scene()->selectedNotes().size();
    QString t = tr("%n note(s)", nullptr, n);
    if (sel > 1)
        t += QStringLiteral(" · ") + tr("%n selected", nullptr, sel);
    m_count->setText(t);
    m_move->setText(m_ctl->view()->movementMode() ? tr("✥ Move") : QString());
    const QSignalBlocker b(m_zoom);
    m_zoom->setValue(int(std::round(m_ctl->view()->zoom() * 100)));
    m_zoom->setToolTip(tr("Zoom: %1 %").arg(int(std::round(m_ctl->view()->zoom() * 100))));
}

// ------------------------------------------------------------------ inspector panel

MindMapPanel::MindMapPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("mindMapPanel"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_stack = new QStackedWidget(this);
    m_empty = new QLabel(tr("Select a document or folder in the binder: each one can have a mind map of its own."), this);
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setMargin(16);
    m_stack->addWidget(m_empty);
    m_holder = new QWidget(this);
    m_stack->addWidget(m_holder);
    lay->addWidget(m_stack, 1);
}

void MindMapPanel::setItem(Project *project, const QString &itemId)
{
    if (project == m_project && itemId == m_itemId && m_ctl)
        return;
    m_project = project;
    m_itemId = itemId;
    rebuild();
}

void MindMapPanel::rebuild()
{
    // the controls belong to one board: made anew for another one
    // (later: this may run while the old view is handling a click, e.g. on a link)
    if (m_view)
        m_view->saveView(); // the board shown so far: its view is kept (0.21.1)
    if (m_holder->layout()) {
        for (QWidget *w : m_holder->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            w->hide();
            w->deleteLater();
        }
        delete m_holder->layout();
    }
    m_view = nullptr;
    m_findBar = nullptr;
    m_footer = nullptr;
    if (m_ctl)
        m_ctl->deleteLater();
    m_ctl = nullptr;
    m_doc = (m_project && !m_itemId.isEmpty()) ? mindmaps::docFor(m_project, m_itemId) : nullptr;
    if (!m_doc) {
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    auto *lay = new QVBoxLayout(m_holder);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_view = new MindMapView(m_holder);
    m_view->setObjectName(QStringLiteral("mindMapView"));
    m_ctl = new MindMapController(m_doc, m_view, m_holder, false);
    // a small bar: the most used commands and the menus of the large window
    m_bar = new QWidget(m_holder);
    auto *bl = new QHBoxLayout(m_bar);
    bl->setContentsMargins(4, 2, 4, 2);
    bl->setSpacing(2);
    auto button = [&](const QString &key, const QString &tip = QString()) {
        auto *b = new QToolButton(m_bar);
        b->setDefaultAction(m_ctl->action(key));
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(18, 18));
        if (!tip.isEmpty())
            b->setToolTip(tip);
        bl->addWidget(b);
        return b;
    };
    button(QStringLiteral("mm_newNote"));
    button(QStringLiteral("mm_newShape"));
    button(QStringLiteral("mm_zoomFit"));
    auto *menuBtn = new QToolButton(m_bar);
    menuBtn->setObjectName(QStringLiteral("mmMenuButton"));
    menuBtn->setText(tr("Menu"));
    menuBtn->setIcon(icons::action(QStringLiteral("qb-mm-menu")));
    menuBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    menuBtn->setIconSize(QSize(18, 18));
    menuBtn->setToolTip(tr("All commands of the mind map"));
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setAutoRaise(true);
    auto *menu = new QMenu(menuBtn);
    MindMapController *ctl = m_ctl;
    auto addMenu = [&](const QString &title, std::function<void(QMenu *)> fill) {
        QMenu *m = menu->addMenu(title);
        connect(m, &QMenu::aboutToShow, m, [m, fill, ctl] {
            m->clear();
            ctl->updateActions();
            fill(m);
        });
        m->addAction(QString()); // so that the menu can open before it is filled
    };
    addMenu(tr("&File"), [ctl](QMenu *m) { ctl->fillFileMenu(m); });
    addMenu(tr("&Edit"), [ctl](QMenu *m) { ctl->fillEditMenu(m); });
    addMenu(tr("&View"), [ctl](QMenu *m) { ctl->fillViewMenu(m); });
    addMenu(tr("&Notes"), [ctl](QMenu *m) { ctl->fillNotesMenu(m); });
    addMenu(tr("F&ormat"), [ctl](QMenu *m) { ctl->fillFormatMenu(m); });
    addMenu(tr("&Help"), [ctl](QMenu *m) { ctl->fillHelpMenu(m); });
    menuBtn->setMenu(menu);
    bl->addWidget(menuBtn);
    bl->addStretch(1);
    QToolButton *help = button(QStringLiteral("mm_help"), tr("How the mind map works: notes, lines, arrows (and an example board)"));
    help->setObjectName(QStringLiteral("mmHelpButton"));
    QToolButton *big = button(QStringLiteral("mm_openWindow"), tr("Open this mind map in a large window (with the inspector of the board)"));
    big->setObjectName(QStringLiteral("mmOpenWindow"));
    lay->addWidget(m_bar);
    m_findBar = new MindMapFindBar(m_ctl, m_holder);
    m_ctl->setFindBar(m_findBar);
    lay->addWidget(m_findBar);
    lay->addWidget(m_view, 1);
    m_footer = new MindMapFooter(m_ctl, true, m_holder);
    lay->addWidget(m_footer);
    m_footer->setVisible(m_doc->board.settings.footer);
    connect(m_ctl, &MindMapController::footerToggled, m_footer, &QWidget::setVisible);
    connect(m_ctl, &MindMapController::findRequested, m_findBar, &MindMapFindBar::open);
    connect(m_ctl, &MindMapController::openWindowRequested, this, [this] {
        if (m_doc)
            emit openWindowRequested(m_doc);
    });
    m_view->setBoardScene(m_doc->scene());
    // the zoom and place this board had in the inspector (0.21.1)
    m_view->setViewMemory(m_project, m_itemId, QStringLiteral("inspector"));
    m_stack->setCurrentWidget(m_holder);
}

// ------------------------------------------------------------------ window

static QHash<MindMapDoc *, QPointer<MindMapWindow>> &windows()
{
    static QHash<MindMapDoc *, QPointer<MindMapWindow>> w;
    return w;
}

MindMapWindow *MindMapWindow::existing(MindMapDoc *doc) { return windows().value(doc); }

MindMapWindow *MindMapWindow::open(MindMapDoc *doc, QWidget *parent)
{
    if (!doc)
        return nullptr;
    MindMapWindow *w = windows().value(doc);
    if (!w) {
        w = new MindMapWindow(doc, parent);
        windows().insert(doc, w);
        w->resize(1100, 760);
    }
    w->show();
    w->raise();
    w->activateWindow();
    return w;
}

MindMapWindow::MindMapWindow(MindMapDoc *doc, QWidget *parent) : QMainWindow(parent, Qt::Window), m_doc(doc)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("mindMapWindow"));
    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_view = new MindMapView(central);
    m_view->setObjectName(QStringLiteral("mindMapWindowView"));
    m_ctl = new MindMapController(doc, m_view, this, true);
    m_findBar = new MindMapFindBar(m_ctl, central);
    m_ctl->setFindBar(m_findBar);
    lay->addWidget(m_findBar);
    auto *split = new QHBoxLayout;
    split->setContentsMargins(0, 0, 0, 0);
    split->setSpacing(0);
    split->addWidget(m_view, 1);
    auto *scroll = new QScrollArea(central);
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(290);
    m_format = new MindMapFormatPanel(m_ctl, scroll);
    scroll->setWidget(m_format);
    m_side = scroll;
    split->addWidget(scroll);
    lay->addLayout(split, 1);
    m_footer = new MindMapFooter(m_ctl, false, central);
    lay->addWidget(m_footer);
    m_footer->setVisible(doc->board.settings.footer);
    setCentralWidget(central);

    QMenuBar *mb = menuBar();
    MindMapController *ctl = m_ctl;
    auto addMenu = [&](const QString &title, std::function<void(QMenu *)> fill) {
        QMenu *m = mb->addMenu(title);
        fill(m);
        connect(m, &QMenu::aboutToShow, ctl, &MindMapController::updateActions);
        return m;
    };
    QMenu *file = addMenu(tr("&File"), [ctl](QMenu *m) { ctl->fillFileMenu(m); });
    file->addSeparator();
    file->addAction(icons::action(QStringLiteral("window-close")), tr("&Close"), QKeySequence::Close, this, &QWidget::close);
    addMenu(tr("&Edit"), [ctl](QMenu *m) { ctl->fillEditMenu(m); });
    addMenu(tr("&View"), [ctl](QMenu *m) { ctl->fillViewMenu(m); });
    addMenu(tr("&Notes"), [ctl](QMenu *m) { ctl->fillNotesMenu(m); });
    addMenu(tr("F&ormat"), [ctl](QMenu *m) { ctl->fillFormatMenu(m); });
    addMenu(tr("&Help"), [ctl](QMenu *m) { ctl->fillHelpMenu(m); });

    connect(m_ctl, &MindMapController::inspectorToggled, m_side, &QWidget::setVisible);
    connect(m_ctl, &MindMapController::footerToggled, m_footer, &QWidget::setVisible);
    connect(m_ctl, &MindMapController::findRequested, m_findBar, &MindMapFindBar::open);
    m_view->setBoardScene(doc->scene());
    // the zoom and place this board had in its window (0.21.1)
    m_view->setViewMemory(doc->project(), doc->itemId(), QStringLiteral("window"));
    connect(doc, &QObject::destroyed, this, &QWidget::close);
    connect(doc->undoStack(), &QUndoStack::cleanChanged, this, &MindMapWindow::updateTitle);
    connect(doc, &MindMapDoc::boardChanged, this, &MindMapWindow::updateTitle);
    updateTitle();
}

void MindMapWindow::updateTitle()
{
    if (!m_doc)
        return;
    QString title = m_doc->title().isEmpty() ? tr("Mind Map") : tr("Mind Map: %1").arg(m_doc->title());
    if (Project *p = m_doc->project())
        if (BinderItem *it = p->item(m_doc->itemId()))
            title = tr("Mind Map: %1").arg(it->title.isEmpty() ? tr("Untitled") : it->title);
    setWindowTitle(title);
}

void MindMapWindow::closeEvent(QCloseEvent *e)
{
    if (m_view)
        m_view->saveView(); // the window's zoom and place of this board (0.21.1)
    if (m_doc) {
        m_doc->scene()->endEditing();
        m_doc->save();
        windows().remove(m_doc);
    }
    QMainWindow::closeEvent(e);
}

} // namespace qb
