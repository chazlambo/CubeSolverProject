// =============================================================================
//  CubeSolver — main firmware
// =============================================================================
//
//  THIS IS THE PROGRAM. Flash this sketch.
//
//  Everything under Code/Test Code/ is bring-up and calibration tooling; this
//  is the application. It replaces the straight-line scripts that used to live
//  in Test_Design_Day_Solve and Test_Scan_Solve_Screen, which ran
//  scan -> solve -> load -> execute top to bottom with blocking waits and then
//  parked in while(1) forever — so a second solve needed the reset button, and
//  a failure parked the machine gripping the cube.
//
//  Board:  Teensy 4.1
//  Setup:  set Arduino's sketchbook location to Code/ so that
//          #include <CubeSystem.h> resolves against Code/libraries/.
//          lv_conf.h lives at Code/libraries/lv_conf.h.
//
//  Structure
//  ---------
//  loop() is a state machine and always returns. All the long operations
//  (scanCube, executeSolve, the servo sweeps) internally call pumpDelay(),
//  which refreshes the display and polls input while they wait — so the UI
//  stays alive and SELECT-held aborts even mid-scan.
//
//  Display ownership: CubeDisplay owns the panel and LVGL. This sketch never
//  touches lv_* or the ILI9341 driver directly. Two of the old sketches stood
//  up their own driver and LVGL instance on the same pins while CubeSystem was
//  also creating one — don't reintroduce that.
//
//  Adding a screen later: add an AppState, add a case, and drive it through
//  Cube.displaySetMessage()/displaySetStatus(). Nothing else needs to change.
// =============================================================================

#include <CubeSystem.h>
#include <CubeHardwareConfig.h>

CubeSystem Cube;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
enum class AppState : uint8_t {
    Boot,        // splash while begin() runs
    Menu,        // pick an action
    AwaitCube,   // "insert a scrambled cube"      -> SELECT
    Scanning,    // scanCube()
    Solving,     // solveVirtual()
    AwaitGo,     // "N moves, ready"               -> SELECT
    Loading,     // clamp the cube
    Executing,   // executeSolve()
    Unloading,   // release the cube
    Done,        // result                          -> SELECT returns to Menu
    Error        // human-readable fault            -> SELECT returns to Menu
};

static AppState state = AppState::Boot;

static const char* kMenuItems[] = {   // non-const: entry 4 rewrites its own label
    "Solve Cube",
    "Calibrate Motors",
    "Calibrate Colors",
    "Home Motors",
    "Align Log: OFF"        // toggles CubeSystem::debugAlignLog (E1 diagnostic)
};
static const int kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);
static int  menuIndex = 0;

static int      lastError   = 0;
static uint32_t solveStart  = 0;
static uint32_t solveMillis = 0;

// ---------------------------------------------------------------------------
//  Input: turn polled levels into single edge events
// ---------------------------------------------------------------------------
//
// The encoder read is an instantaneous I2C level check, so a press is only seen
// if we happen to poll while it is held. Polling here plus inside pumpDelay()
// means presses are caught during long operations too.
enum class Ev : uint8_t { None, Up, Down, Select };

static bool     prevSelect = false;
static bool     prevUp     = false;
static bool     prevDown   = false;
static int32_t  prevPos    = 0;
static uint32_t lastPoll   = 0;

static Ev pollEvent() {
    if (!Cube.encoderInitialized) return Ev::None;

    uint32_t now = millis();
    if (now - lastPoll < 25) return Ev::None;   // ~40 Hz; also debounces
    lastPoll = now;

    // SELECT is sampled and reported FIRST.
    //
    // Reading rotation first and early-returning on any change lets encoder
    // noise starve the button indefinitely — with SELECT held down throughout,
    // the menu keeps scrolling and the press is never seen, then eventually
    // lands on whatever item the drift settled on. On a machine sitting next to
    // seven stepper drivers that is not hypothetical, and the item it lands on
    // might be "Calibrate Motors", which immediately drives all six.
    bool sel  = menuEncoder.selectPressed();
    bool edge = (sel && !prevSelect);
    prevSelect = sel;

    // Rotation is still consumed even when SELECT wins, so prevPos stays in
    // step and the deferred movement isn't replayed later.
    int32_t pos = menuEncoder.getPosition();
    bool rotated = (pos != prevPos);
    bool rotUp   = (pos > prevPos);
    prevPos = pos;

    if (edge)    return Ev::Select;
    if (rotated) return rotUp ? Ev::Up : Ev::Down;

    // Direction buttons double as up/down so the wheel isn't the only way in.
    // Edge-detected like SELECT: these are instantaneous level reads, so
    // returning them raw made a held button emit an event every 25 ms and spin
    // the 4-item menu at 40 items/second.
    bool up   = menuEncoder.upPressed();
    bool down = menuEncoder.downPressed();
    bool upEdge   = (up   && !prevUp);
    bool downEdge = (down && !prevDown);
    prevUp   = up;
    prevDown = down;

    if (upEdge)   return Ev::Up;
    if (downEdge) return Ev::Down;

    return Ev::None;
}

