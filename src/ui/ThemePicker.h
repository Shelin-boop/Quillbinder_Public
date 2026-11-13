#pragma once
// The theme list of the main toolbar (0.90.2, the writer's wish): three buttons that narrow the list to the bright or
// the dark themes or show both, and a drop-down of the themes. Choosing one does what Window > Themes does
// (themeui::choose: the open project's look and the look for new projects). The filter is kept by the program
// (window/themeFilter).
#include <QComboBox>
#include <QStringList>
#include <QWidget>

class QAction;
class QButtonGroup;
class QToolBar;

namespace qb {

class ThemePicker : public QWidget
{
    Q_OBJECT
public:
    enum Filter { Bright = 0, Dark = 1, Both = 2 };

    explicit ThemePicker(QWidget *parent = nullptr);
    Filter filter() const { return m_filter; }
    void setFilter(Filter f);
    // the ids in the list now (for the checks)
    QStringList shownIds() const;
    void refill();

    // On a toolbar too narrow for both, the list gives way to `keep` (the quick search field): its action is hidden
    // while `keep` would otherwise go into the toolbar's overflow menu, and shown again once the toolbar is wide enough.
    static void giveWay(QToolBar *bar, QAction *own, QWidget *keep);

private:
    void picked(int index);

    QButtonGroup *m_group = nullptr;
    QComboBox *m_combo = nullptr;
    Filter m_filter = Both;
    bool m_filling = false;
};

} // namespace qb
