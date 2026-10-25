#include "ui/FilterBar.h"
#include "core/ItemIcons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

static QIcon chip(const QColor &c)
{
    QPixmap pm(12, 12);
    pm.fill(c.isValid() ? c : QColor(Qt::transparent));
    return QIcon(pm);
}

FilterBar::FilterBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("filterBar"));
    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(6, 3, 6, 3);
    vl->setSpacing(3);
    auto *row = new QHBoxLayout;
    m_disclosure = new QToolButton(this);
    m_disclosure->setObjectName(QStringLiteral("filterOptionsButton"));
    m_disclosure->setArrowType(Qt::RightArrow);
    m_disclosure->setAutoRaise(true);
    m_disclosure->setCheckable(true);
    m_disclosure->setToolTip(tr("Filter options"));
    m_edit = new QLineEdit(this);
    m_edit->setObjectName(QStringLiteral("filterField"));
    m_edit->setPlaceholderText(tr("Filter"));
    m_edit->setClearButtonEnabled(true);
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("filterCount"));
    auto *doneButton = new QPushButton(tr("Done"), this);
    doneButton->setObjectName(QStringLiteral("filterDone"));
    doneButton->setToolTip(tr("Remove the filter and show all documents again"));
    row->addWidget(m_disclosure);
    row->addWidget(m_edit, 1);
    row->addWidget(m_count);
    row->addWidget(doneButton);
    vl->addLayout(row);

    m_options = new QWidget(this);
    auto *ol = new QVBoxLayout(m_options);
    ol->setContentsMargins(22, 0, 0, 2);
    ol->setSpacing(3);
    auto *r1 = new QHBoxLayout;
    r1->addWidget(new QLabel(tr("Search in:"), m_options));
    m_inTitle = new QCheckBox(tr("Titles"), m_options);
    m_inSynopsis = new QCheckBox(tr("Synopses"), m_options);
    m_inKeywords = new QCheckBox(tr("Keywords"), m_options);
    m_inText = new QCheckBox(tr("Text"), m_options);
    m_inTitle->setChecked(true);
    m_inSynopsis->setChecked(true);
    m_invert = new QCheckBox(tr("Show what does not match"), m_options);
    for (QCheckBox *c : {m_inTitle, m_inSynopsis, m_inKeywords, m_inText})
        r1->addWidget(c);
    r1->addStretch(1);
    r1->addWidget(m_invert);
    ol->addLayout(r1);
    auto *r2 = new QHBoxLayout;
    m_label = new QComboBox(m_options);
    m_status = new QComboBox(m_options);
    m_keyword = new QComboBox(m_options);
    m_compile = new QComboBox(m_options);
    m_label->setObjectName(QStringLiteral("filterLabel"));
    m_status->setObjectName(QStringLiteral("filterStatus"));
    m_keyword->setObjectName(QStringLiteral("filterKeyword"));
    m_compile->setObjectName(QStringLiteral("filterCompile"));
    m_compile->addItem(tr("Included or not"), 0);
    m_compile->addItem(tr("Included in compile"), 1);
    m_compile->addItem(tr("Not included in compile"), 2);
    for (QComboBox *c : {m_label, m_status, m_keyword, m_compile}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        r2->addWidget(c);
    }
    r2->addStretch(1);
    ol->addLayout(r2);
    vl->addWidget(m_options);
    m_options->hide();
    setAutoFillBackground(true);

    connect(m_disclosure, &QToolButton::toggled, this, &FilterBar::setOptionsVisible);
    connect(m_edit, &QLineEdit::textChanged, this, &FilterBar::emitChanged);
    for (QCheckBox *c : {m_inTitle, m_inSynopsis, m_inKeywords, m_inText, m_invert})
        connect(c, &QCheckBox::toggled, this, &FilterBar::emitChanged);
    for (QComboBox *c : {m_label, m_status, m_keyword, m_compile})
        connect(c, &QComboBox::currentIndexChanged, this, &FilterBar::emitChanged);
    connect(doneButton, &QPushButton::clicked, this, &FilterBar::done);
    fillLists();
}

void FilterBar::setProject(Project *p)
{
    m_project = p;
    if (p) {
        connect(p, &Project::settingsChanged, this, &FilterBar::fillLists, Qt::UniqueConnection);
    }
    fillLists();
}

void FilterBar::fillLists()
{
    const ItemFilter cur = filter();
    m_filling = true;
    m_label->clear();
    m_status->clear();
    m_keyword->clear();
    m_label->addItem(tr("Any Label"), -2);
    m_status->addItem(tr("Any Status"), -2);
    m_keyword->addItem(tr("Any Keyword"), QString());
    if (m_project) {
        m_label->setItemText(0, tr("Any %1").arg(m_project->labelTitle));
        m_status->setItemText(0, tr("Any %1").arg(m_project->statusTitle));
        m_label->addItem(tr("No %1").arg(m_project->labelTitle), -1);
        for (const Label &l : m_project->labels)
            m_label->addItem(chip(l.color), l.name, l.id);
        m_status->addItem(tr("No %1").arg(m_project->statusTitle), -1);
        for (const Status &s : m_project->statuses)
            m_status->addItem(s.name, s.id);
        for (const Keyword &k : m_project->keywords)
            m_keyword->addItem(chip(k.color), k.name, k.id);
    }
    m_label->setCurrentIndex(qMax(0, m_label->findData(cur.label)));
    m_status->setCurrentIndex(qMax(0, m_status->findData(cur.status)));
    m_keyword->setCurrentIndex(qMax(0, m_keyword->findData(cur.keyword)));
    m_filling = false;
}

ItemFilter FilterBar::filter() const
{
    ItemFilter f;
    f.text = m_edit->text();
    f.inTitle = m_inTitle->isChecked();
    f.inSynopsis = m_inSynopsis->isChecked();
    f.inKeywords = m_inKeywords->isChecked();
    f.inText = m_inText->isChecked();
    f.invert = m_invert->isChecked();
    f.label = m_label->count() ? m_label->currentData().toInt() : -2;
    f.status = m_status->count() ? m_status->currentData().toInt() : -2;
    f.keyword = m_keyword->currentData().toString();
    f.compile = m_compile->currentData().toInt();
    return f;
}

void FilterBar::setFilter(const ItemFilter &f)
{
    m_filling = true;
    m_edit->setText(f.text);
    m_inTitle->setChecked(f.inTitle);
    m_inSynopsis->setChecked(f.inSynopsis);
    m_inKeywords->setChecked(f.inKeywords);
    m_inText->setChecked(f.inText);
    m_invert->setChecked(f.invert);
    m_label->setCurrentIndex(qMax(0, m_label->findData(f.label)));
    m_status->setCurrentIndex(qMax(0, m_status->findData(f.status)));
    m_keyword->setCurrentIndex(qMax(0, m_keyword->findData(f.keyword)));
    m_compile->setCurrentIndex(qMax(0, m_compile->findData(f.compile)));
    m_filling = false;
    emitChanged();
}

void FilterBar::focusField()
{
    m_edit->setFocus();
    m_edit->selectAll();
}

bool FilterBar::optionsVisible() const { return !m_options->isHidden(); }

void FilterBar::setOptionsVisible(bool on)
{
    m_options->setVisible(on);
    m_disclosure->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    QSignalBlocker b(m_disclosure);
    m_disclosure->setChecked(on);
}

void FilterBar::setCount(int shown, int total)
{
    m_count->setText(filter().isActive() ? tr("%1 of %2").arg(shown).arg(total) : QString());
}

void FilterBar::emitChanged()
{
    if (!m_filling)
        emit filterChanged(filter());
}

} // namespace qb
