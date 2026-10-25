#include "ui/FormatDialogs.h"

#include "text/DisplayFormat.h"
#include "ui/AppSettings.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace qb {

namespace currentFormatting {
static std::function<bool(Formatting *)> s_provider;
void setProvider(std::function<bool(Formatting *)> provider) { s_provider = std::move(provider); }
bool get(Formatting *f) { return s_provider && s_provider(f); }
} // namespace currentFormatting

// ====================================================================== font

namespace {
enum { WeightRole = Qt::UserRole + 1, ItalicRole };
}

FontDialog::FontDialog(const FontChoice &current, const Formatting &project, const QString &sample, QWidget *parent)
    : QDialog(parent), m_initial(current), m_project(project), m_sample(sample)
{
    setWindowTitle(tr("Fonts"));
    setObjectName(QStringLiteral("fontDialog"));
    m_color = current.spec.color;
    auto *lay = new QVBoxLayout(this);
    auto *lists = new QGridLayout;
    lay->addLayout(lists, 1);

    // family
    lists->addWidget(new QLabel(tr("Font"), this), 0, 0);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search fonts"));
    m_filter->setClearButtonEnabled(true);
    lists->addWidget(m_filter, 1, 0);
    m_families = new QListWidget(this);
    m_families->setObjectName(QStringLiteral("fontFamilies"));
    m_families->setUniformItemSizes(true);
    lists->addWidget(m_families, 2, 0);
    const QString projectFamily = display::resolvedFontFamily(project);
    auto *def = new QListWidgetItem(tr("Project font (%1)").arg(projectFamily), m_families);
    def->setData(Qt::UserRole, QString());
    for (const QString &fam : QFontDatabase::families()) {
        if (QFontDatabase::isPrivateFamily(fam))
            continue;
        auto *it = new QListWidgetItem(fam, m_families);
        it->setData(Qt::UserRole, fam);
    }

    // typeface
    lists->addWidget(new QLabel(tr("Typeface"), this), 0, 1);
    m_typefaces = new QListWidget(this);
    m_typefaces->setObjectName(QStringLiteral("fontTypefaces"));
    lists->addWidget(m_typefaces, 1, 1, 2, 1);

    // size
    lists->addWidget(new QLabel(tr("Size"), this), 0, 2);
    m_size = new QDoubleSpinBox(this);
    m_size->setObjectName(QStringLiteral("fontSize"));
    m_size->setRange(4, 400);
    m_size->setDecimals(1);
    m_size->setSuffix(tr(" pt"));
    lists->addWidget(m_size, 1, 2);
    m_sizes = new QListWidget(this);
    m_sizes->setMaximumWidth(110);
    for (double sz : {8.0, 9.0, 10.0, 10.5, 11.0, 12.0, 13.0, 14.0, 16.0, 18.0, 20.0, 24.0, 28.0, 36.0, 48.0, 72.0})
        new QListWidgetItem(QLocale().toString(sz), m_sizes);
    lists->addWidget(m_sizes, 2, 2);
    lists->setColumnStretch(0, 3);
    lists->setColumnStretch(1, 2);

    // effects
    auto *fx = new QGroupBox(tr("Fine-tuning"), this);
    auto *fl = new QGridLayout(fx);
    m_underline = new QCheckBox(tr("Underline"), fx);
    m_strike = new QCheckBox(tr("Strikethrough"), fx);
    m_caps = new QComboBox(fx);
    m_caps->setObjectName(QStringLiteral("fontCaps"));
    m_caps->addItems({tr("As typed"), tr("Small capitals"), tr("All capitals")});
    m_spacing = new QSpinBox(fx);
    m_spacing->setObjectName(QStringLiteral("fontSpacing"));
    m_spacing->setRange(50, 400);
    m_spacing->setSingleStep(5);
    m_spacing->setSuffix(QStringLiteral(" %"));
    m_spacing->setToolTip(tr("Distance between the letters: 100 % is the font's own spacing"));
    m_colorButton = new QPushButton(fx);
    m_colorButton->setObjectName(QStringLiteral("fontColor"));
    auto *autoColor = new QPushButton(tr("Reset Color"), fx);
    autoColor->setToolTip(tr("Use the text color of the theme"));
    fl->addWidget(m_underline, 0, 0);
    fl->addWidget(m_strike, 1, 0);
    fl->addWidget(new QLabel(tr("Capitals:"), fx), 0, 1, Qt::AlignRight);
    fl->addWidget(m_caps, 0, 2);
    fl->addWidget(new QLabel(tr("Letter spacing:"), fx), 1, 1, Qt::AlignRight);
    fl->addWidget(m_spacing, 1, 2);
    fl->addWidget(new QLabel(tr("Color:"), fx), 0, 3, Qt::AlignRight);
    fl->addWidget(m_colorButton, 0, 4);
    fl->addWidget(autoColor, 1, 4);
    fl->setColumnStretch(5, 1);
    lay->addWidget(fx);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("fontPreview"));
    m_preview->setFrameShape(QFrame::StyledPanel);
    m_preview->setAutoFillBackground(true);
    m_preview->setBackgroundRole(QPalette::Base);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(90);
    m_preview->setMaximumHeight(140);
    m_preview->setTextFormat(Qt::PlainText);
    lay->addWidget(m_preview);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *reset = bb->addButton(tr("Project Font"), QDialogButtonBox::ResetRole);
    reset->setToolTip(tr("Back to the font, size and color of the project"));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // initial values
    m_size->setValue(current.spec.size > 0 ? current.spec.size : project.fontSize);
    m_underline->setChecked(current.underline);
    m_strike->setChecked(current.strike);
    m_caps->setCurrentIndex(qBound(0, current.spec.caps, 2));
    m_spacing->setValue(current.spec.spacing > 0 ? current.spec.spacing : 100);
    selectFamily(current.spec.family);
    fillTypefaces();

    connect(m_filter, &QLineEdit::textChanged, this, &FontDialog::filterFamilies);
    connect(m_families, &QListWidget::currentRowChanged, this, [this] {
        if (m_filling)
            return;
        fillTypefaces();
        updatePreview();
    });
    connect(m_typefaces, &QListWidget::currentRowChanged, this, [this] { updatePreview(); });
    connect(m_sizes, &QListWidget::currentTextChanged, this, [this](const QString &t) {
        bool ok = false;
        const double v = QLocale().toDouble(t, &ok);
        if (ok)
            m_size->setValue(v);
    });
    connect(m_size, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updatePreview(); });
    connect(m_underline, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(m_strike, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(m_caps, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updatePreview(); });
    connect(m_spacing, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updatePreview(); });
    connect(m_colorButton, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color.isValid() ? m_color : palette().color(QPalette::Text), this,
                                                tr("Text Color"));
        if (c.isValid()) {
            m_color = c;
            updatePreview();
        }
    });
    connect(autoColor, &QPushButton::clicked, this, [this] {
        m_color = QColor();
        updatePreview();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_filter->clear();
        selectFamily(QString());
        fillTypefaces();
        m_size->setValue(m_project.fontSize);
        m_caps->setCurrentIndex(0);
        m_spacing->setValue(100);
        m_color = QColor();
        updatePreview();
    });
    updatePreview();
    resize(640, 560);
}

