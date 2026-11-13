#include "ui/ThemePicker.h"

#include "text/TextUtil.h"
#include "ui/Ask.h"
#include "ui/ProjectLook.h"
#include "ui/Theme.h"
#include "ui/ThemeDialogs.h"

#include <QActionGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace qb {

namespace {

const char *kBrightKey = "window/themeBright";
const char *kDarkKey = "window/themeDark";
int s_answerForTests = -3;

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

QString buttonLabel(ThemePicker::Button b)
{
    switch (b) {
    case ThemePicker::Bright: return ThemePicker::tr("Bright");
    case ThemePicker::Dark: return ThemePicker::tr("Dark");
    case ThemePicker::User1: return ThemePicker::tr("U1");
    case ThemePicker::User2: return ThemePicker::tr("U2");
    }
    return QString();
}

QString themeName(const QString &id)
{
    for (const theme::Entry &e : theme::available())
        if (e.id == id)
            return e.name;
    return id;
}

bool themeExists(const QString &id)
{
    theme::Theme t;
    return !id.isEmpty() && theme::load(id, &t);
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

void ThemePicker::setAnswerForTests(int answer) { s_answerForTests = answer; }

QString ThemePicker::userThemeName(Button b) { return b == User2 ? QStringLiteral("User_2") : QStringLiteral("User_1"); }

QString ThemePicker::userThemeId(Button b) { return QLatin1String("user:") + userThemeName(b); }

ThemePicker::ThemePicker(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("themePicker"));
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    const char *names[] = {"themeBright", "themeDark", "themeUser1", "themeUser2"};
    for (Button b : {Bright, Dark, User1, User2}) {
        auto *t = new QToolButton(this);
        t->setObjectName(QLatin1String(names[int(b)]));
        t->setText(buttonLabel(b));
        t->setCheckable(true); // checked (and bold) while its theme is the one shown
        t->setToolButtonStyle(Qt::ToolButtonTextOnly);
        t->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(t, &QToolButton::clicked, this, [this, b] {
            press(b);
            updateButtons(); // (a click toggles the button; what it shows is decided here)
        });
        connect(t, &QWidget::customContextMenuRequested, this, [this, b, t](const QPoint &pos) {
            QMenu menu;
            menu.setObjectName(QStringLiteral("themeButtonMenu"));
            fillButtonMenu(&menu, b);
            menu.exec(t->mapToGlobal(pos));
        });
        m_buttons[int(b)] = t;
        h->addWidget(t);
    }
    h->addSpacing(6);
    m_combo = new ThemeCombo(this);
    m_combo->setObjectName(QStringLiteral("themeCombo"));
    m_combo->setToolTip(tr("The theme of the open project and of new projects (as Window > Themes)"));
    m_combo->setAccessibleName(tr("Theme"));
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_combo->setMinimumContentsLength(14);
    m_combo->setPlaceholderText(tr("Choose a theme"));
    h->addWidget(m_combo);

    connect(m_combo, qOverload<int>(&QComboBox::activated), this, &ThemePicker::picked);
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, &ThemePicker::refill);
    refill();
}

QString ThemePicker::themeOf(Button b) const
{
    if (b == User1 || b == User2)
        return themeExists(userThemeId(b)) ? userThemeId(b) : QString();
    const QString assigned = QSettings().value(QLatin1String(b == Bright ? kBrightKey : kDarkKey)).toString();
    // a theme that is gone (an add-on removed, an own theme deleted): the standard again
    return themeExists(assigned) ? assigned : (b == Bright ? QStringLiteral("light") : QStringLiteral("dark"));
}

bool ThemePicker::assign(Button b, const QString &themeId)
{
    if ((b != Bright && b != Dark) || !themeExists(themeId))
        return false;
    QSettings().setValue(QLatin1String(b == Bright ? kBrightKey : kDarkKey), themeId);
    updateButtons();
    return true;
}

void ThemePicker::tell(const QString &text)
{
    if (auto *mw = qobject_cast<QMainWindow *>(window()))
        mw->statusBar()->showMessage(text, 8000);
}

// The look shown now (theme and the writer's own colors and fonts on top) written as the own theme of a U button.
bool ThemePicker::store(Button b)
{
    theme::Theme t = theme::currentLook();
    t.id = userThemeId(b); // (an own theme's id: saved over the file of that name)
    t.name = userThemeName(b);
    QString error;
    if (theme::saveUserTheme(t, &error).isEmpty()) {
        QMessageBox::warning(window(), tr("Themes"), tr("The look could not be saved in %1: %2").arg(buttonLabel(b), error));
        return false;
    }
    return true;
}

