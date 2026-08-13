// =============================================================================
//  CubeMenu — data-driven menu tree
// =============================================================================
//
//  A menu is a table, not code. The old firmware drove its menu with a
//  `switch (menuIndex)` inside loop(), which works for one flat list of five
//  items and falls apart the moment there are submenus: every screen needs its
//  own index space, its own bounds, its own back handling, and the switch has
//  to know all of it at once.
//
//  Here a screen is a MenuScreen (title + array of MenuItem), and an item
//  either opens another screen or runs an action. Adding a screen is adding a
//  table. Nothing in this file knows what any of the items DO.
//
//  Deliberately dependency-free
//  ----------------------------
//  This file includes <stdint.h> and nothing else, and CubeMenu.cpp includes
//  only this header. It does not know about CubeDisplay, LVGL, the seesaw, or
//  Arduino — rendering goes out through a MenuDrawFn callback and input comes
//  in as a MenuEvent. That is what lets the navigation logic be compiled and
//  unit-tested on a host machine (see tests/test_menu.cpp) instead of only ever
//  being "tested" by flashing a Teensy and pressing buttons.
//
//  Screen size
//  -----------
//  kVisibleRows is 5 because that is the design limit for this machine: no
//  screen shows more than five items at once. Longer tables still work — the
//  engine scrolls and reports moreAbove/moreBelow — but a menu that needs to
//  scroll is a menu that wants splitting.
// =============================================================================

#ifndef CubeMenu_h
#define CubeMenu_h

#include <stdint.h>

struct MenuScreen;

// Action items call one of these. It takes no arguments and returns nothing:
// an action's job is to change application state, and the application owns that
// state, not the menu.
typedef void (*MenuActionFn)();

// An item opens a submenu OR runs an action, never both.
//
// A submenu item has `action == nullptr`; an action item has
// `submenu == nullptr`. An item with both null is legal and inert — useful as a
// placeholder while a screen is being built out.
struct MenuItem {
    const char*       label;
    const MenuScreen* submenu;
    MenuActionFn      action;
};

struct MenuScreen {
    const char*     title;
    const MenuItem* items;
    uint8_t         count;
};

// Navigation events, already edge-detected by the caller.
//
// Up/Down are in SCREEN terms — Up moves toward index 0, the item drawn higher
// up. The old .ino mapped its Ev::Up to `menuIndex += 1`, i.e. "up" moved the
// cursor DOWN the list, which is the kind of thing that costs an hour the first
// time the wheel feels inverted on the bench.
enum class MenuEvent : uint8_t { None, Up, Down, Select, Back };

// Called by render() when the screen needs redrawing.
//
//   title        screen title, never null
//   labels       `rows` pointers, the visible slice of the item list
//   chevron      `rows` flags, true where that item opens a submenu
//   rows         number of visible entries, 1..kVisibleRows
//   selectedRow  index WITHIN the visible slice, 0..rows-1
//   moreAbove/   whether the list continues off-screen, so the renderer can
//   moreBelow    draw scroll hints
typedef void (*MenuDrawFn)(const char*        title,
                           const char* const* labels,
                           const bool*        chevron,
                           uint8_t            rows,
                           uint8_t            selectedRow,
                           bool               moreAbove,
                           bool               moreBelow);

class CubeMenu {
public:
    static const uint8_t kVisibleRows = 5;

    // Depth of the navigation stack. Main -> Settings -> Calibration is 3, so 6
    // leaves room to grow. enter() refuses to go deeper rather than running off
    // the end of the array.
    static const uint8_t kMaxDepth = 6;

    // `root` and `draw` must both be non-null. The screen is drawn on the first
    // render() after this.
    void begin(const MenuScreen* root, MenuDrawFn draw);

    // Swap the root screen and return to it, discarding the navigation stack.
    //
    // This is how the pre-scan and post-scan main menus are switched. The stack
    // MUST be discarded: the old stack's frames point into the previous root's
    // item tables, and a Back from a submenu would otherwise pop into a screen
    // the application no longer considers reachable.
    //
    // No-ops (without clearing the stack) if `root` is already the current root,
    // so calling it defensively on every state change does not yank the user out
    // of a submenu they are reading.
    void setRoot(const MenuScreen* root);

    // Apply one navigation event. Returns true if it changed anything —
    // including running an action, which may have changed application state.
    bool handle(MenuEvent ev);

    // Draw the current screen if it has changed since the last draw.
    // Cheap to call every loop; does nothing when clean.
    void render();

    // Force the next render() to draw. Call this when something OUTSIDE the
    // menu has overwritten the panel — after an operation screen, an error, a
    // scan. The menu cannot detect that on its own.
    void redraw() { dirty = true; }

    // Push a screen. Returns false if the stack is full (screen not pushed).
    bool enter(const MenuScreen* screen);

    // Pop one level. Returns false if already at the root, which is how the
    // application knows a Back at the top level means something else (or
    // nothing).
    bool back();

    // Pop everything, keeping the cursor positions of the levels popped.
    void toRoot();

    const MenuScreen* current() const;
    const MenuItem*   selectedItem() const;

    uint8_t selectedIndex() const { return cursor[depth]; }
    uint8_t depthLevel()    const { return depth; }
    bool    atRoot()        const { return depth == 0; }

private:
    // One cursor per depth, not one global cursor.
    //
    // Backing out of a submenu returns to the item you entered it from. With a
    // single shared cursor, leaving Settings -> Calibration drops you on
    // whatever index you last touched inside Calibration, which on a 3-item
    // Settings screen is usually the wrong row and sometimes out of range.
    const MenuScreen* stack[kMaxDepth];
    uint8_t           cursor[kMaxDepth];
    uint8_t           depth  = 0;

    MenuDrawFn        drawFn = nullptr;
    bool              dirty  = true;

    // Top of the visible window for a given cursor/count. Centres the cursor
    // and clamps to the ends. Always 0 while count <= kVisibleRows, which is
    // every screen this machine currently has.
    static uint8_t windowTop(uint8_t cursorIdx, uint8_t count);
};

#endif // CubeMenu_h
