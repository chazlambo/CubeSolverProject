// CubeMenu.cpp — see CubeMenu.h for the design notes.
#include "CubeMenu.h"

void CubeMenu::begin(const MenuScreen* root, MenuDrawFn draw) {
    for (uint8_t i = 0; i < kMaxDepth; ++i) {
        stack[i]  = nullptr;
        cursor[i] = 0;
    }
    stack[0] = root;
    depth    = 0;
    drawFn   = draw;
    dirty    = true;
    nav      = MenuNav::None;
}

void CubeMenu::setRoot(const MenuScreen* root) {
    if (root == nullptr) return;

    // Same root => nothing changed => do nothing, whatever the current depth.
    //
    // The depth part matters. The application calls this every time it returns
    // to the menu, to keep the main list in step with whether a cube is loaded.
    // If an unchanged root still reset the stack, then finishing an action
    // reached through Settings -> Calibration would dump the user back at the
    // top level instead of the submenu they were working in. And if the guard
    // were dropped entirely, a per-pass call would pin the cursor at 0 forever.
    if (stack[0] == root) return;

    for (uint8_t i = 0; i < kMaxDepth; ++i) {
        stack[i]  = nullptr;
        cursor[i] = 0;
    }
    stack[0] = root;
    depth    = 0;
    dirty    = true;
    nav      = MenuNav::Root;
}

MenuTheme CubeMenu::themeOf(const MenuScreen* screen, const MenuItem* item) {
    if (item != nullptr && item->theme != MenuTheme::Inherit) return item->theme;
    if (screen != nullptr && screen->theme != MenuTheme::Inherit) return screen->theme;
    return MenuTheme::Green;
}

const MenuScreen* CubeMenu::current() const {
    return stack[depth];
}

const MenuItem* CubeMenu::selectedItem() const {
    const MenuScreen* s = current();
    if (s == nullptr || s->items == nullptr || s->count == 0) return nullptr;
    if (cursor[depth] >= s->count) return nullptr;
    return &s->items[cursor[depth]];
}

bool CubeMenu::enter(const MenuScreen* screen) {
    if (screen == nullptr) return false;
    if (depth + 1 >= kMaxDepth) return false;

    depth++;
    stack[depth]  = screen;
    cursor[depth] = 0;
    dirty = true;
    nav   = MenuNav::Enter;
    return true;
}

bool CubeMenu::back() {
    if (depth == 0) return false;

    // Leave stack[depth]/cursor[depth] alone rather than clearing them. They
    // are re-initialised by the next enter() at this depth, and leaving them
    // means a stray render() during the transition draws the screen we just
    // left instead of dereferencing a null.
    depth--;
    dirty = true;
    nav   = MenuNav::Back;
    return true;
}

bool CubeMenu::handle(MenuEvent ev) {
    const MenuScreen* s = current();
    if (s == nullptr || s->items == nullptr || s->count == 0) return false;

    switch (ev) {

    case MenuEvent::Up:
        // Wrap. On a five-item menu, wrapping is one detent to reach the last
        // item instead of four.
        cursor[depth] = (cursor[depth] == 0) ? (uint8_t)(s->count - 1)
                                             : (uint8_t)(cursor[depth] - 1);
        dirty = true;
        nav   = MenuNav::Move;
        return true;

    case MenuEvent::Down:
        cursor[depth] = (uint8_t)((cursor[depth] + 1) % s->count);
        dirty = true;
        nav   = MenuNav::Move;
        return true;

    case MenuEvent::Select: {
        const MenuItem* item = selectedItem();
        if (item == nullptr) return false;

        if (item->submenu != nullptr) {
            return enter(item->submenu);
        }
        if (item->action != nullptr) {
            // The action may leave the menu (a scan, a solve, an info screen)
            // or draw its own screen over it in place. Either way the panel
            // no longer shows this menu, so it must be redrawn when we come
            // back.
            dirty = true;
            item->action();
            return true;
        }
        // Placeholder item with neither target. Deliberately inert.
        return false;
    }

    case MenuEvent::Back:
        return back();

    case MenuEvent::None:
    default:
        return false;
    }
}

uint8_t CubeMenu::windowTop(uint8_t cursorIdx, uint8_t count) {
    if (count <= kVisibleRows) return 0;

    // Centre the cursor, then clamp. Signed arithmetic: cursorIdx - 2 underflows
    // for cursorIdx 0 and 1 in unsigned, which would put the window at the very
    // bottom of a long list exactly when the cursor is at the top of it.
    int top = (int)cursorIdx - (int)(kVisibleRows / 2);
    int maxTop = (int)count - (int)kVisibleRows;
    if (top < 0)      top = 0;
    if (top > maxTop) top = maxTop;
    return (uint8_t)top;
}

void CubeMenu::render() {
    if (!dirty || drawFn == nullptr) return;

    const MenuScreen* s = current();
    if (s == nullptr || s->items == nullptr || s->count == 0) return;

    // Clamp the cursor here as well as in handle().
    //
    // Tables are const data supplied by the application, and an application that
    // swaps a screen's contents under a live cursor (a mode list that grows, a
    // conditional item) would otherwise index past the end. Cheap insurance at
    // the one point that reads every field.
    if (cursor[depth] >= s->count) cursor[depth] = (uint8_t)(s->count - 1);

    const uint8_t top  = windowTop(cursor[depth], s->count);
    uint8_t       rows = (uint8_t)(s->count - top);
    if (rows > kVisibleRows) rows = kVisibleRows;

    const MenuItem* items[kVisibleRows];
    for (uint8_t i = 0; i < rows; ++i) items[i] = &s->items[top + i];

    drawFn(s,
           items,
           rows,
           (uint8_t)(cursor[depth] - top),
           top > 0,
           (uint16_t)(top + rows) < s->count,
           nav);

    dirty = false;

    // Cleared after the draw, not before: a renderer that starts a transition
    // animation reads this once, and a later forced redraw() (an operation
    // screen handing the panel back) must not replay that transition.
    nav = MenuNav::None;
}