// Before a button shows its theme as it is: colors and fonts of the writer's own on top of the theme shown now are
// saved first, so a click never loses a look. An empty U button takes them without a question; with both taken the
// writer says where they go, or that they may go.
bool ThemePicker::keepOwnLook()
{
    const int own = themeui::ownChanges();
    if (own == 0)
        return true;
    for (Button u : {User1, User2})
        if (themeOf(u).isEmpty()) {
            if (!store(u))
                return false;
            tell(tr("The look you had was saved in %1 first.").arg(buttonLabel(u)));
            return true;
        }
    int answer = s_answerForTests;
    if (answer == -3) {
        QMessageBox box(QMessageBox::Question, tr("Change Theme"),
                        tr("The look you see has %1 of your own that are not saved in U1 or U2.")
                            .arg(text::plural(own, tr("color or font"), tr("colors and fonts"))),
                        QMessageBox::NoButton, window());
        box.setObjectName(QStringLiteral("themeKeepQuestion"));
        box.setInformativeText(tr("Save the look in U1 or U2 first (it replaces the look saved there), or switch without "
                                  "saving it?"));
        QPushButton *u1 = box.addButton(tr("Save in U1"), QMessageBox::ActionRole);
        QPushButton *u2 = box.addButton(tr("Save in U2"), QMessageBox::ActionRole);
        QPushButton *drop = box.addButton(tr("Switch Without Saving"), QMessageBox::DestructiveRole);
        QPushButton *cancel = box.addButton(QMessageBox::Cancel);
        // the look came from a U button and was changed since: back into that button; else nothing happens on Enter
        const QString now = look::active().theme;
        QPushButton *standard = now == userThemeId(User1) ? u1 : now == userThemeId(User2) ? u2 : cancel;
        box.setDefaultButton(standard);
        box.setEscapeButton(cancel);
        box.exec();
        answer = box.clickedButton() == u1 ? int(User1) : box.clickedButton() == u2 ? int(User2) : box.clickedButton() == drop ? -1 : -2;
    }
    if (answer == -1)
        return true;
    if (answer != int(User1) && answer != int(User2))
        return false;
    if (!store(Button(answer)))
        return false;
    tell(tr("The look you had was saved in %1 first.").arg(buttonLabel(Button(answer))));
    return true;
}

bool ThemePicker::saveLookIn(Button b, bool askFirst)
{
    if (b != User1 && b != User2)
        return false;
    const bool filled = !themeOf(b).isEmpty();
    if (filled && look::active().theme == userThemeId(b) && themeui::ownChanges() == 0) {
        tell(tr("%1 already holds the look you see.").arg(buttonLabel(b)));
        return false;
    }
    if (filled && askFirst
        && !ask::confirm(window(), tr("Themes"),
                         tr("%1 already holds a look. Replace it with the look you see now? The look saved there cannot be "
                            "brought back.")
                             .arg(buttonLabel(b)),
                         tr("Replace the Look in %1").arg(buttonLabel(b))))
        return false;
    if (!store(b))
        return false;
    // the own colors and fonts are part of the saved look now: it is shown as it is
    const bool ok = themeui::useAsItIs(window(), userThemeId(b));
    refill();
    tell(tr("The look you see is saved in %1. A click on %1 brings it back.").arg(buttonLabel(b)));
    return ok;
}

bool ThemePicker::press(Button b)
{
    if ((b == User1 || b == User2) && themeOf(b).isEmpty())
        return saveLookIn(b, false); // an empty button takes the look shown now
    const QString id = themeOf(b);
    if (id.isEmpty())
        return false;
    if (id == look::active().theme && themeui::ownChanges() == 0)
        return false; // it is shown already
    if (!keepOwnLook())
        return false;
    const bool ok = themeui::useAsItIs(window(), id);
    refill();
    return ok;
}

void ThemePicker::fillButtonMenu(QMenu *menu, Button b)
{
    if (b == User1 || b == User2) {
        QAction *save = menu->addAction(tr("Save the Current Look Here"), this, [this, b] { saveLookIn(b); });
        save->setObjectName(QStringLiteral("themeSaveHere"));
        QAction *show = menu->addAction(tr("Show This Look"), this, [this, b] { press(b); });
        show->setObjectName(QStringLiteral("themeShowThis"));
        show->setEnabled(!themeOf(b).isEmpty());
        return;
    }
    QAction *head = menu->addAction(b == Bright ? tr("Theme of the Button Bright") : tr("Theme of the Button Dark"));
    head->setEnabled(false);
    menu->addSeparator();
    auto *group = new QActionGroup(menu);
    const QString assigned = themeOf(b);
    for (const theme::Entry &e : theme::available()) {
        const theme::Brightness kind = theme::brightnessOf(e.id);
        // a bright theme for Bright, a dark one for Dark; a theme that follows the desktop fits both
        if ((b == Bright && kind == theme::Brightness::Dark) || (b == Dark && kind == theme::Brightness::Bright))
            continue;
        QAction *one = menu->addAction(e.name, this, [this, b, id = e.id] { assign(b, id); });
        one->setCheckable(true);
        one->setChecked(e.id == assigned);
        one->setData(e.id);
        group->addAction(one);
    }
}

void ThemePicker::updateButtons()
{
    const QString now = look::active().theme;
    for (Button b : {Bright, Dark, User1, User2}) {
        QToolButton *t = m_buttons[int(b)];
        if (!t)
            continue;
        const QString id = themeOf(b);
        const bool on = !id.isEmpty() && id == now;
        t->setChecked(on);
        // the one shown in bold: a checked button alone is hard to see in some dark themes
        QFont f = t->font();
        f.setBold(on);
        t->setFont(f);
        if (b == User1 || b == User2)
            t->setToolTip(id.isEmpty() ? tr("Save the look you see now in %1 (the button is empty)").arg(buttonLabel(b))
                                       : tr("Show your look saved in %1 (right-click: save the look you see now here)")
                                             .arg(buttonLabel(b)));
        else
            t->setToolTip(tr("Show the theme “%1” (right-click: choose the theme of this button)").arg(themeName(id)));
    }
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
        if (e.kind != last && m_combo->count() > 0)
            m_combo->insertSeparator(m_combo->count());
        last = e.kind;
        m_combo->addItem(e.name, e.id);
    }
    m_combo->setCurrentIndex(m_combo->findData(current));
    m_filling = false;
    updateButtons();
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
