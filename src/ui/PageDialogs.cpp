#include "ui/PageDialogs.h"
#include "ui/AppSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace qb {

PageSetupDialog::PageSetupDialog(const PageSetup &current, QWidget *parent) : QDialog(parent), m_metric(units::metric())
{
    setWindowTitle(tr("Page Size and Margins"));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("The paper of the page view. Compiled files take their page from the compile format."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto *row = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    auto *paperBox = new QGroupBox(tr("Paper"), this);
    auto *pf = new QFormLayout(paperBox);
    m_paper = new QComboBox(paperBox);
    m_paper->setObjectName(QStringLiteral("pagePaper"));
    for (const PageSetup::Paper &p : PageSetup::papers())
        m_paper->addItem(p.name, p.name);
    m_paper->addItem(tr("Custom"), QStringLiteral("Custom"));
    pf->addRow(tr("&Size:"), m_paper);
    const QString suffix = m_metric ? tr(" cm") : tr(" in");
    auto spin = [&](QWidget *parent, const char *name, double max) {
        auto *s = new QDoubleSpinBox(parent);
        s->setObjectName(QLatin1String(name));
        s->setDecimals(2);
        s->setRange(0, max);
        s->setSingleStep(m_metric ? 0.1 : 0.05);
        s->setSuffix(suffix);
        return s;
    };
    m_width = spin(paperBox, "pageWidth", m_metric ? 100 : 40);
    m_height = spin(paperBox, "pageHeight", m_metric ? 150 : 60);
    pf->addRow(tr("&Width:"), m_width);
    pf->addRow(tr("&Height:"), m_height);
    m_portrait = new QRadioButton(tr("&Portrait"), paperBox);
    m_landscape = new QRadioButton(tr("&Landscape"), paperBox);
    m_landscape->setObjectName(QStringLiteral("pageLandscape"));
    auto *orient = new QHBoxLayout;
    orient->addWidget(m_portrait);
    orient->addWidget(m_landscape);
    orient->addStretch(1);
    pf->addRow(tr("Orientation:"), orient);
    left->addWidget(paperBox);

    auto *marginBox = new QGroupBox(tr("Margins"), this);
    auto *mf = new QFormLayout(marginBox);
    m_top = spin(marginBox, "pageTop", m_metric ? 50 : 20);
    m_bottom = spin(marginBox, "pageBottom", m_metric ? 50 : 20);
    m_left = spin(marginBox, "pageLeft", m_metric ? 50 : 20);
    m_right = spin(marginBox, "pageRight", m_metric ? 50 : 20);
    mf->addRow(tr("&Top:"), m_top);
    mf->addRow(tr("&Bottom:"), m_bottom);
    mf->addRow(tr("L&eft:"), m_left);
    mf->addRow(tr("&Right:"), m_right);
    left->addWidget(marginBox);
    left->addStretch(1);
    row->addLayout(left);

    auto *right = new QVBoxLayout;
    m_preview = new QLabel(this);
    m_preview->setFixedSize(200, 230);
    m_preview->setAlignment(Qt::AlignCenter);
    m_info = new QLabel(this);
    m_info->setAlignment(Qt::AlignCenter);
    right->addWidget(m_preview);
    right->addWidget(m_info);
    right->addStretch(1);
    row->addLayout(right);
    lay->addLayout(row);

    m_newProjects = new QCheckBox(tr("Use this page for &new projects as well"), this);
    m_newProjects->setObjectName(QStringLiteral("pageNewProjects"));
    lay->addWidget(m_newProjects);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            [this] { setSetup(PageSetup::forLocale()); });

    connect(m_paper, qOverload<int>(&QComboBox::activated), this, [this] { paperChosen(); });
    for (QDoubleSpinBox *s : {m_width, m_height})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] {
            // a size typed by hand is a custom paper
            const int custom = m_paper->findData(QStringLiteral("Custom"));
            if (m_paper->currentIndex() != custom) {
                QSignalBlocker b(m_paper);
                m_paper->setCurrentIndex(custom);
            }
            updatePreview();
        });
    for (QDoubleSpinBox *s : {m_left, m_right, m_top, m_bottom})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updatePreview(); });
    connect(m_landscape, &QRadioButton::toggled, this, [this] { updatePreview(); });
    setSetup(current);
}

