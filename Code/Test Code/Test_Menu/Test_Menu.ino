// =============================================================================
//  Test_Menu — menu, navigation and panel test
// =============================================================================
//
//  A bench sketch for the wheel, the buttons and the themed panel. It drives
//  the SAME CubeMenu and CubeDisplay the real firmware does, so anything that
//  feels wrong here feels wrong there.
//
//  WHAT IT DOES
//  ------------
//  Nothing to the cube except LOAD and EJECT. There is no scan, no solve, no
//  calibration, and no stepper motion of any kind — the only things that move
//  are the two servos and the ring, and only when you pick Load or Eject.
//
//  Everything else on the menu is either navigation (which is the point) or a
//  screen demo that draws the panel without touching hardware.
//
//  WHY THE SCREEN DEMOS ARE HERE
//  -----------------------------
//  The operation screens — scan step rows, calibration colour chips, the solve
//  progress bar, the error look — are normally only reachable by running the
//  machine for real. On the bench that means a 40-second scan to check one
//  label. Screens > ... draws each of them from canned data, so the panel can
//  be judged in seconds and photographed without a cube in the chamber.
//
//  BOARD / SETUP
//  -------------
//  Teensy 4.1, same as the firmware. Set Arduino's sketchbook location to
//  Code/ so <CubeSystem.h> resolves. See the repo README.
//
//  Start-up takes ~10 s: CubeSystem::begin() sweeps both servos through the
//  real CubeServo code. It does NOT home the steppers.
//
//  CONTROLS (identical to the firmware — that is what is being tested)
//  -------------------------------------------------------------------
//    wheel / UP / DOWN     move the cursor
//    SELECT / RIGHT        enter a submenu, or run the item
//    LEFT                  back out one level
//    SELECT + LEFT         swallowed as the abort chord, never a menu action
//
//  See docs/theme/UI_DESIGN.md before changing anything the panel draws.
// =============================================================================

#include <CubeSystem.h>
#include <CubeHardwareConfig.h>
#include <CubeMenu.h>

CubeSystem Cube;
CubeMenu   Menu;

using Op = CubeDisplay::OpKind;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
//  Deliberately smaller than the firmware's AppState. There are no long
//  operations to model here: a mechanical move runs to completion inside one
//  loop() pass, and everything else is either the menu or a screen waiting to
//  be dismissed.
enum class TState : uint8_t {
    Menu,       // CubeMenu has the panel
    Screen,     // a full-screen view; SELECT or LEFT returns
    Loading,    // clamp the cube, once
    Ejecting    // release and present it, once
};

static TState state = TState::Menu;

// A screen that redraws itself every pass (the live input report), or animates
// from canned data (the operation-screen demos).
enum class Live : uint8_t { None, Input, Steps, Chips, Progress };
static Live     live      = Live::None;
static uint32_t liveStart = 0;
static uint32_t lastLive  = 0;

// Input edge-detection state, lifted from the firmware unchanged. This is the
// code under test, so it is copied rather than approximated.
static bool     prevSelect = false;
static bool     prevLeft   = false;
static bool     prevRight  = false;
static bool     prevUp     = false;
static bool     prevDown   = false;
static int32_t  prevPos    = 0;
static uint32_t lastPoll   = 0;

// ---------------------------------------------------------------------------
//  Forward declarations
// ---------------------------------------------------------------------------
extern const MenuScreen kScreenNav;
extern const MenuScreen kScreenScreens;
extern const MenuScreen kScreenThree;
extern const MenuScreen kScreenFour;
extern const MenuScreen kScreenFive;
extern const MenuScreen kScreenLong;
extern const MenuScreen kScreenDeep;

static void actLoad();
static void actEject();
static void actReport();
static void actInputReport();
static void actDemoInfo();
static void actDemoSteps();
static void actDemoChips();
static void actDemoProgress();
static void actDemoError();

// ---------------------------------------------------------------------------
//  Menu tables
// ---------------------------------------------------------------------------
//  Between them these cover every visual state the theme has: all six frame
//  colours, screens of three, four and five items, captions of realistic
//  length, preview lists and the placeholder graphic, labels long enough to
//  ellipsise, and a stack deep enough to hit CubeMenu's depth limit.

static const char* const kPrevNav[]     = { "3 items", "4 items", "5 items", "Long" };
static const char* const kPrevScreens[] = { "Info", "Steps", "Chips", "Bar", "Error" };
static const char* const kPrevDeep[]    = { "Deeper", "and", "deeper" };

