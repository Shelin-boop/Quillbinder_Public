#pragma once
// 0.90.1: emptying a menu that is filled again each time it opens. QMenu::clear() deletes only the actions the menu
// owns; a submenu made by addMenu() is a child of the menu, and its entry is owned by the submenu, so the submenu
// stayed with all its entries, icons and connections. Menus filled from the binder (Go To, Open Quick Reference), the
// icon menu, the style menus and the mind map's menus kept a copy of their submenus at every opening: the memory of a
// long writing session grew with every opening (heaptrack, the system test's writing day: about 70 % of what the
// program held after 10 minutes). Only submenus that are children of this menu go; a menu kept elsewhere and added
// again (the Layouts menu in the binder's gear menu) has another parent and stays.
// Header-only: the writing tools (plugins) may use it as well.
#include <QAction>
#include <QMenu>
#include <QPointer>

namespace qb {

inline void clearMenu(QMenu *menu)
{
    if (!menu)
        return;
    QList<QPointer<QMenu>> made;
    for (QAction *a : menu->actions())
        if (QMenu *sub = a->menu(); sub && sub->parent() == menu)
            made << sub;
    menu->clear();
    // later, not now: a submenu may be the one whose signal is being handled (an entry chosen in it)
    for (const QPointer<QMenu> &sub : made)
        if (sub)
            sub->deleteLater();
}

} // namespace qb
