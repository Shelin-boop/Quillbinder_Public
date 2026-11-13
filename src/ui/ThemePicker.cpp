#include "ui/ThemePicker.h"

#include "ui/ProjectLook.h"
#include "ui/Theme.h"
#include "ui/ThemeDialogs.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QEvent>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace qb {

namespace {

const char *kFilterKey = "window/themeFilter";

// the list is filled again just before it opens: add-ons and the theme manager add and remove themes at any time
class ThemeCombo : public QComboBox
{
public:
    explicit ThemeCombo(ThemePicker *owner) : QComboBox(owner), m_owner(owner) {}
    void showPopup() override
    {
        m_owner->refill();
        QComboBox::showPopup();
    }

private:
    ThemePicker *m_owner;
};

QString filterName(ThemePicker::Filter f)
{
    return f == ThemePicker::Bright ? QStringLiteral("bright") : f == ThemePicker::Dark ? QStringLiteral("dark") : QStringLiteral("both");
}

// watches the toolbar: the theme list goes out of the way of the quick search field and comes back when there is room
class GiveWay : public QObject
{
public:
    GiveWay(QToolBar *bar, QAction *own, QWidget *keep) : QObject(bar), m_bar(bar), m_own(own), m_keep(keep)
    {
        bar->installEventFilter(this);
    }

    bool eventFilter(QObject *, QEvent *e) override
    {
        if ((e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest || e->type() == QEvent::Show) && !m_queued) {
            m_queued = true;
            QTimer::singleShot(0, this, [this] {
                m_queued = false;
                look();
            });
        }
        return false;
    }

private:
    void look()
    {
        if (!m_bar || !m_own || !m_keep || !m_bar->isVisible())
            return;
        // only when both are on the toolbar
        bool keepOnBar = false;
        for (QAction *a : m_bar->actions())
            keepOnBar = keepOnBar || m_bar->widgetForAction(a) == m_keep;
        if (!keepOnBar || !m_bar->actions().contains(m_own))
            return;
        if (m_own->isVisible()) {
            if (!m_keep->isVisible()) { // the search went into the overflow menu
                QWidget *w = m_bar->widgetForAction(m_own);
                m_need = (w ? w->sizeHint().width() : 300) + 24;
                m_hiddenAt = m_bar->width();
                m_own->setVisible(false);
            }
        } else if (m_bar->width() >= m_hiddenAt + m_need) {
            m_own->setVisible(true); // if the search goes again, the next look() hides the list again
        }
    }

    QPointer<QToolBar> m_bar;
    QPointer<QAction> m_own;
    QPointer<QWidget> m_keep;
    int m_hiddenAt = 0, m_need = 0;
    bool m_queued = false;
};

} // namespace

void ThemePicker::giveWay(QToolBar *bar, QAction *own, QWidget *keep)
{
    if (!bar || !own || !keep)
        return;
    for (QObject *o : bar->children())
        if (dynamic_cast<GiveWay *>(o))
            return; // once per toolbar
    new GiveWay(bar, own, keep);
}

ThemePicker::ThemePicker(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("themePicker"));
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    const struct {
        Filter f;
        const char *name;
        QString text, tip;
    } buttons[] = {
        {Bright, "themeFilterBright", tr("Bright"), tr("Show the bright themes")},
        {Dark, "themeFilterDark", tr("Dark"), tr("Show the dark themes")},
        {Both, "themeFilterBoth", tr("Both"), tr("Show all themes")},
    };
    for (const auto &b : buttons) {
        auto *t = new QToolButton(this);
        t->setObjectName(QLatin1String(b.name));
        t->setText(b.text);
        t->setToolTip(b.tip);
        t->setCheckable(true);
        t->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_group->addButton(t, int(b.f));
        h->addWidget(t);
    }
    h->addSpacing(6);
    m_combo = new ThemeCombo(this);
    m_combo->setObjectName(QStringLiteral("themeCombo"));
    m_combo->setToolTip(tr("The theme of the open project and of new projects (as Window > Themes)"));
    m_combo->setAccessibleName(tr("Theme"));
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_combo->setMinimumContentsLength(16);
    h->addWidget(m_combo);

    const QString saved = QSettings().value(QLatin1String(kFilterKey)).toString();
    m_filter = saved == QLatin1String("bright") ? Bright : saved == QLatin1String("dark") ? Dark : Both;
    if (QAbstractButton *b = m_group->button(int(m_filter)))
        b->setChecked(true);
    // the chosen one in bold: a checked button alone is hard to see in some dark themes
    auto boldChecked = [this] {
        for (QAbstractButton *b : m_group->buttons()) {
            QFont f = b->font();
            f.setBold(b->isChecked());
            b->setFont(f);
        }
    };
    connect(m_group, &QButtonGroup::buttonToggled, this, boldChecked);
    boldChecked();
    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) { setFilter(Filter(id)); });
    connect(m_combo, qOverload<int>(&QComboBox::activated), this, &ThemePicker::picked);
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, &ThemePicker::refill);
    refill();
}

void ThemePicker::setFilter(Filter f)
{
    if (QAbstractButton *b = m_group->button(int(f)); b && !b->isChecked())
        b->setChecked(true);
    if (f == m_filter)
        return;
    m_filter = f;
    QSettings().setValue(QLatin1String(kFilterKey), filterName(f));
    refill();
}

QStringList ThemePicker::shownIds() const
{
    QStringList out;
    for (int i = 0; i < m_combo->count(); ++i)
        out << m_combo->itemData(i).toString();
    return out;
}

void ThemePicker::refill()
{
    if (m_filling)
        return;
    m_filling = true;
    const QString current = look::active().theme;
    m_combo->clear();
    theme::Kind last = theme::Kind::BuiltIn;
    for (const theme::Entry &e : theme::available()) {
        const theme::Brightness b = theme::brightnessOf(e.id);
        if (m_filter == Bright && b == theme::Brightness::Dark)
            continue;
        if (m_filter == Dark && b == theme::Brightness::Bright)
            continue;
        if (e.kind != last && m_combo->count() > 0)
            m_combo->insertSeparator(m_combo->count());
        last = e.kind;
        m_combo->addItem(e.name, e.id);
    }
    const int at = m_combo->findData(current);
    // the theme in use is not among the shown ones: the list says what it offers instead
    m_combo->setPlaceholderText(m_filter == Bright ? tr("Choose a bright theme")
                                : m_filter == Dark ? tr("Choose a dark theme")
                                                   : tr("Choose a theme"));
    m_combo->setCurrentIndex(at);
    m_filling = false;
}

void ThemePicker::picked(int index)
{
    const QString id = m_combo->itemData(index).toString();
    if (id.isEmpty() || id == look::active().theme)
        return;
    themeui::choose(window(), id);
    refill(); // also when the question was cancelled: the list shows the theme in use again
}

} // namespace qb