void FontDialog::selectFamily(const QString &family)
{
    m_filling = true;
    int row = 0;
    for (int i = 0; i < m_families->count(); ++i)
        if (m_families->item(i)->data(Qt::UserRole).toString().compare(family, Qt::CaseInsensitive) == 0) {
            row = i;
            break;
        }
    m_families->setCurrentRow(row);
    m_families->scrollToItem(m_families->item(row), QAbstractItemView::PositionAtCenter);
    m_filling = false;
}

void FontDialog::filterFamilies(const QString &text)
{
    for (int i = 0; i < m_families->count(); ++i) {
        QListWidgetItem *it = m_families->item(i);
        it->setHidden(!text.isEmpty() && !it->text().contains(text, Qt::CaseInsensitive));
    }
}

void FontDialog::fillTypefaces()
{
    // remember what was chosen, the new family may offer it too
    int weight = m_initial.spec.weight > 0 ? m_initial.spec.weight : (m_initial.bold ? QFont::Bold : QFont::Normal);
    bool italic = m_initial.italic;
    if (QListWidgetItem *cur = m_typefaces->currentItem()) {
        weight = cur->data(WeightRole).toInt();
        italic = cur->data(ItalicRole).toBool();
    }
    m_typefaces->clear();
    QString fam = m_families->currentItem() ? m_families->currentItem()->data(Qt::UserRole).toString() : QString();
    if (fam.isEmpty())
        fam = display::resolvedFontFamily(m_project);
    QStringList styles = QFontDatabase::styles(fam);
    if (styles.isEmpty())
        styles = QStringList{QStringLiteral("Regular"), QStringLiteral("Italic"), QStringLiteral("Bold"),
                             QStringLiteral("Bold Italic")};
    struct Face {
        QString name;
        int weight;
        bool italic;
    };
    QList<Face> faces;
    for (const QString &st : styles) {
        int w = QFontDatabase::weight(fam, st);
        if (w <= 0)
            w = st.contains(QLatin1String("Bold"), Qt::CaseInsensitive) ? QFont::Bold : QFont::Normal;
        const bool i = QFontDatabase::italic(fam, st) || st.contains(QLatin1String("Italic"), Qt::CaseInsensitive)
                       || st.contains(QLatin1String("Oblique"), Qt::CaseInsensitive);
        faces.append({st, w, i});
    }
    // light to heavy, upright before italic; condensed cuts last
    std::stable_sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) {
        const bool ca = a.name.contains(QLatin1String("Condensed"), Qt::CaseInsensitive);
        const bool cb = b.name.contains(QLatin1String("Condensed"), Qt::CaseInsensitive);
        if (ca != cb)
            return cb;
        if (a.weight != b.weight)
            return a.weight < b.weight;
        return !a.italic && b.italic;
    });
    int best = 0, bestScore = 1 << 30;
    for (const Face &fc : faces) {
        auto *it = new QListWidgetItem(fc.name, m_typefaces);
        it->setData(WeightRole, fc.weight);
        it->setData(ItalicRole, fc.italic);
        const int score = qAbs(fc.weight - weight) + (fc.italic != italic ? 2000 : 0)
                          + (fc.name.contains(QLatin1String("Condensed"), Qt::CaseInsensitive) ? 5000 : 0);
        if (score < bestScore) {
            bestScore = score;
            best = m_typefaces->count() - 1;
        }
    }
    m_typefaces->setCurrentRow(best);
}

