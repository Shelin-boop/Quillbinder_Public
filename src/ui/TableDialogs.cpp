// The windows of tables and pictures (0.62); see TableDialogs.h.
#include "ui/TableDialogs.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace qb {

namespace {
QIcon swatch(const QColor &c)
{
    QPixmap pm(28, 14);
    pm.fill(c.isValid() ? c : QColor(Qt::transparent));
    if (!c.isValid()) {
        QPainter p(&pm);
        p.setPen(Qt::gray);
        p.drawRect(0, 0, 27, 13);
        p.drawLine(0, 13, 27, 0);
    }
    return QIcon(pm);
}
} // namespace

TableDialog::TableDialog(const tables::Spec &spec, int columns, bool newTable, QWidget *parent) : QDialog(parent), m_base(spec)
{
    setObjectName(newTable ? QStringLiteral("insertTableDialog") : QStringLiteral("tablePropertiesDialog"));
    setWindowTitle(newTable ? tr("Insert Table") : tr("Table Properties"));
    auto *v = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    v->addLayout(form);
    if (newTable) {
        auto *size = new QHBoxLayout;
        m_rows = new QSpinBox(this);
        m_rows->setObjectName(QStringLiteral("tableRows"));
        m_rows->setRange(1, 200);
        m_rows->setValue(3);
        m_cols = new QSpinBox(this);
        m_cols->setObjectName(QStringLiteral("tableColumns"));
        m_cols->setRange(1, 30);
        m_cols->setValue(qMax(1, columns));
        size->addWidget(m_rows);
        size->addWidget(new QLabel(tr("rows,"), this));
        size->addWidget(m_cols);
        size->addWidget(new QLabel(tr("columns"), this));
        size->addStretch(1);
        form->addRow(tr("Size:"), size);
    }
    m_style = new QComboBox(this);
    m_style->setObjectName(QStringLiteral("tableStyle"));
    for (const auto &st : tables::styles())
        m_style->addItem(st.second, st.first);
    m_style->addItem(tr("My own"), QStringLiteral("custom"));
    form->addRow(tr("Design:"), m_style);

    auto *lines = new QGroupBox(tr("Lines"), this);
    auto *lf = new QFormLayout(lines);
    auto *borderRow = new QHBoxLayout;
    m_border = new QDoubleSpinBox(lines);
    m_border->setObjectName(QStringLiteral("tableBorder"));
    m_border->setRange(0, 6);
    m_border->setSingleStep(0.5);
    m_border->setSuffix(tr(" px"));
    m_borderColor = new QPushButton(tr("Color…"), lines);
    m_borderColor->setObjectName(QStringLiteral("tableBorderColor"));
    borderRow->addWidget(m_border);
    borderRow->addWidget(m_borderColor);
    borderRow->addStretch(1);
    lf->addRow(tr("Width:"), borderRow);
    m_linesOnly = new QCheckBox(tr("Only between the rows (none at the sides)"), lines);
    m_linesOnly->setObjectName(QStringLiteral("tableLinesOnly"));
    lf->addRow(m_linesOnly);
    v->addWidget(lines);

    auto *cells = new QGroupBox(tr("Cells"), this);
    auto *cf = new QFormLayout(cells);
    m_padding = new QDoubleSpinBox(cells);
    m_padding->setObjectName(QStringLiteral("tablePadding"));
    m_padding->setRange(0, 30);
    m_padding->setSuffix(tr(" px"));
    cf->addRow(tr("Space inside a cell:"), m_padding);
    m_spacing = new QDoubleSpinBox(cells);
    m_spacing->setObjectName(QStringLiteral("tableSpacing"));
    m_spacing->setRange(0, 30);
    m_spacing->setSuffix(tr(" px"));
    m_spacing->setToolTip(tr("0: the lines of neighboring cells are one line"));
    cf->addRow(tr("Space between cells:"), m_spacing);
    auto *headerRow = new QHBoxLayout;
    m_header = new QSpinBox(cells);
    m_header->setObjectName(QStringLiteral("tableHeaderRows"));
    m_header->setRange(0, 10);
    m_headerColor = new QPushButton(cells);
    m_headerColor->setObjectName(QStringLiteral("tableHeaderColor"));
    m_headerColor->setText(tr("Color…"));
    headerRow->addWidget(m_header);
    headerRow->addWidget(m_headerColor);
    headerRow->addStretch(1);
    cf->addRow(tr("Header rows:"), headerRow);
    m_stripeColor = new QPushButton(tr("Color…"), cells);
    m_stripeColor->setObjectName(QStringLiteral("tableStripeColor"));
    cf->addRow(tr("Every other row:"), m_stripeColor);
    v->addWidget(cells);

    auto *place = new QGroupBox(tr("Place in the text"), this);
    auto *pf = new QFormLayout(place);
    m_width = new QSpinBox(place);
    m_width->setObjectName(QStringLiteral("tableWidth"));
    m_width->setRange(0, 100);
    m_width->setSuffix(tr(" % of the text width"));
    m_width->setSpecialValueText(tr("As wide as its contents"));
    pf->addRow(tr("Width:"), m_width);
    m_columns = new QLineEdit(place);
    m_columns->setObjectName(QStringLiteral("tableColumnWidths"));
    m_columns->setPlaceholderText(tr("All the same; or shares, for example 30, 70"));
    pf->addRow(tr("Column widths:"), m_columns);
    m_align = new QComboBox(place);
    m_align->setObjectName(QStringLiteral("tableAlign"));
    m_align->addItem(tr("Left"), int(Qt::AlignLeft));
    m_align->addItem(tr("Centered"), int(Qt::AlignHCenter));
    m_align->addItem(tr("Right"), int(Qt::AlignRight));
    pf->addRow(tr("Alignment:"), m_align);
    m_flow = new QComboBox(place);
    m_flow->setObjectName(QStringLiteral("tableFlow"));
    m_flow->addItem(tr("On its own lines"), 0);
    m_flow->addItem(tr("Left, the text flows beside it"), 1);
    m_flow->addItem(tr("Right, the text flows beside it"), 2);
    pf->addRow(tr("Text:"), m_flow);
    v->addWidget(place);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    fill(spec);
    connect(m_style, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (m_filling)
            return;
        const QString id = m_style->currentData().toString();
        if (id == QLatin1String("custom"))
            return;
        tables::Spec p = tables::preset(id);
        // the place in the text is not part of a design
        p.widthPercent = m_width->value();
        p.align = Qt::Alignment(m_align->currentData().toInt());
        p.flow = m_flow->currentData().toInt();
        p.columns = this->spec().columns;
        fill(p);
    });
    for (QDoubleSpinBox *sb : {m_border, m_padding, m_spacing})
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { own(); });
    connect(m_header, qOverload<int>(&QSpinBox::valueChanged), this, [this] { own(); });
    connect(m_linesOnly, &QCheckBox::toggled, this, [this] { own(); });
    auto pick = [this](QPushButton *b, QColor *c, bool canNone) {
        connect(b, &QPushButton::clicked, this, [this, b, c, canNone] {
            QColorDialog dlg(c->isValid() ? *c : QColor(Qt::white), this);
            dlg.setWindowTitle(tr("Color"));
            bool noneChosen = false;
            if (canNone) {
                auto *none = new QPushButton(tr("None"), &dlg);
                if (auto *box = dlg.findChild<QDialogButtonBox *>())
                    box->addButton(none, QDialogButtonBox::ResetRole);
                connect(none, &QPushButton::clicked, &dlg, [&dlg, &noneChosen] {
                    noneChosen = true;
                    dlg.reject();
                });
            }
            const int r = dlg.exec();
            if (r == QDialog::Accepted)
                *c = dlg.currentColor();
            else if (noneChosen)
                *c = QColor();
            else
                return;
            b->setIcon(swatch(*c));
            own();
        });
    };
    pick(m_borderColor, &m_border_c, false);
    pick(m_headerColor, &m_header_c, true);
    pick(m_stripeColor, &m_stripe_c, true);
}

