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

// Frame colour, chosen by the SELECTED item rather than by the screen.
//
// Inherit is deliberately 0. Every existing table was written as a three-field
// aggregate — { "Solve", nullptr, actSolve } — and C++ value-initialises the
// members those braces do not reach. Making the "no opinion" case the zero
// value is what lets those tables keep compiling untouched and still mean
// something sensible: fall back to the screen's theme.
enum class MenuTheme : uint8_t {
    Inherit = 0,
    Green, Blue, Red, Violet, Yellow, Purple
};

// An item opens a submenu OR runs an action, never both.
//
// A submenu item has `action == nullptr`; an action item has
// `submenu == nullptr`. An item with both null is legal and inert — useful as a
// placeholder while a screen is being built out.
//
// Everything from `caption` down is presentation the themed renderer draws and
// a plain one ignores. All of it is optional; an item that sets none of it
// still renders, just without a description line or a preview.
struct MenuItem {
    const char*       label   = nullptr;
    const MenuScreen* submenu = nullptr;
    MenuActionFn      action  = nullptr;

    // One line in the description box. nullptr leaves the box empty.
    const char*       caption = nullptr;

    // What the preview pane lists when this item is selected. nullptr means
    // "no list" — the renderer draws the placeholder graphic instead, which is
    // the design's answer for items that lead to an operation rather than to
    // another menu.
    const char* const* preview      = nullptr;
    uint8_t            previewCount = 0;

    // Frame colour while this item is selected. Inherit takes the screen's.
    MenuTheme          theme        = MenuTheme::Inherit;
};

struct MenuScreen {
    const char*     title = nullptr;
    const MenuItem* items = nullptr;
    uint8_t         count = 0;

    // Default colour for items that do not name one. Inherit means Green.
    //
    // These defaults are what keep every pre-theme table compiling as written:
    // a three-field { title, items, count } brace list still means exactly what
    // it did, and -Wextra stays quiet about the fields it does not mention.
    // Aggregate initialisation with default member initialisers needs C++14,
    // which the simulator (C++17) and the Teensy build (gnu++17) both exceed.
    MenuTheme       theme = MenuTheme::Inherit;
};

// Navigation events, already edge-detected by the caller.
//
// Up/Down are in SCREEN terms — Up moves toward index 0, the item drawn higher
// up. The old .ino mapped its Ev::Up to `menuIndex += 1`, i.e. "up" moved the
// cursor DOWN the list, which is the kind of thing that costs an hour the first
// time the wheel feels inverted on the bench.
enum class MenuEvent : uint8_t { None, Up, Down, Select, Back };

// What the user did to arrive at this frame.
//
// The themed renderer animates screen changes — old items wheel out, new ones
// wheel in, and Back mirrors the direction — so it has to distinguish "the
// cursor moved within a screen" from "we changed screens, this way". Only the
// menu knows which happened, so only the menu can say.
enum class MenuNav : uint8_t {
    None = 0,   // first draw, or a forced redraw() with nothing behind it
    Move,       // cursor moved within the current screen
    Enter,      // pushed into a submenu
    Back,       // popped out of one
    Root        // the root screen was swapped underneath us
};

// Called by render() when the screen needs redrawing.
//
//   screen       the screen being drawn, never null; carries title and theme
//   items        `rows` pointers, the visible slice of the item list. Passing
//                the items themselves rather than pre-flattened label/chevron
//                arrays is what lets a renderer reach the caption, preview and
//                per-item theme without this header growing a parameter per
//                field every time the design gains one.
//   rows         number of visible entries, 1..kVisibleRows
//   selectedRow  index WITHIN the visible slice, 0..rows-1
//   moreAbove/   whether the list continues off-screen, so the renderer can
//   moreBelow    draw scroll hints
//   nav          how we got here; see MenuNav
typedef void (*MenuDrawFn)(const MenuScreen*      screen,
                           const MenuItem* const* items,
                           uint8_t                rows,
                           uint8_t                selectedRow,
                           bool                   moreAbove,
                           bool                   moreBelow,
                           MenuNav                nav);

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

    // Resolve an item's frame colour against its screen's default.
    //
    // Lives here rather than in the renderer because it is a fact about the
    // data model, and a second renderer (or a test) that re-derived the
    // fallback rule could quietly disagree with the first.
    static MenuTheme themeOf(const MenuScreen* screen, const MenuItem* item);

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

    // Consumed by the next render() and reset afterwards, so a redraw() that
    // is not the result of navigation does not replay the last transition.
    MenuNav           nav    = MenuNav::None;

    // Top of the visible window for a given cursor/count. Centres the cursor
    // and clamps to the ends. Always 0 while count <= kVisibleRows, which is
    // every screen this machine currently has.
    static uint8_t windowTop(uint8_t cursorIdx, uint8_t count);
};

#endif // CubeMenu_h