FontChoice FontDialog::choice() const
{
    FontChoice c;
    if (QListWidgetItem *f = m_families->currentItem())
        c.spec.family = f->data(Qt::UserRole).toString();
    if (qAbs(m_size->value() - m_project.fontSize) > 0.01)
        c.spec.size = m_size->value();
    int weight = QFont::Normal;
    if (QListWidgetItem *t = m_typefaces->currentItem()) {
        weight = t->data(WeightRole).toInt();
        c.italic = t->data(ItalicRole).toBool();
    }
    c.bold = weight >= QFont::DemiBold;
    if (weight != QFont::Normal && weight != QFont::Bold)
        c.spec.weight = weight;
    c.underline = m_underline->isChecked();
    c.strike = m_strike->isChecked();
    c.spec.caps = m_caps->currentIndex();
    c.spec.spacing = m_spacing->value() == 100 ? 0 : m_spacing->value();
    c.spec.color = m_color;
    return c;
}

int FontDialog::changedMembers() const
{
    const FontChoice c = choice();
    int m = 0;
    if (c.spec.family.compare(m_initial.spec.family, Qt::CaseInsensitive) != 0)
        m |= direct::Family;
    const double was = m_initial.spec.size > 0 ? m_initial.spec.size : m_project.fontSize;
    if (qAbs(m_size->value() - was) > 0.01)
        m |= direct::Size;
    if (c.spec.weight != m_initial.spec.weight)
        m |= direct::Weight;
    if (c.spec.color != m_initial.spec.color)
        m |= direct::Color;
    if (c.spec.spacing != m_initial.spec.spacing)
        m |= direct::Spacing;
    if (c.spec.caps != m_initial.spec.caps)
        m |= direct::Caps;
    return m;
}

