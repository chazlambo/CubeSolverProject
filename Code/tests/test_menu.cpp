// =============================================================================
//  Host-side test for CubeMenu
// =============================================================================
//
//  CubeMenu depends on nothing but <stdint.h>, so its navigation logic can be
//  compiled and exercised on a PC. Everything here is behaviour that is
//  otherwise only verifiable by flashing a Teensy and pressing buttons.
//
//  Build and run, from this directory:
//      g++ -std=c++11 -Wall -Wextra -I../libraries/CubeSolver
//          test_menu.cpp ../libraries/CubeSolver/CubeMenu.cpp -o /tmp/test_menu
//      /tmp/test_menu
//  (one command; the two g++ lines join)
// =============================================================================

#include "CubeMenu.h"

#include <cstdio>
#include <cstring>

static int    failures = 0;
static int    checks   = 0;

#define CHECK(cond, ...)                                            \
    do {                                                            \
        checks++;                                                   \
        if (!(cond)) {                                              \
            failures++;                                             \
            std::printf("FAIL %s:%d  ", __FILE__, __LINE__);        \
            std::printf(__VA_ARGS__);                               \
            std::printf("\n");                                      \
        }                                                           \
    } while (0)

// ---------------------------------------------------------------------------
//  Captured render output
// ---------------------------------------------------------------------------
struct Frame {
    char    title[32];
    char    labels[CubeMenu::kVisibleRows][32];
    bool    chevron[CubeMenu::kVisibleRows];
    uint8_t rows;
    uint8_t selectedRow;
    bool    moreAbove;
    bool    moreBelow;
    int     drawCount;
};
static Frame g_frame;

static void testDraw(const char*        title,
                     const char* const* labels,
                     const bool*        chevron,
                     uint8_t            rows,
                     uint8_t            selectedRow,
                     bool               moreAbove,
                     bool               moreBelow) {
    std::snprintf(g_frame.title, sizeof(g_frame.title), "%s", title);
    for (uint8_t i = 0; i < rows && i < CubeMenu::kVisibleRows; ++i) {
        std::snprintf(g_frame.labels[i], sizeof(g_frame.labels[i]), "%s", labels[i]);
        g_frame.chevron[i] = chevron[i];
    }
    g_frame.rows        = rows;
    g_frame.selectedRow = selectedRow;
    g_frame.moreAbove   = moreAbove;
    g_frame.moreBelow   = moreBelow;
    g_frame.drawCount++;
}

// ---------------------------------------------------------------------------
//  A menu tree shaped like the real one
// ---------------------------------------------------------------------------
static int g_scanRuns   = 0;
static int g_solveRuns  = 0;
static int g_ejectRuns  = 0;
static int g_aboutRuns  = 0;

static void actScan()  { g_scanRuns++;  }
static void actSolve() { g_solveRuns++; }
static void actEject() { g_ejectRuns++; }
static void actAbout() { g_aboutRuns++; }

extern const MenuScreen kSettings;
extern const MenuScreen kCalibration;
extern const MenuScreen kModes;

static const MenuItem kCalItems[] = {
    { "Calibration Status", nullptr, nullptr },
    { "Color Sensors",      nullptr, nullptr },
    { "Motor Positions",    nullptr, nullptr },
    { "Servo Positions",    nullptr, nullptr },
};
const MenuScreen kCalibration = { "Calibration", kCalItems, 4 };

static const MenuItem kSettingsItems[] = {
    { "Calibration", &kCalibration, nullptr  },
    { "Diagnostics", nullptr,       nullptr  },
    { "About",       nullptr,       actAbout },
};
const MenuScreen kSettings = { "Settings", kSettingsItems, 3 };

static const MenuItem kModesItems[] = {
    { "Scramble Solve", nullptr, nullptr },
    { "Idle Mode",      nullptr, nullptr },
    { "Demo Mode",      nullptr, nullptr },
    { "Step Solve",     nullptr, nullptr },
    { "Patterns",       nullptr, nullptr },
};
const MenuScreen kModes = { "Modes", kModesItems, 5 };

