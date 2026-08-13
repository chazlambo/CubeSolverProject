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

// ---------------------------------------------------------------------------
//  Menu tables
// ---------------------------------------------------------------------------
//  Five items maximum per screen — that is what fits the panel without
//  scrolling, and a screen that wants a sixth item wants splitting instead.
//  CubeMenu will scroll if pushed, but nothing here should make it.
//
//  There is no "Back" item anywhere: LEFT backs out of every screen. Adding one
//  would cost a row on screens that are already at the limit.

// ---- Main, before a cube is scanned ----
static const MenuItem kMainPreItems[] = {
    { "Load & Scan Cube", nullptr,         actLoadScan },
    { "Settings",         &kScreenSettings, nullptr    },
    { "Stats",            nullptr,         actStats    },
};
static const MenuScreen kScreenMainPre = { "Cube Solver", kMainPreItems, 3 };

// ---- Main, once the cube state is known ----
//
// Which of the two is the root is decided by VirtualCube::isReady(), not by a
// flag of this sketch's own — see syncMenuRoot(). That matters because
// safeStop() invalidates the virtual cube on every fault path, so the menu
// reverts to "you need to scan" without this file having to catch every case.
static const MenuItem kMainPostItems[] = {
    { "Solve",      nullptr,          actSolve },
    { "Modes",      &kScreenModes,    nullptr  },
    { "Eject Cube", nullptr,          actEject },
    { "Settings",   &kScreenSettings, nullptr  },
    { "Stats",      nullptr,          actStats },
};
static const MenuScreen kScreenMainPost = { "Cube Ready", kMainPostItems, 5 };

// ---- Modes ----  (all NOT-IMPLEMENTED)
static const MenuItem kModesItems[] = {
    { "Scramble Solve", nullptr, actNotImplemented },
    { "Idle Mode",      nullptr, actNotImplemented },
    { "Demo Mode",      nullptr, actNotImplemented },
    { "Step Solve",     nullptr, actNotImplemented },
    { "Patterns",       nullptr, actNotImplemented },
};
const MenuScreen kScreenModes = { "Modes", kModesItems, 5 };

// ---- Settings ----
static const MenuItem kSettingsItems[] = {
    { "Calibration", &kScreenCalibration, nullptr  },
    { "Diagnostics", &kScreenDiagnostics, nullptr  },
    { "About",       nullptr,             actAbout },
};
const MenuScreen kScreenSettings = { "Settings", kSettingsItems, 3 };

// ---- Calibration ----
static const MenuItem kCalibrationItems[] = {
    { "Calibration Status", nullptr, actCalStatus      },
    { "Color Sensors",      nullptr, actCalColors      },
    { "Motor Positions",    nullptr, actCalMotors      },
    { "Servo Positions",    nullptr, actNotImplemented },
};
const MenuScreen kScreenCalibration = { "Calibration", kCalibrationItems, 4 };

// ---- Diagnostics ----  (all NOT-IMPLEMENTED)
//
// NOTE: the old flat menu's "Align Log" toggle (CubeSystem::debugAlignLog) has
// no home until Parameters is built. The flag still exists and still works; it
// is just not reachable from the panel this iteration.
static const MenuItem kDiagnosticsItems[] = {
    { "Hardware Test", nullptr, actNotImplemented },
    { "Sensor Test",   nullptr, actNotImplemented },
    { "Parameters",    nullptr, actNotImplemented },
    { "Cube State",    nullptr, actNotImplemented },
    { "Fault Log",     nullptr, actNotImplemented },
};
const MenuScreen kScreenDiagnostics = { "Diagnostics", kDiagnosticsItems, 5 };

// ---------------------------------------------------------------------------
//  Display helpers
// ---------------------------------------------------------------------------
static void show(const char* msg, const char* status = nullptr) {
    Cube.displaySetMessage(msg);
    if (status) Cube.displaySetStatus(status);
    else        Cube.displayClearStatus();
    Cube.displayUpdate();
}

// A read-only text screen. SELECT or LEFT returns to wherever the menu was.
static void showInfo(const char* title, const char* body) {
    cubeDisplay.showMessage(title, body, "SELECT or LEFT to go back");
    Cube.displayUpdate();
    state = AppState::Info;
}