double PageSetupDialog::toUnit(double mm) const { return m_metric ? mm / 10.0 : mm / 25.4; }
double PageSetupDialog::fromUnit(double v) const { return m_metric ? v * 10.0 : v * 25.4; }

void PageSetupDialog::setSetup(const PageSetup &in)
{
    const PageSetup s = in.normalized();
    QSignalBlocker b1(m_paper), b2(m_width), b3(m_height), b4(m_left), b5(m_right), b6(m_top), b7(m_bottom),
        b8(m_landscape), b9(m_portrait);
    int idx = m_paper->findData(s.paper);
    if (idx < 0)
        idx = m_paper->findData(QStringLiteral("Custom"));
    m_paper->setCurrentIndex(idx);
    m_width->setValue(toUnit(s.width));
    m_height->setValue(toUnit(s.height));
    m_left->setValue(toUnit(s.left));
    m_right->setValue(toUnit(s.right));
    m_top->setValue(toUnit(s.top));
    m_bottom->setValue(toUnit(s.bottom));
    m_landscape->setChecked(s.landscape);
    m_portrait->setChecked(!s.landscape);
    updatePreview();
}

PageSetup PageSetupDialog::setup() const
{
    PageSetup s;
    s.paper = m_paper->currentData().toString();
    s.width = fromUnit(m_width->value());
    s.height = fromUnit(m_height->value());
    for (const PageSetup::Paper &p : PageSetup::papers())
        if (p.name == s.paper) { // exact size of a preset, not the rounded one of the fields
            s.width = p.w;
            s.height = p.h;
        }
    s.landscape = m_landscape->isChecked();
    s.left = fromUnit(m_left->value());
    s.right = fromUnit(m_right->value());
    s.top = fromUnit(m_top->value());
    s.bottom = fromUnit(m_bottom->value());
    return s.normalized();
}

bool PageSetupDialog::useForNewProjects() const { return m_newProjects->isChecked(); }

void PageSetupDialog::paperChosen()
{
    const QString name = m_paper->currentData().toString();
    for (const PageSetup::Paper &p : PageSetup::papers())
        if (p.name == name) {
            QSignalBlocker b1(m_width), b2(m_height);
            m_width->setValue(toUnit(p.w));
            m_height->setValue(toUnit(p.h));
        }
    updatePreview();
}

void PageSetupDialog::updatePreview()
{
    const PageSetup s = setup();
    const QSizeF size = s.sizeMm();
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(m_preview->size() * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    const double scale = qMin((m_preview->width() - 16) / size.width(), (m_preview->height() - 16) / size.height());
    const QRectF sheet((m_preview->width() - size.width() * scale) / 2, (m_preview->height() - size.height() * scale) / 2,
                       size.width() * scale, size.height() * scale);
    p.fillRect(sheet.translated(3, 3), QColor(0, 0, 0, 40));
    p.fillRect(sheet, Qt::white);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(sheet);
    const QRectF text = sheet.adjusted(s.left * scale, s.top * scale, -s.right * scale, -s.bottom * scale);
    p.setPen(QPen(QColor(90, 140, 210), 1, Qt::DashLine));
    p.drawRect(text);
    p.setPen(QColor(170, 170, 170));
    for (double y = text.top() + 5; y < text.bottom() - 2; y += 5)
        p.drawLine(QPointF(text.left() + 3, y), QPointF(text.right() - 3, y));
    p.end();
    m_preview->setPixmap(pm);
    const QString fmt = m_metric ? QStringLiteral("%1 × %2 cm") : QStringLiteral("%1 × %2 in");
    const QLocale loc;
    m_info->setText(tr("Text area: %1").arg(fmt.arg(loc.toString(toUnit(size.width() - s.left - s.right), 'f', 1),
                                                    loc.toString(toUnit(size.height() - s.top - s.bottom), 'f', 1))));
}

} // namespace qb