void FontDialog::updatePreview()
{
    const FontChoice c = choice();
    QFont f(c.spec.family.isEmpty() ? display::resolvedFontFamily(m_project) : c.spec.family);
    f.setPointSizeF(qBound(6.0, m_size->value(), 60.0));
    f.setWeight(QFont::Weight(c.spec.weight > 0 ? c.spec.weight : (c.bold ? QFont::Bold : QFont::Normal)));
    f.setItalic(c.italic);
    f.setUnderline(c.underline);
    f.setStrikeOut(c.strike);
    if (c.spec.spacing > 0)
        f.setLetterSpacing(QFont::PercentageSpacing, c.spec.spacing);
    f.setCapitalization(c.spec.caps == 1 ? QFont::SmallCaps : c.spec.caps == 2 ? QFont::AllUppercase : QFont::MixedCase);
    m_preview->setFont(f);
    QPalette pal = m_preview->palette();
    pal.setColor(QPalette::WindowText, m_color.isValid() ? m_color : palette().color(QPalette::Text));
    m_preview->setPalette(pal);
    QString sample = m_sample.simplified();
    if (sample.size() > 60)
        sample = sample.left(60);
    m_preview->setText(sample.isEmpty() ? tr("The quick brown fox jumps over the lazy dog") : sample);
    // color swatch
    QPixmap pm(28, 14);
    pm.fill(m_color.isValid() ? m_color : palette().color(QPalette::Text));
    m_colorButton->setIcon(QIcon(pm));
    m_colorButton->setText(m_color.isValid() ? m_color.name() : tr("Automatic"));
}

// ====================================================================== paragraph

namespace {

QString tabTypeName(QTextOption::TabType t)
{
    switch (t) {
    case QTextOption::RightTab: return FontDialog::tr("Right");
    case QTextOption::CenterTab: return FontDialog::tr("Centered");
    case QTextOption::DelimiterTab: return FontDialog::tr("Decimal point");
    default: return FontDialog::tr("Left");
    }
}

QDoubleSpinBox *lengthSpin(QWidget *parent, double minPx, double maxPx)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(2);
    s->setRange(units::fromPx(minPx), units::fromPx(maxPx));
    s->setSingleStep(units::metric() ? 0.25 : 0.125);
    s->setSuffix(units::suffix());
    return s;
}

} // namespace