void TableDialog::fill(const tables::Spec &s)
{
    m_filling = true;
    m_style->setCurrentIndex(qMax(0, m_style->findData(s.style)));
    m_border->setValue(s.border);
    m_border_c = s.borderColor;
    m_borderColor->setIcon(swatch(m_border_c));
    m_linesOnly->setChecked(s.linesOnly);
    m_padding->setValue(s.padding);
    m_spacing->setValue(s.spacing);
    m_header->setValue(s.headerRows);
    m_header_c = s.headerColor;
    m_headerColor->setIcon(swatch(m_header_c));
    m_stripe_c = s.stripeColor;
    m_stripeColor->setIcon(swatch(m_stripe_c));
    m_width->setValue(s.widthPercent);
    m_align->setCurrentIndex(s.align & Qt::AlignHCenter ? 1 : s.align & Qt::AlignRight ? 2 : 0);
    m_flow->setCurrentIndex(qBound(0, s.flow, 2));
    QStringList cols;
    for (double c : s.columns)
        cols << QString::number(c, 'g', 4);
    m_columns->setText(cols.join(QLatin1String(", ")));
    m_filling = false;
}

void TableDialog::own()
{
    if (m_filling)
        return;
    m_filling = true;
    m_style->setCurrentIndex(m_style->findData(QStringLiteral("custom")));
    m_filling = false;
}