// ---- root ----
static const MenuItem kMainItems[] = {
    { "Load Cube",   nullptr,          actLoad,        "Clamp the cube. Moves servos.",
      nullptr, 0, MenuTheme::Blue },
    { "Eject Cube",  nullptr,          actEject,       "Release and present it.",
      nullptr, 0, MenuTheme::Violet },
    { "Navigation",  &kScreenNav,      nullptr,        "Screens of every size.",
      kPrevNav, 4, MenuTheme::Red },
    { "Screens",     &kScreenScreens,  nullptr,        "Draw the panel, no hardware.",
      kPrevScreens, 5, MenuTheme::Yellow },
    { "Input Report", nullptr,         actInputReport, "Live wheel and buttons.",
      nullptr, 0, MenuTheme::Purple },
};
static const MenuScreen kScreenMain = { "Menu Test", kMainItems, 5, MenuTheme::Green };

// ---- navigation: one screen per item count ----
//
// Five items pack tighter than four or fewer (different start and pitch), so
// both layouts need looking at on the real panel.
static const MenuItem kNavItems[] = {
    { "Three Items", &kScreenThree, nullptr, "The wider row pitch." },
    { "Four Items",  &kScreenFour,  nullptr, "Also the wider pitch." },
    { "Five Items",  &kScreenFive,  nullptr, "The tight pitch." },
    { "Long Labels", &kScreenLong,  nullptr, "Check the ellipsis." },
    { "Depth Test",  &kScreenDeep,  nullptr, "Re-enters itself.", kPrevDeep, 3 },
};
const MenuScreen kScreenNav = { "Navigation", kNavItems, 5, MenuTheme::Red };

static const MenuItem kThreeItems[] = {
    { "Alpha", nullptr, actReport, "First of three." },
    { "Bravo", nullptr, actReport, "Second of three." },
    { "Delta", nullptr, actReport, "Third of three." },
};
const MenuScreen kScreenThree = { "Three Items", kThreeItems, 3, MenuTheme::Blue };

static const MenuItem kFourItems[] = {
    { "Alpha", nullptr, actReport, "First of four." },
    { "Bravo", nullptr, actReport, "Second of four." },
    { "Delta", nullptr, actReport, "Third of four." },
    { "Echo",  nullptr, actReport, "Fourth of four." },
};
const MenuScreen kScreenFour = { "Four Items", kFourItems, 4, MenuTheme::Violet };

static const MenuItem kFiveItems[] = {
    { "Alpha", nullptr, actReport, "First of five." },
    { "Bravo", nullptr, actReport, "Second of five." },
    { "Delta", nullptr, actReport, "Third of five." },
    { "Echo",  nullptr, actReport, "Fourth of five." },
    { "Fox",   nullptr, actReport, "Fifth of five." },
};
const MenuScreen kScreenFive = { "Five Items", kFiveItems, 5, MenuTheme::Yellow };

// Labels and captions past what fits, on purpose. Both should ellipsise
// cleanly rather than overrun the bar or the description box.
static const MenuItem kLongItems[] = {
    { "Reasonable",                    nullptr, actReport, "A caption that fits." },
    { "Considerably Longer Label",     nullptr, actReport,
      "A caption written well past the width of the description box." },
    { "AAAAAAAAAAAAAAAAAAAAAAAAAAAAA", nullptr, actReport, "No spaces to break on." },
};
const MenuScreen kScreenLong = { "Long Labels", kLongItems, 3, MenuTheme::Purple };

// One item that re-enters its own screen. SELECT repeatedly to confirm the
// stack clamps at kMaxDepth instead of running off the end of the array, and
// that LEFT unwinds every level.
static const MenuItem kDeepItems[] = {
    { "Go Deeper",  &kScreenDeep, nullptr,   "Pushes another level." },
    { "Say Depth",  nullptr,      actReport, "Reports the current depth." },
};
const MenuScreen kScreenDeep = { "Depth Test", kDeepItems, 2, MenuTheme::Green };