ParagraphDialog::ParagraphDialog(const direct::ParaSpec &current, QWidget *parent)
    : QDialog(parent), m_initial(current), m_tabs(current.tabs)
{
    setWindowTitle(tr("Paragraph"));
    setObjectName(QStringLiteral("paragraphDialog"));
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    lay->addLayout(top);

    auto *left = new QVBoxLayout;
    top->addLayout(left);
    auto *ind = new QGroupBox(tr("Alignment and indents"), this);
    auto *il = new QFormLayout(ind);
    m_align = new QComboBox(ind);
    m_align->setObjectName(QStringLiteral("paraAlign"));
    m_align->addItem(tr("Left"), int(Qt::AlignLeft));
    m_align->addItem(tr("Centered"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Right"), int(Qt::AlignRight));
    m_align->addItem(tr("Justified"), int(Qt::AlignJustify));
    m_left = lengthSpin(ind, 0, 2000);
    m_left->setObjectName(QStringLiteral("paraLeft"));
    m_right = lengthSpin(ind, 0, 2000);
    m_right->setObjectName(QStringLiteral("paraRight"));
    m_first = lengthSpin(ind, -1000, 1000);
    m_first->setObjectName(QStringLiteral("paraFirst"));
    m_first->setToolTip(tr("Indent of the first line; a negative value gives a hanging indent"));
    il->addRow(tr("Alignment:"), m_align);
    il->addRow(tr("Left:"), m_left);
    il->addRow(tr("Right:"), m_right);
    il->addRow(tr("First line:"), m_first);
    left->addWidget(ind);

    auto *sp = new QGroupBox(tr("Spacing"), this);
    auto *sl = new QFormLayout(sp);
    m_before = new QDoubleSpinBox(sp);
    m_before->setObjectName(QStringLiteral("paraBefore"));
    m_after = new QDoubleSpinBox(sp);
    m_after->setObjectName(QStringLiteral("paraAfter"));
    for (QDoubleSpinBox *b : {m_before, m_after}) {
        b->setRange(0, 400);
        b->setDecimals(1);
        b->setSuffix(tr(" pt"));
    }
    m_lineMode = new QComboBox(sp);
    m_lineMode->setObjectName(QStringLiteral("paraLineMode"));
    m_lineMode->addItem(tr("Multiple of the line height"), int(QTextBlockFormat::ProportionalHeight));
    m_lineMode->addItem(tr("Exactly"), int(QTextBlockFormat::FixedHeight));
    m_lineMode->addItem(tr("At least"), int(QTextBlockFormat::MinimumHeight));
    m_line = new QDoubleSpinBox(sp);
    m_line->setObjectName(QStringLiteral("paraLine"));
    sl->addRow(tr("Before:"), m_before);
    sl->addRow(tr("After:"), m_after);
    sl->addRow(tr("Line spacing:"), m_lineMode);
    sl->addRow(QString(), m_line);
    left->addWidget(sp);
    left->addStretch(1);

    auto *tb = new QGroupBox(tr("Tab stops"), this);
    auto *tl = new QVBoxLayout(tb);
    m_tabList = new QListWidget(tb);
    m_tabList->setObjectName(QStringLiteral("paraTabs"));
    tl->addWidget(m_tabList, 1);
    auto *row = new QHBoxLayout;
    m_tabPos = lengthSpin(tb, 1, 4000);
    m_tabPos->setObjectName(QStringLiteral("paraTabPos"));
    m_tabPos->setValue(units::metric() ? 2.5 : 1.0);
    m_tabType = new QComboBox(tb);
    m_tabType->setObjectName(QStringLiteral("paraTabType"));
    for (QTextOption::TabType t : {QTextOption::LeftTab, QTextOption::CenterTab, QTextOption::RightTab, QTextOption::DelimiterTab})
        m_tabType->addItem(tabTypeName(t), int(t));
    row->addWidget(m_tabPos);
    row->addWidget(m_tabType, 1);
    tl->addLayout(row);
    auto *row2 = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add"), tb);
    add->setObjectName(QStringLiteral("paraTabAdd"));
    auto *remove = new QPushButton(tr("Remove"), tb);
    auto *clear = new QPushButton(tr("Remove All"), tb);
    row2->addWidget(add);
    row2->addWidget(remove);
    row2->addWidget(clear);
    tl->addLayout(row2);
    auto *hint = new QLabel(tr("Positions are measured from the start of the first line."), tb);
    hint->setWordWrap(true);
    tl->addWidget(hint);
    top->addWidget(tb, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *reset = bb->addButton(tr("Reset to Default"), QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("paraReset"));
    reset->setToolTip(tr("Remove the paragraph formatting set by hand; the paragraph follows its style and the project again"));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(reset, &QPushButton::clicked, this, [this] {
        m_reset = true;
        accept();
    });

    // values
    m_align->setCurrentIndex(qMax(0, m_align->findData(int(current.align))));
    m_left->setValue(units::fromPx(current.left));
    m_right->setValue(units::fromPx(current.right));
    m_first->setValue(units::fromPx(current.first));
    m_before->setValue(direct::pxToPt(current.before));
    m_after->setValue(direct::pxToPt(current.after));
    m_lineMode->setCurrentIndex(qMax(0, m_lineMode->findData(current.lineType)));
    updateLineUnit();
    m_line->setValue(current.lineType == QTextBlockFormat::ProportionalHeight ? current.line / 100.0
                                                                              : direct::pxToPt(current.line));
    refreshTabs();

    connect(m_lineMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const bool wasMultiple = m_line->suffix().isEmpty();
        updateLineUnit();
        const bool multiple = m_lineMode->currentData().toInt() == QTextBlockFormat::ProportionalHeight;
        if (multiple && !wasMultiple)
            m_line->setValue(1.2);
        else if (!multiple && wasMultiple)
            m_line->setValue(16);
    });
    connect(add, &QPushButton::clicked, this, [this] {
        const double px = units::toPx(m_tabPos->value());
        const auto type = QTextOption::TabType(m_tabType->currentData().toInt());
        for (int i = m_tabs.size() - 1; i >= 0; --i)
            if (qAbs(m_tabs[i].position - px) < 1.0)
                m_tabs.removeAt(i);
        m_tabs.append(QTextOption::Tab(px, type, type == QTextOption::DelimiterTab ? QLatin1Char('.') : QChar()));
        std::sort(m_tabs.begin(), m_tabs.end(),
                  [](const QTextOption::Tab &a, const QTextOption::Tab &b) { return a.position < b.position; });
        refreshTabs();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int r = m_tabList->currentRow();
        if (r >= 0 && r < m_tabs.size()) {
            m_tabs.removeAt(r);
            refreshTabs();
        }
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_tabs.clear();
        refreshTabs();
    });
    connect(m_tabList, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r >= 0 && r < m_tabs.size()) {
            m_tabPos->setValue(units::fromPx(m_tabs[r].position));
            m_tabType->setCurrentIndex(qMax(0, m_tabType->findData(int(m_tabs[r].type))));
        }
    });
}

