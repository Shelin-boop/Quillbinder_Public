#include "ui/WindowCare.h"

#include "ui/HintLabel.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLayout>
#include <QEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScreen>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

namespace qb {
namespace windowcare {

namespace {

const char *kOwner = "qbCareOwner";
const char *kFocus = "qbCareFocus";
const char *kBusy = "qbCarePlaceholderBusy";

// room for a title bar and the frame when the window has none yet (no window manager has framed it)
constexpr int kTitleRoom = 34;
constexpr int kSideRoom = 8;

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

bool isField(const QObject *o)
{
    return qobject_cast<const QLineEdit *>(o) || qobject_cast<const QTextEdit *>(o) || qobject_cast<const QPlainTextEdit *>(o);
}

bool isDialogWindow(const QWidget *w)
{
    if (!w || !w->isWindow())
        return false;
    const Qt::WindowType t = w->windowType();
    return (t == Qt::Dialog || t == Qt::Sheet) && qobject_cast<const QDialog *>(w);
}

void giveBack(QPointer<QWidget> owner, QPointer<QWidget> focus)
{
    if (!owner || !owner->isVisible() || QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return;
    // a window of the program has the keyboard: the window manager (or the dialog's own action) placed it
    if (QWidget *f = QApplication::focusWidget(); f && f->window()->isVisible())
        return;
    QWidget *target = focus && focus->isVisible() && focus->isEnabled() && focus->window() == owner ? focus.data()
                                                                                                   : owner->focusWidget();
    owner->activateWindow();
    if (target)
        target->setFocus(Qt::ActiveWindowFocusReason);
}

class Filter : public QObject
{
public:
    using QObject::QObject;

    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::Show:
            if (auto *w = qobject_cast<QWidget *>(o); w && isDialogWindow(w))
                shown(w);
            break;
        case QEvent::Hide:
            if (auto *w = qobject_cast<QWidget *>(o); w && isDialogWindow(w))
                hidden(w);
            break;
        case QEvent::Polish:
        case QEvent::PaletteChange:
            if (isField(o) && !o->property(kBusy).toBool())
                readablePlaceholder(static_cast<QWidget *>(o));
            break;
        default: break;
        }
        return QObject::eventFilter(o, e);
    }

private:
    static void shown(QWidget *d)
    {
        // UR-02: never taller or wider than the screen can show
        const QRect area = usableArea(d);
        if (area.isValid() && (d->width() + 2 * kSideRoom > area.width() || d->height() + kTitleRoom > area.height()))
            fitInto(d, area);
        // UR-01: where the keyboard was before
        QWidget *owner = d->parentWidget() ? d->parentWidget()->window() : QApplication::activeWindow();
        if (!owner || owner == d)
            return;
        QWidget *focus = QApplication::focusWidget();
        if (!focus || focus->window() != owner)
            focus = owner->focusWidget();
        d->setProperty(kOwner, QVariant::fromValue<QObject *>(owner));
        d->setProperty(kFocus, QVariant::fromValue<QObject *>(focus));
    }

    static void hidden(QWidget *d)
    {
        QPointer<QWidget> owner = qobject_cast<QWidget *>(d->property(kOwner).value<QObject *>());
        QPointer<QWidget> focus = qobject_cast<QWidget *>(d->property(kFocus).value<QObject *>());
        if (!owner)
            return;
        // after the dialog's own clean-up and whatever its result started; again a little later: without a window
        // manager the display server takes the keyboard from the program after the dialog's window is gone, and Qt
        // learns of it only then (a non-modal dialog such as Project Targets)
        QTimer::singleShot(0, owner, [owner, focus] { giveBack(owner, focus); });
        QTimer::singleShot(250, owner, [owner, focus] { giveBack(owner, focus); });
    }
};

} // namespace

void install()
{
    static bool done = false;
    if (done || !qApp)
        return;
    done = true;
    qApp->installEventFilter(new Filter(qApp));
    // fields made before the filter came
    for (QWidget *w : QApplication::allWidgets())
        if (isField(w))
            readablePlaceholder(w);
}

QRect usableArea(const QWidget *w)
{
    QScreen *s = w ? w->screen() : nullptr;
    if (!s)
        s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry() : QRect();
}

bool fitInto(QWidget *window, const QRect &area)
{
    if (!window || !area.isValid())
        return false;
    // the frame a window manager adds (known once the window was framed), else room for one
    int top = kTitleRoom, side = kSideRoom;
    if (window->isVisible()) {
        const QRect fr = window->frameGeometry(), g = window->geometry();
        if (fr != g) {
            top = (g.top() - fr.top()) + (fr.bottom() - g.bottom());
            side = qMax(g.left() - fr.left(), fr.right() - g.right());
        }
    }
    const QSize least = window->minimumSizeHint().expandedTo(window->minimumSize());
    const int maxW = area.width() - 2 * side, maxH = area.height() - top;
    const int width = qMax(least.width(), qMin(window->width(), maxW));
    const int height = qMax(least.height(), qMin(window->height(), maxH));
    if (width != window->width() || height != window->height())
        window->resize(width, height);
    // inside the area, the title bar first
    QPoint pos = window->pos();
    pos.setX(qBound(area.left() + side, pos.x(), qMax(area.left() + side, area.right() - side - width)));
    pos.setY(qBound(area.top() + top - side, pos.y(), qMax(area.top() + top - side, area.bottom() - height)));
    if (pos != window->pos())
        window->move(pos);
    return width + 2 * side <= area.width() && height + top <= area.height();
}

void readablePlaceholder(QWidget *field)
{
    if (!field)
        return;
    QPalette p = field->palette();
    bool changed = false;
    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive}) {
        const QColor text = p.color(g, QPalette::Text);
        const QColor back = p.color(g, QPalette::Base);
        if (hintlabel::contrast(p.color(g, QPalette::PlaceholderText), back) >= 4.5)
            continue;
        QColor shown = text;
        // toward the background as far as the contrast allows (at most 45 % of the way, so it still looks like a hint)
        for (double t = 0.45; t > 0.0; t -= 0.05) {
            const QColor c = mix(text, back, t);
            if (hintlabel::contrast(c, back) >= 4.5) {
                shown = c;
                break;
            }
        }
        p.setColor(g, QPalette::PlaceholderText, shown);
        changed = true;
    }
    if (!changed)
        return;
    field->setProperty(kBusy, true);
    field->setPalette(p);
    field->setProperty(kBusy, false);
}

void addCloseButton(QDialog *dialog)
{
    if (!dialog || !dialog->layout() || dialog->findChild<QDialogButtonBox *>(QStringLiteral("windowClose")))
        return;
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    box->setObjectName(QStringLiteral("windowClose"));
    box->setContentsMargins(8, 4, 8, 8);
    QObject::connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->layout()->addWidget(box);
}

} // namespace windowcare
} // namespace qb