static const MenuItem kMainPreItems[] = {
    { "Load & Scan Cube", nullptr,   actScan },
    { "Settings",         &kSettings, nullptr },
    { "Stats",            nullptr,   nullptr },
};
static const MenuScreen kMainPre = { "Cube Solver", kMainPreItems, 3 };

static const MenuItem kMainPostItems[] = {
    { "Solve",      nullptr,    actSolve },
    { "Modes",      &kModes,    nullptr  },
    { "Eject Cube", nullptr,    actEject },
    { "Settings",   &kSettings, nullptr  },
    { "Stats",      nullptr,    nullptr  },
};
static const MenuScreen kMainPost = { "Cube Ready", kMainPostItems, 5 };

// A deliberately over-long screen, to exercise scrolling. No real screen on the
// machine is allowed to look like this; the engine still has to cope.
static const MenuItem kLongItems[] = {
    { "one", nullptr, nullptr }, { "two",   nullptr, nullptr },
    { "three", nullptr, nullptr }, { "four", nullptr, nullptr },
    { "five", nullptr, nullptr }, { "six",   nullptr, nullptr },
    { "seven", nullptr, nullptr },
};
static const MenuScreen kLong = { "Long", kLongItems, 7 };

// ---------------------------------------------------------------------------
static void resetCounters() {
    g_scanRuns = g_solveRuns = g_ejectRuns = g_aboutRuns = 0;
    std::memset(&g_frame, 0, sizeof(g_frame));
}

