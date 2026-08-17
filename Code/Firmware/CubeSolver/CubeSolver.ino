// =============================================================================
//  CubeSolver — main firmware
// =============================================================================
//
//  THIS IS THE PROGRAM. Flash this sketch.
//
//  Everything under Code/Test Code/ is bring-up and calibration tooling; this
//  is the application.
//
//  Board:  Teensy 4.1
//  Setup:  set Arduino's sketchbook location to Code/ so that
//          #include <CubeSystem.h> resolves against Code/libraries/.
//          lv_conf.h lives at Code/libraries/lv_conf.h.
//
//  Structure
//  ---------
//  Two layers, deliberately separate:
//
//    MENU   — CubeMenu, driven by the tables below. Pure navigation: it knows
//             which screens exist and which item is selected, and nothing about
//             what any of them do. Adding a screen is adding a table.
//
//    OPS    — the AppState machine in loop(). One state per long-running
//             operation. Menu actions do not perform work; they set a state and
//             return, so the panel is repainted before the machine moves.
//
//  loop() always returns. The long operations (scanCube, executeSolve, the
//  servo sweeps) internally call pumpDelay(), which refreshes the display and
//  polls input while they wait — so the UI stays alive and the abort chord is
//  noticed even mid-scan.
//
//  Input map (see pollEvent)
//  -------------------------
//    wheel / UP / DOWN   move the cursor, or step through a screen
//    SELECT / RIGHT      enter a submenu, run an item, acknowledge a screen
//    LEFT                back out one level
//    SELECT + LEFT, 1.5s abort a running operation (handled inside CubeSystem)
//
//  Display ownership: CubeDisplay owns the panel and LVGL. This sketch never
//  touches lv_* or the ILI9341 driver directly.
//
//  What is not built yet
//  ---------------------
//  Every item marked NOT-IMPLEMENTED below lands on a placeholder screen. The
//  menu structure is complete; the screens behind it are the next job.
// =============================================================================

#include <CubeSystem.h>
#include <CubeHardwareConfig.h>
#include <CubeMenu.h>

static const char* kFirmwareVersion = "0.9.0";

CubeSystem Cube;
CubeMenu   Menu;

// ---------------------------------------------------------------------------
//  Operation states
// ---------------------------------------------------------------------------
enum class AppState : uint8_t {
    SelfTest,    // boot faults listed              -> SELECT continues
    Menu,        // CubeMenu has control
    Info,        // static text screen              -> SELECT/LEFT returns
    AwaitCube,   // "insert a scrambled cube"       -> SELECT
    Scanning,    // scanCube()
    Solving,     // solveVirtual()
    Loading,     // clamp the cube
    Executing,   // executeSolve()
    Unloading,   // release the cube
    Ejecting,    // release + present for removal
    CalMotors,   // calibrateMotorRotations()
    CalColors,   // calibrateColorSensors()
    Done,        // result                          -> SELECT returns to Menu
    Error        // human-readable fault            -> SELECT returns to Menu
};

static AppState state = AppState::SelfTest;
static int      lastError   = 0;   // kept for the Fault Log screen, still to be built
static uint32_t solveStart  = 0;
static uint32_t solveMillis = 0;

// Input edge-detection state. Declared here rather than next to pollEvent()
// because toMenu(), further up, re-seeds the rotary baseline.
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
//  The tables reference actions, and the actions reference the tables, so both
//  need declaring first. Arduino's auto-prototyping handles functions but not
//  the screen objects.
extern const MenuScreen kScreenSettings;
extern const MenuScreen kScreenCalibration;
extern const MenuScreen kScreenDiagnostics;
extern const MenuScreen kScreenModes;

static void actLoadScan();
static void actSolve();
static void actEject();
static void actStats();
static void actAbout();
static void actCalStatus();
static void actCalColors();
static void actCalMotors();
static void actNotImplemented();
static void actCubeState();

