#pragma once
// The theme control of the toolbars (0.90.2; its own toolbar "Themes" since 0.91.0): four buttons and a drop-down of
// all themes.
//
// 0.91.0 (the writer's request of 2026-11-13: "Let the User safe his own theme. Just 2 themes called User_1 and
// User_2. remove the 'Both' and exchange it for the U1 and U2 ones. also, the buttons do not work"): the buttons are
// switches, no longer filters of the list.
//   Bright, Dark: a click shows the theme assigned to the button ("Light" and "Dark Mode" until the writer assigns
//                 another by right-click; kept in window/themeBright, window/themeDark).
//   U1, U2:       the writer's own looks, kept as the own themes "User_1" and "User_2". A click on an empty button
//                 saves the look shown now there; a click on a filled one shows its look; right-click > Save the
//                 Current Look Here replaces it.
// A button shows its theme as it is, so the click changes what is seen. Colors and fonts of the writer's own that lie
// on top of the theme shown before would be lost by that: they are saved first, into an empty U button without a
// question, else the writer says where (keepOwnLook). The list still does what Window > Themes does (themeui::choose).
#include <QComboBox>
#include <QStringList>
#include <QWidget>

class QAction;
class QMenu;
class QToolBar;
class QToolButton;

namespace qb {

class ThemePicker : public QWidget
{
    Q_OBJECT
public:
    enum Button { Bright = 0, Dark = 1, User1 = 2, User2 = 3 };

    explicit ThemePicker(QWidget *parent = nullptr);

    // the theme a button shows: "" for an empty U button
    QString themeOf(Button b) const;
    static QString userThemeId(Button b);   // "user:User_1", "user:User_2"
    static QString userThemeName(Button b); // "User_1", "User_2"
    // Bright, Dark: the theme of the button from now on (false: no such theme, or a U button)
    bool assign(Button b, const QString &themeId);
    // what a click on the button does; false when nothing changed (cancelled, or a failure that was told)
    bool press(Button b);
    // U1, U2: the look shown now becomes the button's look and the theme in use (askFirst: a filled button asks
    // before its look is replaced)
    bool saveLookIn(Button b, bool askFirst = true);
    // what a right-click on the button offers
    void fillButtonMenu(QMenu *menu, Button b);

    // the ids in the list now (for the checks)
    QStringList shownIds() const;
    void refill();

    // For the checks: the answer to the question that comes when the look has changes of the writer's own, both U
    // buttons are taken and a button is clicked: User1 or User2 = save there first, -1 = switch without saving,
    // -2 = cancel, -3 (the standard) = ask.
    static void setAnswerForTests(int answer);

    // On a toolbar too narrow for both, the list gives way to `keep` (the quick search field): its action is hidden
    // while `keep` would otherwise go into the toolbar's overflow menu, and shown again once the toolbar is wide enough.
    // (Only where the writer put both on one toolbar: since 0.91.0 each stands on a toolbar of its own.)
    static void giveWay(QToolBar *bar, QAction *own, QWidget *keep);

private:
    void picked(int index);
    void updateButtons();
    bool store(Button b);
    bool keepOwnLook();
    void tell(const QString &text);

    QToolButton *m_buttons[4] = {};
    QComboBox *m_combo = nullptr;
    bool m_filling = false;
};

} // namespace qb