// ---- screen demos ----
static const MenuItem kScreensItems[] = {
    { "Info Panel",   nullptr, actDemoInfo,     "Aligned label/value rows." },
    { "Scan Steps",   nullptr, actDemoSteps,    "Three passes, lit in turn." },
    { "Colour Chips", nullptr, actDemoChips,    "Two boards, six colours." },
    { "Progress Bar", nullptr, actDemoProgress, "Fills over four seconds." },
    { "Error Screen", nullptr, actDemoError,    "The red stopped look." },
};
const MenuScreen kScreenScreens = { "Screens", kScreensItems, 5, MenuTheme::Yellow };

// ---------------------------------------------------------------------------
//  Display helpers
// ---------------------------------------------------------------------------
static void showOp(Op kind, const char* title, const char* headline,
                   const char* hint = nullptr) {
    cubeDisplay.showOperation(kind, title, headline, hint);
    Cube.displayUpdate();
}

// A screen that waits for SELECT or LEFT. `l` picks what keeps updating it.
static void showScreen(Op kind, const char* title, const char* headline,
                       Live l = Live::None) {
    showOp(kind, title, headline, "SELECT or LEFT to go back");
    live      = l;
    liveStart = millis();
    state     = TState::Screen;
}

static void toMenu() {
    // Re-seed the rotary baseline. The encoder free-runs while a screen is up
    // or a servo is moving, and without this the accumulated delta arrives as
    // one huge rotation the moment the menu comes back.
    if (Cube.encoderInitialized) prevPos = menuEncoder.getPosition();
    Menu.redraw();
    live  = Live::None;
    state = TState::Menu;
}

static void drawMenu(const MenuScreen*      screen,
                     const MenuItem* const* items,
                     uint8_t                rows,
                     uint8_t                selectedRow,
                     bool                   moreAbove,
                     bool                   moreBelow,
                     MenuNav                nav) {
    cubeDisplay.showList(screen, items, rows, selectedRow, moreAbove, moreBelow, nav);
}

// ---------------------------------------------------------------------------
//  Actions — the only two that touch hardware
// ---------------------------------------------------------------------------
static void actLoad() {
    Cube.clearAbort();
    showOp(Op::Solve, "Load", "Clamping the cube");
    state = TState::Loading;
}

static void actEject() {
    Cube.clearAbort();
    showOp(Op::Info, "Eject", "Releasing the cube");
    state = TState::Ejecting;
}

// ---------------------------------------------------------------------------
//  Actions — navigation feedback
// ---------------------------------------------------------------------------
// Names the item that was picked and the depth it was picked at. That is the
// whole point of the navigation screens: confirming SELECT fires on the row the
// cursor is actually on, at every level.
static void actReport() {
    const MenuItem* it = Menu.selectedItem();

    static char rows[3][48];
    snprintf(rows[0], sizeof(rows[0]), "Item\t%s", (it && it->label) ? it->label : "?");
    snprintf(rows[1], sizeof(rows[1]), "Index\t%u", (unsigned)Menu.selectedIndex());
    snprintf(rows[2], sizeof(rows[2]), "Depth\t%u", (unsigned)Menu.depthLevel());

    const char* lines[] = { rows[0], rows[1], rows[2] };
    showScreen(Op::Done, "Selected", nullptr);
    cubeDisplay.setOpLines(lines, 3);
    Cube.displayUpdate();
}

// Live readout of the wheel and every button. The one screen that shows a
// flaky encoder or a dead button directly, instead of leaving you to infer it
// from a menu that scrolls oddly.
static void actInputReport() {
    showScreen(Op::Info, "Input Report", nullptr, Live::Input);
}

static void updateInputReport() {
    if (!Cube.encoderInitialized) {
        const char* lines[] = { "Menu encoder not found on Wire1." };
        cubeDisplay.setOpLines(lines, 1);
        return;
    }

    const uint8_t b = menuEncoder.readButtons();

    static char rows[4][48];
    snprintf(rows[0], sizeof(rows[0]), "Wheel\t%ld", (long)menuEncoder.getPosition());
    snprintf(rows[1], sizeof(rows[1]), "SELECT\t%s",
             (b & RotaryEncoder::BTN_SELECT) ? "DOWN" : "-");
    snprintf(rows[2], sizeof(rows[2]), "UP / DOWN\t%s %s",
             (b & RotaryEncoder::BTN_UP)   ? "DOWN" : "-",
             (b & RotaryEncoder::BTN_DOWN) ? "DOWN" : "-");
    snprintf(rows[3], sizeof(rows[3]), "LEFT / RIGHT\t%s %s",
             (b & RotaryEncoder::BTN_LEFT)  ? "DOWN" : "-",
             (b & RotaryEncoder::BTN_RIGHT) ? "DOWN" : "-");

    const char* lines[] = { rows[0], rows[1], rows[2], rows[3],
                            "", "Hold LEFT alone to leave." };
    cubeDisplay.setOpLines(lines, 6);
}

