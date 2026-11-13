#include "ui/OptionStorage.h"

#include "core/SettingsLayer.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPointer>
#include <QSizePolicy>
#include <QVariant>

namespace qb {
namespace optstore {

namespace {

const char *kKey = "qbStorageKey";
const char *kBase = "qbBaseToolTip";
const char *kBadge = "qbStorageBadge";
const char *kWhy = "qbStorageWhy";

QString joined(const QString &base, const QString &why)
{
    if (why.isEmpty())
        return base;
    return base.isEmpty() ? why : base + QStringLiteral("\n\n") + why;
}

QString tr(const char *s) { return QCoreApplication::translate("qb::optstore", s); }

// a filled circle with a white capital, drawn for the screen's scale
QPixmap badgePixmap(bool project, int size, qreal dpr)
{
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // green #2e7d32 and blue #1565c0: white letters on them have a contrast above 4.5:1
    p.setBrush(project ? QColor(0x2e, 0x7d, 0x32) : QColor(0x15, 0x65, 0xc0));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));
    QFont f = QApplication::font();
    f.setBold(true);
    f.setPixelSize(qMax(7, int(size * 0.62)));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, project ? QStringLiteral("P") : QStringLiteral("A"));
    return pm;
}

// shows and hides the badge with its control
class Follow : public QObject
{
public:
    Follow(QWidget *control, QLabel *badge) : QObject(badge), m_badge(badge) { control->installEventFilter(this); }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (m_badge && (e->type() == QEvent::ShowToParent || e->type() == QEvent::HideToParent))
            m_badge->setVisible(!static_cast<QWidget *>(o)->isHidden());
        if (m_badge && e->type() == QEvent::EnabledChange)
            m_badge->setEnabled(static_cast<QWidget *>(o)->isEnabled());
        return false;
    }

private:
    QPointer<QLabel> m_badge;
};

} // namespace

void mark(QWidget *control, const QString &key)
{
    if (!control || control->property(kKey).isValid())
        return;
    control->setProperty(kKey, key);
    control->setProperty(kBase, control->toolTip());
    auto *badge = new QLabel;
    badge->setObjectName(QStringLiteral("storageBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    control->setProperty(kBadge, QVariant::fromValue<QObject *>(badge));
    QWidget *parent = control->parentWidget();
    QLayout *lay = parent ? parent->layout() : nullptr;
    if (lay) {
        // the control moves into a holder that takes its place in the layout: the badge, then the control
        auto *holder = new QWidget(parent);
        holder->setObjectName(QStringLiteral("storageHolder"));
        auto *h = new QHBoxLayout;
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        QLayoutItem *old = lay->replaceWidget(control, holder);
        if (old) {
            delete old;
            holder->setLayout(h);
            h->addWidget(badge, 0, Qt::AlignVCenter);
            h->addWidget(control);
            const QSizePolicy::Policy hp = control->sizePolicy().horizontalPolicy();
            if (!(hp & QSizePolicy::GrowFlag))
                h->addStretch(1);
            holder->setSizePolicy(QSizePolicy::Preferred, control->sizePolicy().verticalPolicy());
            holder->setVisible(true);
            badge->setVisible(!control->isHidden());
            new Follow(control, badge);
            return;
        }
        delete h;
        delete holder;
    }
    // not in a layout this program can reach: the tool tip alone tells
    delete badge;
    control->setProperty(kBadge, QVariant());
}

QString keyOf(const QWidget *control)
{
    return control ? control->property(kKey).toString() : QString();
}

bool fixedToProgram(const QString &key)
{
    if (key == QLatin1String("look"))
        return false;
    if (key.endsWith(QLatin1String("/*")))
        return settingslayer::programOnly(key.chopped(1) + QStringLiteral("x"));
    return settingslayer::programOnly(key) || settingslayer::namesProgramOrPlace(key);
}

bool projectKeeps(const QString &key)
{
    if (key == QLatin1String("look"))
        return false;
    const QVariantMap own = settingslayer::projectValues();
    if (key.endsWith(QLatin1String("/*"))) {
        const QString prefix = key.chopped(1);
        for (auto it = own.cbegin(); it != own.cend(); ++it)
            if (it.key().startsWith(prefix))
                return true;
        return false;
    }
    return own.contains(key);
}

QString placeOf(const QString &key, const State &s)
{
    if (key == QLatin1String("look"))
        return s.projectOpen && s.lookThisProject ? QStringLiteral("P") : QStringLiteral("A");
    if (fixedToProgram(key) || !s.projectOpen || !s.projectFirst || !s.thisProject)
        return QStringLiteral("A");
    return projectKeeps(key) ? QStringLiteral("P") : QStringLiteral("A");
}

QString explanation(const QString &key, const State &s)
{
    if (key == QLatin1String("look")) {
        if (!s.projectOpen)
            return tr("Part of the look: kept by the program as the look for new projects (A). An open project keeps a "
                      "look of its own (P).");
        return s.lookThisProject
                   ? tr("Part of the look: kept with this project (P), as chosen in \"Show and change the look of\" above.")
                   : tr("Part of the look: kept by the program as the look for new projects (A), as chosen in \"Show and "
                        "change the look of\" above.");
    }
    if (fixedToProgram(key))
        return tr("Fixed: kept by the program for all projects (A). A project cannot keep a value of its own here.");
    if (!s.projectOpen)
        return tr("Depends on the setting: kept by the program for all projects (A). An open project can keep a value "
                  "of its own (P): \"Settings for: This project\".");
    if (!s.projectFirst)
        return tr("Depends on the setting: kept by the program for all projects (A). Projects' own values are not used "
                  "now (General > Which settings come first).");
    const bool own = projectKeeps(key);
    if (s.thisProject)
        return own ? tr("Depends on the setting: this project keeps a value of its own (P), used while it is open.")
                   : tr("Depends on the setting: this project uses the value for all projects (A). Change it here and "
                        "press OK to keep a value of its own (P).");
    return own ? tr("Depends on the setting: here you change the value for all projects (A). This project keeps a value "
                    "of its own (P), which it uses while it is open.")
               : tr("Depends on the setting: kept by the program for all projects (A). A project can keep a value of its "
                    "own (P): \"Settings for: This project\".");
}

void refresh(QWidget *root, const State &s)
{
    if (!root)
        return;
    const int size = qMax(14, root->fontMetrics().height());
    const qreal dpr = root->devicePixelRatioF();
    const QPixmap pPix = badgePixmap(true, size, dpr), aPix = badgePixmap(false, size, dpr);
    QList<QWidget *> all = root->findChildren<QWidget *>();
    for (QWidget *w : all) {
        const QString key = keyOf(w);
        if (key.isEmpty())
            continue;
        const bool project = placeOf(key, s) == QLatin1String("P");
        const QString why = explanation(key, s);
        w->setProperty(kWhy, why);
        w->setToolTip(joined(w->property(kBase).toString(), why));
        if (auto *badge = qobject_cast<QLabel *>(w->property(kBadge).value<QObject *>())) {
            badge->setPixmap(project ? pPix : aPix);
            badge->setToolTip(why);
            badge->setProperty("qbPlace", project ? QStringLiteral("P") : QStringLiteral("A"));
            badge->setAccessibleName(project ? tr("Kept in this project") : tr("Kept by the program for all projects"));
        }
    }
}

void setToolTip(QWidget *control, const QString &base)
{
    if (!control)
        return;
    if (keyOf(control).isEmpty()) {
        control->setToolTip(base);
        return;
    }
    control->setProperty(kBase, base);
    control->setToolTip(joined(base, control->property(kWhy).toString()));
}

} // namespace optstore
} // namespace qb