static void drawMenu(const char*        title,
                     const char* const* labels,
                     const bool*        chevron,
                     uint8_t            rows,
                     uint8_t            selectedRow,
                     bool               moreAbove,
                     bool               moreBelow) {
    cubeDisplay.showList(title, labels, chevron, rows, selectedRow,
                         moreAbove, moreBelow);
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
    char sub[96];
    if (Cube.lastFault != 0 && Cube.lastFault != code) {
        snprintf(sub, sizeof(sub), "%s  (code %d, fault %d)", detail, code, Cube.lastFault);
    } else {
        snprintf(sub, sizeof(sub), "%s  (code %d)", detail, code);
    }
    show(what, sub);
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
    show("Insert a scrambled cube", "Press SELECT when loaded");
    state = AppState::AwaitCube;
}

static void actSolve() {
    Cube.clearAbort();
    show("Solving...", "");
    state = AppState::Solving;
}

static void actEject() {
    Cube.clearAbort();
    show("Ejecting cube...", "");
    state = AppState::Ejecting;
}

static void actCalMotors() {
    Cube.clearAbort();
    show("Calibrating motors...", "Do not touch the machine");
    state = AppState::CalMotors;
}

static void actCalColors() {
    Cube.clearAbort();
    show("Calibrating colours...", "Needs a SOLVED cube");
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

    char body[224];
    snprintf(body, sizeof(body),
             "Motors:  %s\n"
             "Colour:  %s\n"
             "Board 1: %d/9 healthy, min sep %d\n"
             "Board 2: %d/9 healthy, min sep %d\n"
             "(sep x1000; under 20 unusable)",
             Cube.getMotorCalibration() ? "CALIBRATED" : "NOT CALIBRATED",
             Cube.getColorCalibration() ? "CALIBRATED" : "NOT CALIBRATED",
             ok1, worst1, ok2, worst2);
    showInfo("Calibration Status", body);
}

static void actAbout() {
    char body[192];
    snprintf(body, sizeof(body),
             "CubeSolver firmware v%s\n"
             "Built %s %s\n\n"
             "Designed, built and programmed by\n"
             "Charlie Lambert",
             kFirmwareVersion, __DATE__, __TIME__);
    showInfo("About", body);
}

static void actStats() {
    // NOT IMPLEMENTED. Counters need an EEPROM block of their own (there is
    // room: the existing layout uses ~2152 of the Teensy 4.1's 4284 bytes), plus
    // a magic+version header so adding a field later does not read garbage.
    showInfo("Stats",
             "Solves:    not recorded yet\n"
             "Best time: not recorded yet\n\n"
             "Statistics are not stored yet - the\n"
             "EEPROM block is still to be added.");
}

// One placeholder for every unbuilt screen. It names itself from the item that
// invoked it, so each new table entry does not need its own stub function.
static void actNotImplemented() {
    const MenuItem* it = Menu.selectedItem();
    showInfo(it && it->label ? it->label : "Not implemented",
             "Not implemented yet.\n\n"
             "The menu structure is in place; this\n"
             "screen is still to be built.");
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
    char body[256];
    int  n = 0;
    body[0] = '\0';

    auto addLine = [&](const char* line) {
        n += snprintf(body + n, (n < (int)sizeof(body)) ? sizeof(body) - n : 0, "%s\n", line);
        if (n > (int)sizeof(body)) n = (int)sizeof(body);
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

    cubeDisplay.showMessage("Startup Faults", body, "SELECT to continue anyway");
    Cube.displayUpdate();
    Serial.println(F("=== startup faults ==="));
    Serial.print(body);
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
            show("Scanning cube...", "SELECT+LEFT to abort");
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
            show("Scan complete", "Press SELECT");
            state = AppState::Done;
        }
        break;
    }

    case AppState::Solving: {
        int e = Cube.solveVirtual();
        if (e) {
            fail("Solve failed", solveErrorText(e), e);
        } else {
            show("Loading cube...", "");
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
        snprintf(sub, sizeof(sub), "%d moves - SELECT+LEFT to abort",
                 Cube.solutionLength);
        show("Solving cube", sub);
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
        show("Solved!", sub);
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
        show("Cube ejected", "Take the cube out, then press SELECT");
        state = AppState::Done;
        break;

    case AppState::CalMotors: {
        int e = Cube.calibrateMotorRotations();
        if (e) fail("Motor calibration failed", calibErrorText(e), e);
        else   { show("Motors calibrated", "Press SELECT"); state = AppState::Done; }
        break;
    }

    case AppState::CalColors: {
        int e = Cube.calibrateColorSensors();
        if (e) fail("Colour calibration failed", calibErrorText(e), e);
        else   { show("Colours calibrated", "Press SELECT"); state = AppState::Done; }
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