// ---------------------------------------------------------------------------
//  Display helpers
// ---------------------------------------------------------------------------
static void show(const char* msg, const char* status = nullptr) {
    Cube.displaySetMessage(msg);
    if (status) Cube.displaySetStatus(status);
    else        Cube.displayClearStatus();
    Cube.displayUpdate();
    Serial.println(msg);
}

static void showMenu() {
    char buf[96];
    snprintf(buf, sizeof(buf), "> %s", kMenuItems[menuIndex]);
    char sub[64];
    snprintf(sub, sizeof(sub), "%d/%d   turn to change, press to run",
             menuIndex + 1, kMenuCount);
    show(buf, sub);
}

// Human-readable fault text. The machine used to display bare integers like
// "Err 122", whose meaning existed only in a comment block in CubeSystem.cpp.
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
    char buf[96];
    snprintf(buf, sizeof(buf), "%s", what);
    char sub[96];
    if (Cube.lastFault != 0 && Cube.lastFault != code) {
        snprintf(sub, sizeof(sub), "%s  (code %d, fault %d)", detail, code, Cube.lastFault);
    } else {
        snprintf(sub, sizeof(sub), "%s  (code %d)", detail, code);
    }
    show(buf, sub);
    state = AppState::Error;
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

    // Seed the rotary baseline from the encoder's ACTUAL count. It is a free
    // running absolute counter, so leaving prevPos at 0 makes the first poll
    // register a large phantom rotation and jump the menu selection.
    if (Cube.encoderInitialized) prevPos = menuEncoder.getPosition();

    state = AppState::Menu;
    showMenu();
}

void loop() {
    // Always service the display, whatever state we are in.
    Cube.displayUpdate();

    Ev ev = pollEvent();

    switch (state) {

    case AppState::Boot:
        state = AppState::Menu;
        showMenu();
        break;

    case AppState::Menu:
        if (ev == Ev::Up || ev == Ev::Down) {
            menuIndex += (ev == Ev::Up) ? 1 : -1;
            if (menuIndex < 0)           menuIndex = kMenuCount - 1;
            if (menuIndex >= kMenuCount) menuIndex = 0;
            showMenu();
        } else if (ev == Ev::Select) {
            Cube.clearAbort();
            switch (menuIndex) {
            case 0:
                state = AppState::AwaitCube;
                show("Insert a scrambled cube", "Press SELECT when loaded");
                break;
            case 1: {
                show("Calibrating motors...", "Do not touch the machine");
                int e = Cube.calibrateMotorRotations();
                if (e) fail("Motor calibration failed", calibErrorText(e), e);
                else   { show("Motors calibrated", "Press SELECT"); state = AppState::Done; }
                break;
            }
            case 2: {
                show("Calibrating colours...", "Needs a SOLVED cube");
                int e = Cube.calibrateColorSensors();
                if (e) fail("Colour calibration failed", calibErrorText(e), e);
                else   { show("Colours calibrated", "Press SELECT"); state = AppState::Done; }
                break;
            }
            case 3: {
                show("Homing motors...", "");
                int e = Cube.homeMotors();
                if (e) fail("Homing failed", "Check encoders and jams", e);
                else   { show("Motors homed", "Press SELECT"); state = AppState::Done; }
                break;
            }
            case 4: {
                // E1: per-move alignment error logging. Off by default because
                // it prints once per moved motor per move.
                Cube.debugAlignLog = !Cube.debugAlignLog;
                kMenuItems[4] = Cube.debugAlignLog ? "Align Log: ON" : "Align Log: OFF";
                showMenu();
                break;
            }
            }
        }
        break;

    case AppState::AwaitCube:
        if (ev == Ev::Select) {
            // Clear first: pollEvent() fires on the SELECT rising edge, so a
            // user who presses and HOLDS to start would otherwise latch an
            // abort that scanCube() honours on its very first iteration.
            Cube.clearAbort();
            show("Scanning cube...", "Hold SELECT to abort");
            state = AppState::Scanning;
        }
        break;

    case AppState::Scanning: {
        int e = Cube.scanCube();
        if (e) {
            fail("Scan failed", scanErrorText(e), e);
        } else {
            show("Solving...", "");
            state = AppState::Solving;
        }
        break;
    }

    case AppState::Solving: {
        int e = Cube.solveVirtual();
        if (e) {
            fail("Solve failed", solveErrorText(e), e);
        } else {
            char sub[64];
            snprintf(sub, sizeof(sub), "%d moves - press SELECT to run",
                     Cube.solutionLength);
            show("Solution found", sub);
            state = AppState::AwaitGo;
        }
        break;
    }

    case AppState::AwaitGo:
        if (ev == Ev::Select) {
            Cube.clearAbort();          // see the note in AwaitCube
            show("Loading cube...", "");
            state = AppState::Loading;
        }
        break;

    case AppState::Loading:
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

        show("Solving cube", "Hold SELECT to abort");
        solveStart = millis();
        state = AppState::Executing;
        break;

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

    case AppState::Done:
        if (ev == Ev::Select) {
            state = AppState::Menu;
            showMenu();
        }
        break;

    case AppState::Error:
        // Unlike the old sketches, an error is not a dead end: the cube has
        // already been released and SELECT returns to the menu.
        if (ev == Ev::Select) {
            Cube.clearAbort();
            state = AppState::Menu;
            showMenu();
        }
        break;
    }
}