void ParagraphDialog::updateLineUnit()
{
    const bool multiple = m_lineMode->currentData().toInt() == QTextBlockFormat::ProportionalHeight;
    if (multiple) {
        m_line->setSuffix(QString());
        m_line->setDecimals(2);
        m_line->setRange(0.5, 10.0);
        m_line->setSingleStep(0.05);
    } else {
        m_line->setSuffix(tr(" pt"));
        m_line->setDecimals(1);
        m_line->setRange(4, 400);
        m_line->setSingleStep(1);
    }
}

void ParagraphDialog::refreshTabs()
{
    m_tabList->clear();
    for (const QTextOption::Tab &t : m_tabs)
        m_tabList->addItem(QStringLiteral("%1  -  %2").arg(units::format(t.position), tabTypeName(t.type)));
}

direct::ParaSpec ParagraphDialog::values() const
{
    direct::ParaSpec s;
    s.align = Qt::Alignment(m_align->currentData().toInt());
    s.left = units::toPx(m_left->value());
    s.right = units::toPx(m_right->value());
    s.first = units::toPx(m_first->value());
    s.before = direct::ptToPx(m_before->value());
    s.after = direct::ptToPx(m_after->value());
    s.lineType = m_lineMode->currentData().toInt();
    s.line = s.lineType == QTextBlockFormat::ProportionalHeight ? m_line->value() * 100.0 : direct::ptToPx(m_line->value());
    s.tabs = m_tabs;
    return s;
}