int main() {
    CubeMenu menu;
    resetCounters();
    menu.begin(&kMainPre, testDraw);

    // --- first render draws, second does not -------------------------------
    menu.render();
    CHECK(g_frame.drawCount == 1, "first render should draw");
    CHECK(std::strcmp(g_frame.title, "Cube Solver") == 0, "title = %s", g_frame.title);
    CHECK(g_frame.rows == 3, "rows = %u", g_frame.rows);
    CHECK(g_frame.selectedRow == 0, "selectedRow = %u", g_frame.selectedRow);
    CHECK(g_frame.chevron[1] == true,  "Settings should show a chevron");
    CHECK(g_frame.chevron[0] == false, "Load & Scan should not");
    CHECK(g_frame.moreAbove == false && g_frame.moreBelow == false, "no scroll hints on a 3-item screen");

    menu.render();
    CHECK(g_frame.drawCount == 1, "clean render should not redraw");

    // --- wrap in both directions -------------------------------------------
    menu.handle(MenuEvent::Up);
    CHECK(menu.selectedIndex() == 2, "Up from 0 wraps to last, got %u", menu.selectedIndex());
    menu.handle(MenuEvent::Down);
    CHECK(menu.selectedIndex() == 0, "Down from last wraps to 0, got %u", menu.selectedIndex());
    menu.handle(MenuEvent::Down);
    menu.handle(MenuEvent::Down);
    CHECK(menu.selectedIndex() == 2, "two Downs -> 2, got %u", menu.selectedIndex());

    // --- Back at the root is refused ---------------------------------------
    CHECK(menu.atRoot(), "should be at root");
    CHECK(menu.handle(MenuEvent::Back) == false, "Back at root must report false");
    CHECK(menu.atRoot(), "still at root after refused Back");

    // --- actions fire ------------------------------------------------------
    menu.handle(MenuEvent::Up);          // -> index 1 ... wait, from 2 Up -> 1
    menu.handle(MenuEvent::Up);          // -> index 0
    CHECK(menu.selectedIndex() == 0, "back at Load & Scan, got %u", menu.selectedIndex());
    menu.handle(MenuEvent::Select);
    CHECK(g_scanRuns == 1, "scan action ran %d times", g_scanRuns);
    CHECK(menu.atRoot(), "an action must not change depth");

    // --- descend two levels and back out, cursors preserved ----------------
    menu.handle(MenuEvent::Down);        // Settings
    CHECK(menu.selectedIndex() == 1, "on Settings row");
    menu.handle(MenuEvent::Select);
    CHECK(menu.depthLevel() == 1, "depth = %u", menu.depthLevel());
    menu.render();
    CHECK(std::strcmp(g_frame.title, "Settings") == 0, "title = %s", g_frame.title);
    CHECK(g_frame.rows == 3, "Settings rows = %u", g_frame.rows);

    menu.handle(MenuEvent::Select);      // Calibration
    CHECK(menu.depthLevel() == 2, "depth = %u", menu.depthLevel());
    menu.handle(MenuEvent::Down);
    menu.handle(MenuEvent::Down);
    CHECK(menu.selectedIndex() == 2, "cursor inside Calibration = %u", menu.selectedIndex());

    menu.handle(MenuEvent::Back);
    CHECK(menu.depthLevel() == 1, "depth after Back = %u", menu.depthLevel());
    CHECK(menu.selectedIndex() == 0, "Settings cursor preserved at 0, got %u", menu.selectedIndex());

    menu.handle(MenuEvent::Down);
    menu.handle(MenuEvent::Down);        // About
    menu.handle(MenuEvent::Select);
    CHECK(g_aboutRuns == 1, "about action ran %d times", g_aboutRuns);
    CHECK(menu.depthLevel() == 1, "action from a submenu must not change depth");

    menu.handle(MenuEvent::Back);
    CHECK(menu.atRoot(), "back at root");
    CHECK(menu.selectedIndex() == 1, "main cursor preserved on Settings, got %u", menu.selectedIndex());

    // --- re-entering a submenu resets ITS cursor ---------------------------
    menu.handle(MenuEvent::Select);
    CHECK(menu.selectedIndex() == 0, "re-entered Settings starts at 0, got %u", menu.selectedIndex());
    menu.handle(MenuEvent::Back);

    // --- inert placeholder items -------------------------------------------
    menu.handle(MenuEvent::Down);        // Stats: no submenu, no action
    CHECK(menu.selectedIndex() == 2, "on Stats");
    CHECK(menu.handle(MenuEvent::Select) == false, "placeholder Select reports no change");
    CHECK(menu.atRoot() && menu.depthLevel() == 0, "placeholder must not navigate");

    // --- swapping roots discards the stack ---------------------------------
    menu.handle(MenuEvent::Up);
    menu.handle(MenuEvent::Select);      // into Settings
    menu.handle(MenuEvent::Select);      // into Calibration
    CHECK(menu.depthLevel() == 2, "buried two deep");
    menu.setRoot(&kMainPost);
    CHECK(menu.depthLevel() == 0, "setRoot must return to depth 0, got %u", menu.depthLevel());
    CHECK(menu.selectedIndex() == 0, "setRoot resets the cursor, got %u", menu.selectedIndex());
    menu.render();
    CHECK(std::strcmp(g_frame.title, "Cube Ready") == 0, "title = %s", g_frame.title);
    CHECK(g_frame.rows == 5, "post-scan rows = %u", g_frame.rows);

    // Back must not pop into the abandoned tree.
    CHECK(menu.handle(MenuEvent::Back) == false, "Back after setRoot must be refused");

    // --- setRoot to the CURRENT root is a no-op -----------------------------
    menu.handle(MenuEvent::Down);
    menu.handle(MenuEvent::Down);
    CHECK(menu.selectedIndex() == 2, "cursor moved to 2");
    menu.setRoot(&kMainPost);
    CHECK(menu.selectedIndex() == 2, "redundant setRoot must not move the cursor, got %u",
          menu.selectedIndex());

    // ...including from inside a submenu. The application calls setRoot() every
    // time it hands control back to the menu, so a redundant call made while
    // the user is two levels down must leave them exactly where they are —
    // otherwise finishing a calibration would dump them at the top level.
    menu.handle(MenuEvent::Down);         // Settings row (index 3)
    menu.handle(MenuEvent::Select);
    CHECK(menu.depthLevel() == 1, "inside Settings");
    menu.handle(MenuEvent::Down);
    menu.setRoot(&kMainPost);
    CHECK(menu.depthLevel() == 1, "redundant setRoot must not pop the stack, depth %u",
          menu.depthLevel());
    CHECK(menu.selectedIndex() == 1, "and must not move the submenu cursor, got %u",
          menu.selectedIndex());
    menu.handle(MenuEvent::Back);

    // A root that genuinely differs still discards everything.
    menu.handle(MenuEvent::Select);
    CHECK(menu.depthLevel() == 1, "back inside Settings");
    menu.setRoot(&kMainPre);
    CHECK(menu.depthLevel() == 0, "changed root returns to depth 0");
    CHECK(menu.selectedIndex() == 0, "changed root resets the cursor");
    menu.setRoot(&kMainPost);

    // --- scrolling on an over-long screen ----------------------------------
    {
        CubeMenu longMenu;
        resetCounters();
        longMenu.begin(&kLong, testDraw);
        longMenu.render();
        CHECK(g_frame.rows == CubeMenu::kVisibleRows, "clamped to %u rows, got %u",
              CubeMenu::kVisibleRows, g_frame.rows);
        CHECK(g_frame.selectedRow == 0, "cursor at top row");
        CHECK(std::strcmp(g_frame.labels[0], "one") == 0, "window starts at 'one', got %s",
              g_frame.labels[0]);
        CHECK(g_frame.moreAbove == false, "nothing above at the top");
        CHECK(g_frame.moreBelow == true,  "more below at the top");

        // Walk to the last item; the window must follow and stop at the end.
        for (int i = 0; i < 6; ++i) longMenu.handle(MenuEvent::Down);
        CHECK(longMenu.selectedIndex() == 6, "at last item, got %u", longMenu.selectedIndex());
        longMenu.render();
        CHECK(std::strcmp(g_frame.labels[0], "three") == 0, "window pinned to end, starts %s",
              g_frame.labels[0]);
        CHECK(g_frame.selectedRow == 4, "cursor on bottom row, got %u", g_frame.selectedRow);
        CHECK(g_frame.moreAbove == true,  "more above at the end");
        CHECK(g_frame.moreBelow == false, "nothing below at the end");

        // Wrap past the end: window must snap back to the top, not underflow.
        longMenu.handle(MenuEvent::Down);
        CHECK(longMenu.selectedIndex() == 0, "wrapped to 0, got %u", longMenu.selectedIndex());
        longMenu.render();
        CHECK(std::strcmp(g_frame.labels[0], "one") == 0, "window back at top, starts %s",
              g_frame.labels[0]);
        CHECK(g_frame.selectedRow == 0, "cursor row 0, got %u", g_frame.selectedRow);
    }

    // --- depth limit -------------------------------------------------------
    {
        // A screen whose only item re-enters itself: the fastest way to hit the
        // stack limit. It must refuse rather than write past the array.
        static MenuItem  selfItem[1];
        static MenuScreen selfScreen = { "Self", selfItem, 1 };
        selfItem[0].label   = "deeper";
        selfItem[0].submenu = &selfScreen;
        selfItem[0].action  = nullptr;

        CubeMenu deep;
        resetCounters();
        deep.begin(&selfScreen, testDraw);
        for (int i = 0; i < 20; ++i) deep.handle(MenuEvent::Select);
        CHECK(deep.depthLevel() == CubeMenu::kMaxDepth - 1,
              "depth clamped to %u, got %u", CubeMenu::kMaxDepth - 1, deep.depthLevel());
        deep.render();   // must not crash or read out of bounds
        CHECK(g_frame.rows == 1, "self screen has 1 row, got %u", g_frame.rows);
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