// ---------------------------------------------------------------------------
//  Actions — screen demos, all from canned data
// ---------------------------------------------------------------------------
static void actDemoInfo() {
    const char* lines[] = {
        "Motors\tCALIBRATED",
        "Colour\tNOT CALIBRATED",
        "Board 1\t9/9 healthy, sep 165",
        "Board 2\t8/9 healthy, sep 3",
        "",
        "Canned values - nothing was read.",
    };
    showScreen(Op::Info, "Info Panel", nullptr);
    cubeDisplay.setOpLines(lines, 6);
    Cube.displayUpdate();
}

static void actDemoSteps() {
    showScreen(Op::Scan, "Scan Steps", nullptr, Live::Steps);
}

static void actDemoChips() {
    // Headline left empty: the demo sets it per stage below, and a fixed one
    // here would sit contradicting the stage line underneath it.
    showScreen(Op::Calibrate, "Colour Chips", "", Live::Chips);
}

static void actDemoProgress() {
    showScreen(Op::Solve, "Progress Bar", "21 moves to run", Live::Progress);
}

static void actDemoError() {
    showScreen(Op::Error, "Stopped", "Move failed - cube released");
    const char* lines[] = { "Canned - nothing actually failed." };
    cubeDisplay.setOpLines(lines, 1);
    Cube.displayUpdate();
}