direct::ParaSpec ParagraphDialog::result() const
{
    direct::ParaSpec s = values();
    // only what was changed: compare in the dialog's own precision
    auto changed = [](double now, double was, double eps) { return qAbs(now - was) > eps; };
    const double unitEps = units::toPx(0.005);
    s.bits = 0;
    if (s.align != m_initial.align)
        s.bits |= direct::Align;
    if (changed(s.left, m_initial.left, unitEps))
        s.bits |= direct::Left;
    if (changed(s.right, m_initial.right, unitEps))
        s.bits |= direct::Right;
    if (changed(s.first, m_initial.first, unitEps))
        s.bits |= direct::First;
    if (changed(s.before, m_initial.before, 0.07))
        s.bits |= direct::Before;
    if (changed(s.after, m_initial.after, 0.07))
        s.bits |= direct::After;
    if (s.lineType != m_initial.lineType
        || changed(s.line, m_initial.line, s.lineType == QTextBlockFormat::ProportionalHeight ? 0.5 : 0.07))
        s.bits |= direct::Line;
    bool sameTabs = s.tabs.size() == m_initial.tabs.size();
    for (int i = 0; sameTabs && i < s.tabs.size(); ++i)
        sameTabs = qAbs(s.tabs[i].position - m_initial.tabs[i].position) < 0.05 && s.tabs[i].type == m_initial.tabs[i].type;
    if (!sameTabs)
        s.bits |= direct::Tabs;
    return s;
}

// ====================================================================== default formatting

DefaultFormattingDialog::DefaultFormattingDialog(int documents, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Text to Default Formatting"));
    setObjectName(QStringLiteral("defaultFormattingDialog"));
    auto *lay = new QVBoxLayout(this);
    auto *info = new QLabel(documents == 1
                                ? tr("Give the selected document the formatting of the project again. Bold, italics, "
                                     "styles, notes and links stay.")
                                : tr("Give the %1 selected documents the formatting of the project again. Bold, italics, "
                                     "styles, notes and links stay.")
                                      .arg(documents),
                            this);
    info->setWordWrap(true);
    lay->addWidget(info);
    m_fonts = new QCheckBox(tr("Remove fonts, sizes, colors and letter spacing"), this);
    m_fonts->setObjectName(QStringLiteral("dfFonts"));
    m_fonts->setChecked(true);
    m_paragraphs = new QCheckBox(tr("Remove indents, spacing and line spacing"), this);
    m_paragraphs->setObjectName(QStringLiteral("dfParagraphs"));
    m_paragraphs->setChecked(true);
    m_keepAlign = new QCheckBox(tr("Keep the alignment (centered, right, justified)"), this);
    m_keepAlign->setObjectName(QStringLiteral("dfKeepAlign"));
    m_keepAlign->setChecked(true);
    m_keepTabs = new QCheckBox(tr("Keep tab stops"), this);
    m_keepTabs->setChecked(true);
    lay->addWidget(m_fonts);
    lay->addWidget(m_paragraphs);
    lay->addWidget(m_keepAlign);
    lay->addWidget(m_keepTabs);
    auto *warn = new QLabel(tr("A snapshot of each document is taken first, so the old formatting can be brought back."), this);
    warn->setWordWrap(true);
    lay->addWidget(warn);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool DefaultFormattingDialog::removeFonts() const { return m_fonts->isChecked(); }

int DefaultFormattingDialog::paragraphBits() const
{
    int bits = 0;
    if (m_paragraphs->isChecked())
        bits |= direct::Left | direct::Right | direct::First | direct::Before | direct::After | direct::Line;
    if (!m_keepAlign->isChecked())
        bits |= direct::Align;
    if (!m_keepTabs->isChecked())
        bits |= direct::Tabs;
    return bits;
}

} // namespace qb