// ---------------------------------------------------------------------------
//  Menu tables
// ---------------------------------------------------------------------------
//  Five items maximum per screen — that is what fits the panel without
//  scrolling, and a screen that wants a sixth item wants splitting instead.
//  CubeMenu will scroll if pushed, but nothing here should make it.
//
//  There is no "Back" item anywhere: LEFT backs out of every screen. Adding one
//  would cost a row on screens that are already at the limit.
//
//  Presentation fields
//  -------------------
//  `caption` is the description line under the frame; keep it under about 34
//  characters or it will be ellipsised in the 182 px box. `preview` is what the
//  side pane lists — spell it out for items that open a submenu, and leave it
//  null for items that start an operation, which makes the pane draw the
//  design's placeholder graphic instead. `theme` recolours the whole frame
//  while that item is selected; omitting it inherits the screen's.

// Preview lists. These deliberately repeat their submenu's item labels rather
// than being generated from them: the pane is a teaser, it is written to fit,
// and a screen is free to show fewer entries there than it really has. The
// pane is about 54 px wide at 9 px type — roughly 13 characters — so these are
// abbreviated where the real item name would not fit.
static const char* const kPrevSettings[]    = { "Calibration", "Diagnostics", "About" };
static const char* const kPrevModes[]       = { "Scramble", "Idle Mode", "Demo Mode",
                                                "Step Solve", "Patterns" };
static const char* const kPrevCalibration[] = { "Status", "Colors", "Motors", "Servos" };
static const char* const kPrevDiagnostics[] = { "Hardware", "Sensor Test", "Parameters",
                                                "Cube State", "Fault Log" };

// ---- Main, before a cube is scanned ----
static const MenuItem kMainPreItems[] = {
    { "Load & Scan Cube", nullptr,          actLoadScan, "Read all six faces.",
      nullptr, 0, MenuTheme::Blue },
    { "Settings",         &kScreenSettings, nullptr,     "Setup and machine info.",
      kPrevSettings, 3, MenuTheme::Yellow },
    { "Stats",            nullptr,          actStats,    "View solve records.",
      nullptr, 0, MenuTheme::Purple },
};
static const MenuScreen kScreenMainPre = { "Cube Solver", kMainPreItems, 3, MenuTheme::Green };

// ---- Main, once the cube state is known ----
//
// Which of the two is the root is decided by VirtualCube::isReady(), not by a
// flag of this sketch's own — see syncMenuRoot(). That matters because
// safeStop() invalidates the virtual cube on every fault path, so the menu
// reverts to "you need to scan" without this file having to catch every case.
static const MenuItem kMainPostItems[] = {
    { "Solve",      nullptr,          actSolve, "Compute and run the solution.",
      nullptr, 0, MenuTheme::Blue },
    { "Modes",      &kScreenModes,    nullptr,  "Other ways to run it.",
      kPrevModes, 5, MenuTheme::Red },
    { "Eject Cube", nullptr,          actEject, "Release the cube.",
      nullptr, 0, MenuTheme::Violet },
    { "Settings",   &kScreenSettings, nullptr,  "Setup and machine info.",
      kPrevSettings, 3, MenuTheme::Yellow },
    { "Stats",      nullptr,          actStats, "View solve records.",
      nullptr, 0, MenuTheme::Purple },
};
static const MenuScreen kScreenMainPost = { "Cube Ready", kMainPostItems, 5, MenuTheme::Green };

// ---- Modes ----  (all NOT-IMPLEMENTED)
static const MenuItem kModesItems[] = {
    { "Scramble Solve", nullptr, actNotImplemented, "Scramble, then solve it." },
    { "Idle Mode",      nullptr, actNotImplemented, "Turn slowly while waiting." },
    { "Demo Mode",      nullptr, actNotImplemented, "Show off, unattended." },
    { "Step Solve",     nullptr, actNotImplemented, "One move at a time." },
    { "Patterns",       nullptr, actNotImplemented, "Fold the cube into shapes." },
};
const MenuScreen kScreenModes = { "Modes", kModesItems, 5, MenuTheme::Red };

// ---- Settings ----
static const MenuItem kSettingsItems[] = {
    { "Calibration", &kScreenCalibration, nullptr,  "Tune motors and sensors.",
      kPrevCalibration, 4, MenuTheme::Violet },
    { "Diagnostics", &kScreenDiagnostics, nullptr,  "Test and inspect hardware.",
      kPrevDiagnostics, 5, MenuTheme::Purple },
    { "About",       nullptr,             actAbout, "Firmware and build info." },
};
const MenuScreen kScreenSettings = { "Settings", kSettingsItems, 3, MenuTheme::Yellow };