tables::Spec TableDialog::spec() const
{
    tables::Spec s;
    s.style = m_style->currentData().toString();
    s.border = m_border->value();
    s.borderColor = m_border_c.isValid() ? m_border_c : QColor(0x80, 0x80, 0x80);
    s.linesOnly = m_linesOnly->isChecked();
    s.padding = m_padding->value();
    s.spacing = m_spacing->value();
    s.headerRows = m_header->value();
    s.headerColor = m_header_c;
    s.stripeColor = m_stripe_c;
    s.widthPercent = m_width->value();
    s.align = Qt::Alignment(m_align->currentData().toInt());
    s.flow = m_flow->currentData().toInt();
    for (const QString &part : m_columns->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const double v = part.trimmed().toDouble(&ok);
        if (ok && v > 0)
            s.columns << v;
    }
    return s;
}

int TableDialog::rows() const { return m_rows ? m_rows->value() : 0; }
int TableDialog::columns() const { return m_cols ? m_cols->value() : 0; }

// ------------------------------------------------------------------ pictures

PictureDialog::PictureDialog(const QSize &natural, int width, figures::Position pos, const QString &caption,
                             const QString &description, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("pictureDialog"));
    setWindowTitle(tr("Picture"));
    auto *form = new QFormLayout(this);
    auto *sizeRow = new QHBoxLayout;
    m_width = new QSpinBox(this);
    m_width->setObjectName(QStringLiteral("pictureWidth"));
    m_width->setRange(16, 4000);
    m_width->setSuffix(tr(" px"));
    m_width->setValue(width);
    auto *original = new QPushButton(tr("Original Size"), this);
    sizeRow->addWidget(m_width);
    sizeRow->addWidget(original);
    sizeRow->addStretch(1);
    form->addRow(tr("Width on screen:"), sizeRow);
    auto *info = new QLabel(tr("Original size: %1 × %2 px. The height follows the width.").arg(natural.width()).arg(natural.height()), this);
    info->setWordWrap(true);
    form->addRow(info);
    connect(original, &QPushButton::clicked, this, [this, natural] { m_width->setValue(natural.width()); });

    m_pos = new QComboBox(this);
    m_pos->setObjectName(QStringLiteral("picturePosition"));
    m_pos->addItem(tr("In the line, like a letter"), int(figures::InLine));
    m_pos->addItem(tr("Left, the text flows around it"), int(figures::Left));
    m_pos->addItem(tr("Right, the text flows around it"), int(figures::Right));
    m_pos->addItem(tr("Centered on its own lines"), int(figures::Centered));
    m_pos->setCurrentIndex(m_pos->findData(int(pos)));
    form->addRow(tr("Position:"), m_pos);
    m_caption = new QLineEdit(caption, this);
    m_caption->setObjectName(QStringLiteral("pictureCaption"));
    m_caption->setPlaceholderText(tr("None"));
    form->addRow(tr("Caption:"), m_caption);
    auto *capHint = new QLabel(tr("Shown under the picture and compiled with it; a picture in the line has none."), this);
    capHint->setWordWrap(true);
    capHint->setEnabled(false);
    form->addRow(capHint);
    m_desc = new QPlainTextEdit(description, this);
    m_desc->setObjectName(QStringLiteral("pictureDescription"));
    m_desc->setPlaceholderText(tr("What the picture shows, in a sentence or two"));
    m_desc->setFixedHeight(fontMetrics().height() * 4 + 12);
    form->addRow(tr("Description:"), m_desc);
    auto *descHint = new QLabel(tr("Not shown in the text: e-books, Word files and screen readers keep it with the picture "
                                   "(alt text), and it appears when the mouse rests on the picture."),
                                this);
    descHint->setWordWrap(true);
    descHint->setEnabled(false);
    form->addRow(descHint);
    auto enableCaption = [this] {
        m_caption->setEnabled(m_pos->currentData().toInt() != int(figures::InLine));
    };
    connect(m_pos, qOverload<int>(&QComboBox::currentIndexChanged), this, enableCaption);
    enableCaption();
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(bb);
    resize(520, sizeHint().height());
}

int PictureDialog::pictureWidth() const { return m_width->value(); }
figures::Position PictureDialog::position() const { return figures::Position(m_pos->currentData().toInt()); }
QString PictureDialog::caption() const { return m_pos->currentData().toInt() == int(figures::InLine) ? QString() : m_caption->text().trimmed(); }
QString PictureDialog::description() const { return m_desc->toPlainText().trimmed(); }

} // namespace qb