// Drive whichever demo is running from elapsed time. Each loops, so the panel
// can be watched or photographed without re-picking the item.
static void updateDemo() {
    const uint32_t t = millis() - liveStart;

    switch (live) {

    case Live::Steps: {
        // Three passes, ~1.6 s each, with a rotation between them — the shape
        // of the real scan, played back.
        const uint32_t cycle = t % 6400;
        const int      pass  = (int)(cycle / 1600);
        const bool     rotating = (cycle % 1600) > 1100 && pass < 2;

        Cube.displaySteps(CubeSystem::kScanPassLabels, CubeSystem::kScanPasses,
                          (pass < 3) ? pass : -1, rotating ? pass + 1 : pass);
        cubeDisplay.setStatus(rotating ? "Rotating cube" : "Reading two faces");
        break;
    }

    case Live::Chips: {
        // Fill both rows out of step, the way the real calibration does: each
        // rotation feeds a different colour to each board.
        const uint32_t cycle = t % 7200;
        const int      n     = (int)(cycle / 800);        // 0..8
        uint8_t bits[2] = { 0, 0 };
        for (int i = 0; i < n && i < 8; ++i) {
            const int stage = (i < 4) ? 0 : 1;
            const int rot   = (i < 4) ? i : i - 4;
            const uint8_t* pair = (stage == 0) ? CubeSystem::kCalSideColors[rot]
                                               : CubeSystem::kCalTopColors[rot];
            bits[0] |= (uint8_t)(1u << pair[0]);
            bits[1] |= (uint8_t)(1u << pair[1]);
        }
        Cube.displayChips(bits, 2);
        const bool sides = (n <= 4);
        cubeDisplay.setMessage(sides ? "Sampling side faces" : "Sampling top and bottom");
        char sub[32];
        snprintf(sub, sizeof(sub), "%s  (%d/4)", sides ? "Side faces" : "Top and bottom",
                 sides ? (n > 0 ? n : 1) : (n - 4 > 0 ? n - 4 : 1));
        cubeDisplay.setStatus(sub);
        break;
    }

    case Live::Progress: {
        const uint32_t cycle = t % 5000;
        int done = (int)((cycle * 21) / 4000);
        if (done > 21) done = 21;
        char sub[32];
        snprintf(sub, sizeof(sub), "Move %d/21", done);
        cubeDisplay.setStatus(sub);
        Cube.displayProgress(done, 21);
        break;
    }

    case Live::Input:
        updateInputReport();
        break;

    case Live::None:
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  Input: turn polled levels into single edge events
// ---------------------------------------------------------------------------
//  Copied from the firmware verbatim. This IS the thing under test — an
//  approximation here would test the approximation.
static MenuEvent pollEvent() {
    if (!Cube.encoderInitialized) return MenuEvent::None;

    uint32_t now = millis();
    if (now - lastPoll < 25) return MenuEvent::None;   // ~40 Hz; also debounces
    lastPoll = now;

    // All five buttons in one transaction, so the SELECT+LEFT chord is
    // detectable at all and it costs one I2C round trip instead of five.
    const uint8_t b = menuEncoder.readButtons();

    const bool selDown   = b & RotaryEncoder::BTN_SELECT;
    const bool leftDown  = b & RotaryEncoder::BTN_LEFT;
    const bool rightDown = b & RotaryEncoder::BTN_RIGHT;
    const bool upDown    = b & RotaryEncoder::BTN_UP;
    const bool downDown  = b & RotaryEncoder::BTN_DOWN;

    const bool selEdge   = selDown   && !prevSelect;
    const bool leftEdge  = leftDown  && !prevLeft;
    const bool rightEdge = rightDown && !prevRight;
    const bool upEdge    = upDown    && !prevUp;
    const bool downEdge  = downDown  && !prevDown;

    prevSelect = selDown;
    prevLeft   = leftDown;
    prevRight  = rightDown;
    prevUp     = upDown;
    prevDown   = downDown;

    // Rotation is read and CONSUMED unconditionally, even when a button wins
    // below, so prevPos stays in step and the deferred movement is not replayed
    // later as a phantom scroll.
    const int32_t pos = menuEncoder.getPosition();
    const bool rotated = (pos != prevPos);
    const bool clockwise = (pos > prevPos);
    prevPos = pos;

    // The abort chord is not a menu gesture.
    if (selDown && leftDown) return MenuEvent::None;

    // Buttons before rotation: reading rotation first and early-returning on
    // any change lets encoder noise starve the buttons indefinitely.
    if (selEdge || rightEdge) return MenuEvent::Select;
    if (leftEdge)             return MenuEvent::Back;

    // Clockwise moves DOWN the list. If it feels inverted on the bench, this is
    // the only line to flip — and then flip the matching line in the firmware.
    if (rotated) return clockwise ? MenuEvent::Down : MenuEvent::Up;

    if (upEdge)   return MenuEvent::Up;
    if (downEdge) return MenuEvent::Down;

    return MenuEvent::None;
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Brings up the display, both servos and the menu encoder, and registers
    // the cooperative pump. Takes ~10 s, almost all of it servo sweeps.
    Cube.begin();

    if (!Cube.displayReady()) {
        Serial.println(F("ERROR: display unavailable - this sketch has nothing to show."));
    }
    if (!Cube.encoderInitialized) {
        Serial.println(F("WARNING: menu encoder not found. Navigation will not respond;"));
        Serial.println(F("         Input Report will say so on the panel."));
    }

    Serial.println(F("Test_Menu ready. Load and Eject move servos; nothing else does."));

    Menu.begin(&kScreenMain, drawMenu);
    if (Cube.encoderInitialized) prevPos = menuEncoder.getPosition();
    state = TState::Menu;
}

void loop() {
    // Always service the display, whatever state we are in.
    Cube.displayUpdate();

    const MenuEvent ev = pollEvent();

    switch (state) {

    case TState::Menu:
        Menu.handle(ev);
        // Only redraw while the menu still has the panel: an action may have
        // moved us to a screen, and repainting the list over it would leave the
        // panel lying about what is on it.
        if (state == TState::Menu) Menu.render();
        break;

    case TState::Screen:
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            toMenu();
        } else if (live != Live::None && millis() - lastLive >= 50) {
            // Throttled to ~20 Hz. The input report reads the seesaw over I2C
            // every call, and an unthrottled loop() would hammer the same bus
            // pollEvent() is trying to use.
            lastLive = millis();
            updateDemo();
        }
        break;

    case TState::Loading:
        // Ordering is mechanically load-bearing: bottom, then ring, then top.
        // Same order the firmware uses, which is half the reason to test it
        // from here rather than from a scratch sketch.
        Cube.botServoExtend();
        Cube.ringExtend();
        Cube.topServoExtend();
        showScreen(Op::Done, "Load", "Cube clamped");
        break;

    case TState::Ejecting:
        Cube.unloadCube();
        Cube.botServoPartial();
        showScreen(Op::Done, "Eject", "Cube ejected");
        break;
    }
}