// ---- Calibration ----
static const MenuItem kCalibrationItems[] = {
    { "Calibration Status", nullptr, actCalStatus,      "What is calibrated so far." },
    { "Color Sensors",      nullptr, actCalColors,      "Learn the six face colours." },
    { "Motor Positions",    nullptr, actCalMotors,      "Find the motor home points." },
    { "Servo Positions",    nullptr, actNotImplemented, "Set the gripper travel." },
};
const MenuScreen kScreenCalibration = { "Calibration", kCalibrationItems, 4, MenuTheme::Violet };

// ---- Diagnostics ----  (all NOT-IMPLEMENTED)
//
// NOTE: the old flat menu's "Align Log" toggle (CubeSystem::debugAlignLog) has
// no home until Parameters is built. The flag still exists and still works; it
// is just not reachable from the panel this iteration.
static const MenuItem kDiagnosticsItems[] = {
    { "Hardware Test", nullptr, actNotImplemented, "Exercise every actuator." },
    { "Sensor Test",   nullptr, actNotImplemented, "Watch the sensors live." },
    { "Parameters",    nullptr, actNotImplemented, "Tunable machine settings." },
    { "Cube State",    nullptr, actCubeState,      "Show the stored cube." },
    { "Fault Log",     nullptr, actNotImplemented, "Recent faults and errors." },
};
const MenuScreen kScreenDiagnostics = { "Diagnostics", kDiagnosticsItems, 5, MenuTheme::Purple };

// ---------------------------------------------------------------------------
//  Display helpers
// ---------------------------------------------------------------------------
using Op = CubeDisplay::OpKind;

// Every screen the machine shows while it is working goes through here, so all
// of them get the same frame, the same title position and the same hint bar as
// the menu. `kind` only picks the frame colour — it is what tells an operator
// across the room whether the machine is scanning, solving, calibrating or
// stopped, without reading a word.
static void showOp(Op kind, const char* title, const char* headline,
                   const char* hint = nullptr) {
    cubeDisplay.showOperation(kind, title, headline, hint);
    Cube.displayUpdate();
}

