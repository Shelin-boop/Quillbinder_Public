#include "ui/AlignButtons.h"

#include "core/ItemIcons.h"
#include "core/ProjectTypes.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>

namespace qb {

namespace {
struct Choice {
    const char *name; // object name of the button (the tests find it by this)
    const char *icon;
    Qt::Alignment align;
};
const Choice kChoices[] = {{"left", "format-justify-left", Qt::AlignLeft},
                           {"center", "format-justify-center", Qt::AlignHCenter},
                           {"right", "format-justify-right", Qt::AlignRight},
                           {"justify", "format-justify-fill", Qt::AlignJustify}};
} // namespace

AlignButtons::AlignButtons(QWidget *parent) : QWidget(parent), m_group(new QButtonGroup(this))
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    const QString tips[] = {tr("Align left"), tr("Centered"), tr("Align right"), tr("Justified")};
    int i = 0;
    for (const Choice &c : kChoices) {
        auto *b = new QToolButton(this);
        b->setObjectName(QLatin1String(c.name));
        b->setIcon(icons::action(QLatin1String(c.icon)));
        b->setToolTip(tips[i]);
        b->setAccessibleName(tips[i]);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setIconSize(QSize(18, 18));
        m_group->addButton(b, i);
        row->addWidget(b);
        ++i;
    }
    row->addStretch(1);
    m_group->setExclusive(true);
    m_group->button(0)->setChecked(true);
    connect(m_group, &QButtonGroup::idClicked, this, [this] { emit alignmentChanged(alignment()); });
}

Qt::Alignment AlignButtons::alignment() const
{
    const int id = m_group->checkedId();
    return id >= 0 && id < 4 ? kChoices[id].align : Qt::AlignLeft;
}

void AlignButtons::setAlignment(Qt::Alignment a)
{
    a = bodyAlignment(a);
    for (int i = 0; i < 4; ++i)
        if (kChoices[i].align == a)
            m_group->button(i)->setChecked(true);
}

} // namespace qb