// A read-only status screen. SELECT or LEFT returns to wherever the menu was.
//
// Rows are "Label\tValue"; the display right-aligns the value half, which is
// what makes these read as a table rather than as a wall of text. A row with no
// tab spans the full width.
static void showInfo(const char* title, const char* const* lines, int count,
                     const char* headline = nullptr) {
    cubeDisplay.showOperation(Op::Info, title, headline, "SELECT or LEFT to go back");
    cubeDisplay.setOpLines(lines, count);
    Cube.displayUpdate();
    state = AppState::Info;
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

// Point the menu at the main list that matches the machine's actual state.
//
// VirtualCube::isReady() is the single source of truth for "we know what the
// cube looks like": scanCube() sets it, and safeStop() clears it on every fault
// path. A separate bool in this file would have to be cleared in each of those
// places and would eventually disagree with the solver.
//
// CubeMenu::setRoot() is a no-op when the root is unchanged, so calling this on
// every return to the menu does not disturb a user sitting in a submenu.
static void syncMenuRoot() {
    Menu.setRoot(Cube.virtualCube.isReady() ? &kScreenMainPost : &kScreenMainPre);
}

static void toMenu() {
    syncMenuRoot();

    // Re-seed the rotary baseline.
    //
    // pollEvent() is not called during a scan or a solve, so the encoder's free
    // running count can move a long way while the machine is busy — knocked,
    // or nudged by the user reaching for the abort. Without this, the first
    // poll back at the menu sees the whole accumulated delta as one rotation
    // and the selection jumps the moment the machine finishes working.
    if (Cube.encoderInitialized) prevPos = menuEncoder.getPosition();

    Menu.redraw();
    state = AppState::Menu;
}

// ---------------------------------------------------------------------------
//  Fault text
// ---------------------------------------------------------------------------
// The machine used to display bare integers like "Err 122", whose meaning
// existed only in a comment block in CubeSystem.cpp.
static const char* scanErrorText(int code) {
    switch (code) {
        case 1:  return "Sensor 1 could not identify a face";
        case 2:  return "Sensor 2 could not identify a face";
        case 3:  return "Two faces read the same - scan slipped";
        case 60: return "Impossible cube, repair failed. Rescan.";
        case 70: return "Scan aborted";
        case 80: return "Cube rotation failed (ROTX) - jam or encoder";
        case 81: return "Cube rotation failed (ROTZ) - jam or encoder";
        case 90: return "Colour sensor board offline - check wiring";
        default:
            if (code >= 10 && code < 20) return "Sensor 1 face rejected";
            if (code >= 20 && code < 30) return "Sensor 2 face rejected";
            if (code >= 30 && code < 40) return "Could not set orientation";
            if (code >= 40 && code < 50) return "Wrong number of a colour";
            if (code >= 50 && code < 60) return "Could not build cube";
            return "Scan failed";
    }
}

static const char* solveErrorText(int code) {
    switch (code) {
        case 11: return "Cube not scanned yet";
        case 12: return "No solution - illegal cube or timeout";
        case 13: return "Orientation misread. Rescan.";
        case 14: return "Colour misread (impossible piece). Rescan.";
        case 15: return "Solution too long";
        default: return "Solve failed";
    }
}

// Calibration faults. These codes are distinct from the scan codes even though
// both are small integers, so they need their own table — sharing scanErrorText
// would silently mislabel them.
static const char* calibErrorText(int code) {
    switch (code) {
        case 8:  return "Save failed - machine is NOT calibrated";
        case 9:  return "Aborted - EEPROM left untouched";
        case 90: return "Colour sensor board offline - check wiring";
        case CubeSystem::ERR_ENCODER_FAULT: return "Encoder unreadable";
        default: return "Check sensors and cube seating";
    }
}

static const char* execErrorText(int code) {
    // The same gesture produces two codes: 105 when the abort is seen between
    // moves, 125 when executeMove() unwinds it from inside one (20 + ERR_ABORTED,
    // then +100). Both are the user stopping the machine, not a fault.
    if (code == 100 + CubeSystem::ERR_ABORTED) return "Solve aborted";
    if (code == 100 + 20 + CubeSystem::ERR_ABORTED) return "Solve aborted";
    if (code == 100 + 20 + CubeSystem::ERR_ENCODER_FAULT) return "Encoder unreadable - check wiring";
    if (code == 1) return "Cube not ready";
    if (code == 2) return "No solution loaded";
    return "Move failed - cube released, rescan";
}

static void fail(const char* what, const char* detail, int code) {
    lastError = code;

    // The detail goes on the sub-line and only the code goes in the hint box.
    // Both used to share the hint, which is 182 px wide — long fault strings
    // like "Impossible cube, repair failed. Rescan." were ellipsised away
    // exactly when they were most worth reading.
    char hint[48];
    if (Cube.lastFault != 0 && Cube.lastFault != code) {
        snprintf(hint, sizeof(hint), "code %d, fault %d - SELECT", code, Cube.lastFault);
    } else {
        snprintf(hint, sizeof(hint), "code %d - SELECT to continue", code);
    }

    showOp(Op::Error, "Stopped", what, hint);
    cubeDisplay.setStatus(detail);
    Cube.displayUpdate();
    state = AppState::Error;
}

// ---------------------------------------------------------------------------
//  Menu actions
// ---------------------------------------------------------------------------
//  An action sets state and returns. It must not block: the panel still shows
//  the menu at this point, and the operation's own screen is not drawn until
//  loop() comes round again.

static void actLoadScan() {
    Cube.clearAbort();
    showOp(Op::Scan, "Scan", "Insert a scrambled cube", "Press SELECT when loaded");
    state = AppState::AwaitCube;
}

static void actSolve() {
    Cube.clearAbort();
    showOp(Op::Solve, "Solve", "Computing a solution");
    state = AppState::Solving;
}

static void actEject() {
    Cube.clearAbort();
    showOp(Op::Info, "Eject", "Releasing the cube");
    state = AppState::Ejecting;
}

static void actCalMotors() {
    Cube.clearAbort();
    showOp(Op::Calibrate, "Motor Calibration", "Finding home positions",
           "Do not touch the machine");
    state = AppState::CalMotors;
}

static void actCalColors() {
    Cube.clearAbort();
    showOp(Op::Calibrate, "Colour Calibration", "Learning the six colours",
           "Needs a SOLVED cube");
    state = AppState::CalColors;
}

static void actCalStatus() {
    // Separation is reported x1000 as an integer. LV_USE_FLOAT is 0 in
    // lv_conf.h and printf("%f") on this toolchain is a size/behaviour question
    // nobody needs to answer for a status screen.
    int worst1 = 9999, worst2 = 9999, ok1 = 0, ok2 = 0;
    for (int i = 0; i < 9; ++i) {
        int s1 = (int)(colorSensor1.getSensorSeparation(i) * 1000.0f);
        int s2 = (int)(colorSensor2.getSensorSeparation(i) * 1000.0f);
        if (s1 < worst1) worst1 = s1;
        if (s2 < worst2) worst2 = s2;
        if (colorSensor1.checkSensorHealth(i) == 0) ok1++;
        if (colorSensor2.checkSensorHealth(i) == 0) ok2++;
    }

    char rowMotors[48], rowColour[48], rowB1[48], rowB2[48];
    snprintf(rowMotors, sizeof(rowMotors), "Motors\t%s",
             Cube.getMotorCalibration() ? "CALIBRATED" : "NOT CALIBRATED");
    snprintf(rowColour, sizeof(rowColour), "Colour\t%s",
             Cube.getColorCalibration() ? "CALIBRATED" : "NOT CALIBRATED");
    snprintf(rowB1, sizeof(rowB1), "Board 1\t%d/9 healthy, sep %d", ok1, worst1);
    snprintf(rowB2, sizeof(rowB2), "Board 2\t%d/9 healthy, sep %d", ok2, worst2);

    const char* rows[] = {
        rowMotors, rowColour, rowB1, rowB2,
        "",
        "Separation x1000; under 20 is unusable.",
    };
    showInfo("Calibration Status", rows, 6);
}

static void actAbout() {
    char rowVer[48], rowBuilt[48];
    snprintf(rowVer, sizeof(rowVer), "Firmware\tv%s", kFirmwareVersion);
    snprintf(rowBuilt, sizeof(rowBuilt), "Built\t%s", __DATE__);

    const char* rows[] = {
        rowVer,
        rowBuilt,
        "Board\tTeensy 4.1",
        "",
        "Designed, built and programmed by",
        "Charlie Lambert",
    };
    showInfo("About", rows, 6);
}

static void actStats() {
    // NOT IMPLEMENTED. Counters need an EEPROM block of their own (there is
    // room: the existing layout uses ~2152 of the Teensy 4.1's 4284 bytes), plus
    // a magic+version header so adding a field later does not read garbage.
    const char* rows[] = {
        "Solves\tnot recorded yet",
        "Best time\tnot recorded yet",
        "Last solve\tnot recorded yet",
        "",
        "Nothing is stored yet - the EEPROM",
        "block is still to be added.",
    };
    showInfo("Stats", rows, 6);
}

// The stored virtual cube, unfolded. This is the screen that answers "does the
// machine think it is holding the cube I am holding", which until now could only
// be checked by reading a 54-character dump over Serial.
static void actCubeState() {
    char net[CubeDisplay::kNetFacelets];
    const char* fac = nullptr;
    const char* what = nullptr;

    if (Cube.virtualCube.isReady()) {
        fac  = Cube.virtualCube.getColorArray();
        what = nullptr;
    } else if (Cube.scanFacesRecorded > 0) {
        // Nothing was built, but a scan was recorded — which is exactly when
        // somebody wants to see it. Reassemble the raw per-face readings into
        // net order using the same pass/sensor table the scan display uses.
        //
        // CAVEAT, and it is why this says "last scan" rather than "cube": each
        // face is laid out as its sensor saw it, and the per-face rotation is
        // only resolved later by setOrientation(). A stray sticker shows up
        // here, but WHERE it sits within its face may be turned.
        for (int i = 0; i < CubeDisplay::kNetFacelets; ++i) net[i] = 'X';
        for (int pass = 0; pass < CubeSystem::kScanPasses; ++pass) {
            for (int sen = 0; sen < 2; ++sen) {
                const int f       = 2 * pass + sen;
                const int netFace = CubeSystem::kScanPassFaces[pass][sen];
                if (f >= Cube.scanFacesRecorded) continue;
                for (int k = 0; k < 9; ++k) net[netFace * 9 + k] = Cube.scanColor[f][k];
            }
        }
        fac  = net;
        what = "last scan, not built";
    } else {
        const char* rows[] = {
            "Nothing has been scanned yet, or the",
            "last scan was discarded by a fault.",
        };
        showInfo("Cube State", rows, 2, "No cube state");
        return;
    }

    // Nine of each colour is the cheapest check that the stored state is a
    // cube at all, and the one an operator can act on: a count that is not nine
    // says which colour was misread, which is more use than "invalid".
    static const char kOrder[6] = { 'W', 'Y', 'R', 'O', 'G', 'B' };
    int count[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < CubeDisplay::kNetFacelets; ++i) {
        for (int c = 0; c < 6; ++c) if (fac[i] == kOrder[c]) count[c]++;
    }

    char sub[80];
    if (what) {
        snprintf(sub, sizeof(sub), "W%d Y%d R%d O%d G%d B%d  -  %s",
                 count[0], count[1], count[2], count[3], count[4], count[5], what);
    } else {
        snprintf(sub, sizeof(sub), "W%d  Y%d  R%d  O%d  G%d  B%d",
                 count[0], count[1], count[2], count[3], count[4], count[5]);
    }

    cubeDisplay.showOperation(Op::Info, "Cube State", nullptr,
                              "SELECT or LEFT to go back");
    cubeDisplay.setOpCubeNet(fac);
    cubeDisplay.setStatus(sub);
    Cube.displayUpdate();
    state = AppState::Info;
}

// One placeholder for every unbuilt screen. It names itself from the item that
// invoked it, so each new table entry does not need its own stub function.
static void actNotImplemented() {
    const MenuItem* it = Menu.selectedItem();
    const char* rows[] = {
        "The menu structure is in place;",
        "this screen is still to be built.",
    };
    showInfo(it && it->label ? it->label : "Not implemented", rows, 2,
             "Not implemented yet");
}

// ---------------------------------------------------------------------------
//  Input: turn polled levels into single edge events
// ---------------------------------------------------------------------------
//
// The encoder read is an instantaneous I2C level check, so a press is only seen
// if we happen to poll while it is held. Polling here plus inside pumpDelay()
// means presses are caught during long operations too.
//
// The prev* / prevPos / lastPoll state lives up with the other globals.
static MenuEvent pollEvent() {
    if (!Cube.encoderInitialized) return MenuEvent::None;

    uint32_t now = millis();
    if (now - lastPoll < 25) return MenuEvent::None;   // ~40 Hz; also debounces
    lastPoll = now;

    // All five buttons in one transaction. Sampling them together is what makes
    // the SELECT+LEFT abort chord detectable at all, and it costs one I2C round
    // trip instead of five.
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

    // The abort chord is not a menu gesture. Swallowing it here stops a SELECT
    // from firing the highlighted item at the moment the user reaches for the
    // abort — which, on the main menu, could be "Eject Cube".
    if (selDown && leftDown) return MenuEvent::None;

    // Buttons are reported before rotation.
    //
    // Reading rotation first and early-returning on any change lets encoder
    // noise starve the buttons indefinitely — with SELECT held throughout, the
    // menu keeps scrolling and the press is never seen, then eventually lands
    // on whatever item the drift settled on. On a machine sitting next to seven
    // stepper drivers that is not hypothetical.
    if (selEdge || rightEdge) return MenuEvent::Select;
    if (leftEdge)             return MenuEvent::Back;

    // Clockwise moves DOWN the list, the way every other wheel-driven menu
    // behaves. If it feels inverted on the bench, this is the only line to flip.
    if (rotated) return clockwise ? MenuEvent::Down : MenuEvent::Up;

    if (upEdge)   return MenuEvent::Up;
    if (downEdge) return MenuEvent::Down;

    return MenuEvent::None;
}

// ---------------------------------------------------------------------------
//  Boot self-test
// ---------------------------------------------------------------------------
// Shown ONLY when something failed. A machine that came up clean goes straight
// to the menu — a checklist of ticks is a screen nobody reads, which is exactly
// how a cross in it gets missed.
static void showSelfTestFailures() {
    // One row per fault, not one string with newlines in it: the panel lays
    // rows out itself, and a fault list is exactly the case where each line
    // wants to be its own row rather than a wrapped paragraph.
    static char rows[CubeDisplay::kOpLines][64];
    const char* lines[CubeDisplay::kOpLines];
    int n = 0;

    auto addLine = [&](const char* line) {
        if (n >= CubeDisplay::kOpLines) return;
        snprintf(rows[n], sizeof(rows[n]), "%s", line);
        lines[n] = rows[n];
        ++n;
    };

    if (!Cube.encoderInitialized) addLine("Menu encoder (seesaw) not found");
    if (!Cube.colorSensorsOk)     addLine("Colour sensor board offline");
    if (!Cube.encoderMuxOk)       addLine("Motor encoder mux offline");

    // Name the motors rather than the indices. "Encoder 4 failed" means nothing
    // standing at the machine with a screwdriver.
    static const char* kMotorNames[7] = { "U", "R", "F", "D", "L", "B", "Ring" };
    char missing[64];
    int  m = 0;
    missing[0] = '\0';
    for (int i = 0; i < 7; ++i) {
        if (!Cube.motorEncoderOk[i]) {
            m += snprintf(missing + m, (m < (int)sizeof(missing)) ? sizeof(missing) - m : 0,
                          "%s%s", (m > 0) ? " " : "", kMotorNames[i]);
            if (m > (int)sizeof(missing)) m = (int)sizeof(missing);
        }
    }
    if (missing[0] != '\0') {
        char line[96];
        snprintf(line, sizeof(line), "Motor encoders offline: %s", missing);
        addLine(line);
    }

    cubeDisplay.showOperation(CubeDisplay::OpKind::Error, "Startup Faults",
                              nullptr, "SELECT to continue anyway");
    cubeDisplay.setOpLines(lines, n);
    Cube.displayUpdate();

    Serial.println(F("=== startup faults ==="));
    for (int i = 0; i < n; ++i) Serial.println(lines[i]);
    state = AppState::SelfTest;
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // begin() brings up the display first, then the rest of the hardware, and
    // registers the cooperative pump.
    Cube.begin();

    if (!Cube.displayReady()) {
        // Not fatal — displayWaitForSelect() falls back to the encoder and
        // Serial — but say so loudly rather than running a degraded machine
        // silently.
        Serial.println(F("WARNING: display unavailable, running on Serial."));
    }

    // Present the cube for removal.
    //
    // begin() leaves both servos retracted, which parks a cube already in the
    // machine right down inside the colour-sensor box where it cannot be got at
    // by hand. Lifting the bottom servo is what makes it grabbable, so it is
    // part of coming up, not an optional convenience.
    //
    // Uses the existing partial() position (3/4 travel) for now; the intended
    // eject height is lower and gets its own calibrated endpoint once Servo
    // Positions is built.
    Cube.botServoPartial();

    // Seed the rotary baseline from the encoder's ACTUAL count. It is a free
    // running absolute counter, so leaving prevPos at 0 makes the first poll
    // register a large phantom rotation and jump the menu selection.
    if (Cube.encoderInitialized) prevPos = menuEncoder.getPosition();

    Menu.begin(&kScreenMainPre, drawMenu);

    if (Cube.selfTestPassed()) {
        toMenu();
    } else {
        showSelfTestFailures();
    }
}

void loop() {
    // Always service the display, whatever state we are in.
    Cube.displayUpdate();

    MenuEvent ev = pollEvent();

    switch (state) {

    case AppState::SelfTest:
        if (ev == MenuEvent::Select) toMenu();
        break;

    case AppState::Menu:
        // Keep the main list honest every pass, not just on the way back from
        // an operation. setRoot() is a no-op while the root is unchanged, which
        // it is for all but the one pass after the cube state flips, so this
        // costs a pointer compare. It matters because the virtual cube can be
        // invalidated by something other than the operation the user just ran.
        syncMenuRoot();

        Menu.handle(ev);
        // Only redraw while the menu still has control: an action may have
        // moved us to an operation screen, and repainting the list over it
        // would leave the panel lying about what the machine is doing.
        if (state == AppState::Menu) Menu.render();
        break;

    case AppState::Info:
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) toMenu();
        break;

    case AppState::AwaitCube:
        if (ev == MenuEvent::Select) {
            // Clear first: pollEvent() fires on the SELECT rising edge, and
            // although the abort now needs LEFT as well, clearing here also
            // discards any latch left over from a previous operation.
            Cube.clearAbort();
            // The face row is filled in from inside scanCube(), which is the
            // only thing that knows what each face turned out to be.
            showOp(Op::Scan, "Scan", "Reading the cube", "SELECT+LEFT to abort");
            state = AppState::Scanning;
        } else if (ev == MenuEvent::Back) {
            toMenu();
        }
        break;

    case AppState::Scanning: {
        int e = Cube.scanCube();
        if (e) {
            // TODO: a scan review screen belongs here — the per-sticker colour,
            // runner-up and confidence are all recorded in Cube.scanColor/
            // scanAlt/scanConf, which is exactly what is needed to show WHICH
            // sticker was ambiguous instead of just a code.
            fail("Scan failed", scanErrorText(e), e);
        } else {
            // Keep the face row up: this is the one moment the operator can
            // check the machine read the cube it is actually holding.
            showOp(Op::Done, "Scan", "Scan complete", "Press SELECT");
            Cube.displayFaces(Cube.scanFaceChips);
            Cube.displayUpdate();
            state = AppState::Done;
        }
        break;
    }

    case AppState::Solving: {
        int e = Cube.solveVirtual();
        if (e) {
            fail("Solve failed", solveErrorText(e), e);
        } else {
            showOp(Op::Solve, "Solve", "Clamping the cube");
            state = AppState::Loading;
        }
        break;
    }

    case AppState::Loading: {
        // Ordering is mechanically load-bearing: bottom, then ring, then top.
        Cube.botServoExtend();
        Cube.ringExtend();
        Cube.topServoExtend();

        // If an abort landed during the clamp, release rather than starting a
        // solve. safeStop() suspends the latch internally so the unload is a
        // real unload, not a pumped-out no-op.
        if (Cube.abortPending()) {
            Cube.safeStop(CubeSystem::ERR_ABORTED);
            fail("Aborted", "Cube released", CubeSystem::ERR_ABORTED);
            break;
        }

        char sub[64];
        snprintf(sub, sizeof(sub), "%d moves to run", Cube.solutionLength);
        showOp(Op::Solve, "Solve", sub, "SELECT+LEFT to abort");
        solveStart = millis();
        state = AppState::Executing;
        break;
    }

    case AppState::Executing: {
        int e = Cube.executeSolve();
        solveMillis = millis() - solveStart;
        if (e) {
            // executeSolve() already called safeStop(), which released the cube
            // and invalidated the virtual state — so there is nothing to unload
            // and a rescan is required.
            fail("Solve stopped", execErrorText(e), e);
        } else {
            state = AppState::Unloading;
        }
        break;
    }

    case AppState::Unloading: {
        Cube.unloadCube();
        char sub[64];
        snprintf(sub, sizeof(sub), "%d moves in %lu.%02lu s",
                 Cube.solutionLength,
                 (unsigned long)(solveMillis / 1000),
                 (unsigned long)((solveMillis % 1000) / 10));
        showOp(Op::Done, "Solve", "Solved!", sub);
        state = AppState::Done;
        break;
    }

    case AppState::Ejecting:
        // Release, then lift the cube back into reach. The virtual state goes
        // with it: once the cube is out of the machine's grip we have no idea
        // whether the user turned a face, so anything derived from the old scan
        // is a guess. resetCube() is also what flips the main menu back to its
        // pre-scan form via syncMenuRoot().
        Cube.unloadCube();
        Cube.botServoPartial();
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
        showOp(Op::Done, "Eject", "Cube ejected",
               "Take the cube out, then press SELECT");
        state = AppState::Done;
        break;

    case AppState::CalMotors: {
        int e = Cube.calibrateMotorRotations();
        if (e) fail("Motor calibration failed", calibErrorText(e), e);
        else   { showOp(Op::Done, "Motor Calibration", "Motors calibrated", "Press SELECT");
                 state = AppState::Done; }
        break;
    }

    case AppState::CalColors: {
        int e = Cube.calibrateColorSensors();
        if (e) fail("Colour calibration failed", calibErrorText(e), e);
        else   { showOp(Op::Done, "Colour Calibration", "Colours calibrated", "Press SELECT");
                 state = AppState::Done; }
        break;
    }

    case AppState::Done:
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) toMenu();
        break;

    case AppState::Error:
        // An error is not a dead end: the cube has already been released and
        // SELECT returns to the menu.
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            Cube.clearAbort();
            toMenu();
        }
        break;
    }
}
