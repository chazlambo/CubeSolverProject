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
//  Nothing lands on a placeholder any more: every menu item has a real screen
//  behind it, backed by the machine. One thing is left out on purpose rather
//  than unbuilt:
//
//    - Stats has no reset gesture. What a reset should spare — the fault
//      history? the lifetime run clock? — is a design question first.
// =============================================================================

#include <CubeSystem.h>
#include <CubeHardwareConfig.h>
#include <CubeMenu.h>
#include <CubePump.h>       // pumpOnce(), for wait states that hold the cube
#include <CubeTuneTable.h>  // the shared tuning table: tuneLoadAll() at boot

static const char* kFirmwareVersion = "1.0.0";

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
    CalColorsPrompt, // "load the cube like THIS"        -> SELECT starts
    CalColors,   // calibrateColorSensors()
    Done,        // result                          -> SELECT returns to Menu
    Error,       // human-readable fault            -> SELECT returns to Menu

    // The Modes menu runs as per-move states: one executeMove() (~0.3 s,
    // internally pumped) per loop() pass, so pollEvent() at the top of loop()
    // samples the buttons at every move boundary. A blocking run-the-list
    // routine could not offer that — the pump only detects the abort chord,
    // and Demo Mode's graceful SELECT exit needs a real event poll between
    // moves, not just an abort latch.
    ModeClamp,       // clamp bottom -> ring -> top, then hand off per runMode
    ModeScrambling,  // run s_runList one move per pass (scramble or pattern fold)
    ModeComputing,   // "Computing" painted FIRST, then solveVirtual() blocks
    ModeExecuting,   // run solveMoves one move per pass; ribbon + progress
    DemoRest,        // Demo's timed "Solved!" pause between runs
    StepReady,       // Step Solve waits; SELECT runs the next solution move
    Idle,            // dial + timer + random turns
    ResetConfirm,    // red confirm before Reset Defaults wipes the tuning

    // Not modes: the Hardware Test jog page and the tuning value editor. Each
    // owns the input while it is up — loop() hands it to jogLoop() or
    // paramsLoop() and returns BEFORE pollEvent() runs, because both pages
    // need the wheel and the UP/DOWN buttons to mean different things and
    // pollEvent() deliberately collapses them into one.
    Jog,
    Params,

    // The Sensor Test pages. Live readouts, not operations: nothing moves,
    // the machine just shows what its senses report. Sensors and SensorRaw
    // are the color-board pair — the 18-sensor list, then one sensor's raw
    // numbers — Motors is the seven encoder angles, InputReport the wheel
    // and buttons. They use pollEvent(), not pollJog(): the wheel moves a
    // cursor (or nothing), which is exactly the meaning pollEvent() gives it.
    Sensors,
    SensorRaw,
    Motors,
    InputReport,

    // The Fault Log page: a read-only scroll over the EEPROM ring fail()
    // appends to. Unlike the sensor pages nothing here ticks — it repaints on
    // entry and on scroll, and that is all.
    FaultLog
};

static AppState state = AppState::SelfTest;
static uint32_t solveStart  = 0;
static uint32_t solveMillis = 0;

// Which mode the shared Mode* states above are running for. They branch on
// this at their end transitions, so every mode shares one clamp, one scramble
// runner and one solve runner instead of each carrying its own copy of the
// loop.
enum class RunMode : uint8_t { None, ScrambleSolve, Demo, Step, Pattern, IdleSolve };
static RunMode runMode = RunMode::None;

// Title for the shared completion screen in Unloading. Solve, Scramble Solve
// and later modes all finish there, and a "Solved!" screen titled "Solve"
// after a Scramble Solve would name the wrong operation.
static const char* s_opTitle = "Solve";

// The scramble, as pointers into CubeSystem::kFaceMoves. Deliberately NOT
// stored in solveMoves[]: safeStop() and clearSolution() wipe that array on
// every fault path, and the failure screen's ribbon still holds these
// pointers while it waits for a human.
static const char* scramblePtrs[CubeSystem::kScrambleLen];

// The move list ModeScrambling is running — the scramble above, or later a
// pattern's fixed sequence. One indirection so the state itself does not have
// to care which.
static const char* const* s_runList  = nullptr;
static int                s_runCount = 0;
static int                s_runAt    = 0;

// solveMoves[] as const char* rows for the ribbon, captured after
// solveVirtual(). Valid until safeStop()/clearSolution() — and every failure
// path leaves through fail(), whose repaint clears the ribbon before the
// pointers die.
static const char* s_solveRibbon[CubeSystem::maxMoves];
static int         execAt = 0;

// Demo Mode's loop context: which run this is (1-based — it names the run on
// every status line and on the exit screen), and when the "Solved!" rest
// pause between runs ends. The deadline is compared as a millis() difference,
// like the idle timer, so the 49-day rollover is a non-event.
static int      demoRuns  = 0;
static uint32_t restUntil = 0;

// Which pattern actPattern() chose: an index into the kPattern* tables plus
// the menu label for the screens. Captured at the action, before anything
// repaints — the states that run the fold execute long after selectedItem()
// stopped meaning this row.
static int         patIdx  = 0;
static const char* patName = "Pattern";

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
extern const MenuScreen kScreenPatterns;
extern const MenuScreen kScreenServoTune;
extern const MenuScreen kScreenParams;
extern const MenuScreen kScreenSensors;

static void actLoadScan();
static void actSolve();
static void actEject();
static void actModeScramble();
static void actModeIdle();
static void actModeDemo();
static void actModeStep();
static void actPattern();
static void actJog();
static void actSensorColors();
static void actSensorMotors();
static void actInputReport();
static void actFaultLog();
static void actTopServo();
static void actBotServo();
static void actRingPos();
static void actFaceMot();
static void actAlignPar();
static void actColorPar();
static void actResetTune();
static void actStats();
static void actAbout();
static void actCalStatus();
static void actCalColors();
static void actCalMotors();
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
//  design's placeholder graphic instead. `theme` recolors the whole frame
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
static const char* const kPrevServoTune[]   = { "Top Servo", "Bottom Servo", "Ring" };
static const char* const kPrevSensors[]     = { "Color", "Motors", "Input" };
static const char* const kPrevParams[]      = { "Face Motors", "Alignment", "Color",
                                                "Reset" };

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

// ---- Modes ----
static const MenuItem kModesItems[] = {
    { "Scramble Solve", nullptr, actModeScramble, "Scramble, then solve it." },
    { "Idle Mode",      nullptr, actModeIdle,     "Turn slowly while waiting." },
    { "Demo Mode",      nullptr, actModeDemo,     "Show off, unattended." },
    { "Step Solve",     nullptr, actModeStep,     "One move at a time." },
    { "Patterns",       &kScreenPatterns, nullptr, "Fold the cube into shapes." },
};
const MenuScreen kScreenModes = { "Modes", kModesItems, 5, MenuTheme::Red };

// ---- Patterns ----
//
// The point of this screen: the preview pane shows what each pattern
// PRODUCES — a list of names would say nothing about what you are choosing
// between. All eight MenuItem fields are spelled because previewNet is the
// LAST one, and the positional rows above stop early.
//
// Item order IS table order: each row's previewNet indexes the kPattern*
// tables by position, and actPattern() reuses selectedIndex() the same way —
// reorder one without the other and the machine folds the wrong pattern
// under the right preview.
static const MenuItem kPatternItems[] = {
    { "Checkerboard", nullptr, actPattern, "U2 D2 R2 L2 F2 B2",
      nullptr, 0, MenuTheme::Blue,   CubeSystem::kPatternNets[0] },
    { "Cube in Cube", nullptr, actPattern, "Fifteen moves.",
      nullptr, 0, MenuTheme::Green,  CubeSystem::kPatternNets[1] },
    { "Six Spot",     nullptr, actPattern, "U D' R L' F B' U D'",
      nullptr, 0, MenuTheme::Yellow, CubeSystem::kPatternNets[2] },
    { "Superflip",    nullptr, actPattern, "Every edge flipped.",
      nullptr, 0, MenuTheme::Purple, CubeSystem::kPatternNets[3] },
};
const MenuScreen kScreenPatterns = { "Patterns", kPatternItems, 4, MenuTheme::Violet };

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
    { "Calibration Status", nullptr, actCalStatus, "What is calibrated so far." },
    { "Color Sensors",      nullptr, actCalColors, "Learn the six face colors." },
    { "Motor Positions",    nullptr, actCalMotors, "Find the motor home points." },
    { "Servo Positions",    &kScreenServoTune, nullptr, "Set the gripper travel.",
      kPrevServoTune, 3, MenuTheme::Violet },
};
const MenuScreen kScreenCalibration = { "Calibration", kCalibrationItems, 4, MenuTheme::Violet };

// ---- Servo Positions ----
//
// The three tuning sections that move something you can watch, kept under
// Calibration and away from the numbers that only take effect on the next
// move — those live under Diagnostics > Parameters. Each item opens the value
// editor on one section of the shared CubeTuneTable.
static const MenuItem kServoTuneItems[] = {
    { "Top Servo",    nullptr, actTopServo, "Grips from above.",
      nullptr, 0, MenuTheme::Violet },
    { "Bottom Servo", nullptr, actBotServo, "Grips, centres and ejects.",
      nullptr, 0, MenuTheme::Blue },
    { "Ring",         nullptr, actRingPos,  "Stepper, not a servo.",
      nullptr, 0, MenuTheme::Green },
};
const MenuScreen kScreenServoTune = { "Servo Positions", kServoTuneItems, 3, MenuTheme::Violet };

// ---- Diagnostics ----
static const MenuItem kDiagnosticsItems[] = {
    { "Hardware Test", nullptr, actJog,            "Exercise every actuator." },
    { "Sensor Test",   &kScreenSensors, nullptr,   "Watch the sensors live.",
      kPrevSensors, 3, MenuTheme::Purple },
    { "Parameters",    &kScreenParams, nullptr,    "Tunable machine settings.",
      kPrevParams, 4, MenuTheme::Yellow },
    { "Cube State",    nullptr, actCubeState,      "Show the stored cube." },
    { "Fault Log",     nullptr, actFaultLog,       "Recent faults and errors." },
};
const MenuScreen kScreenDiagnostics = { "Diagnostics", kDiagnosticsItems, 5, MenuTheme::Purple };

// ---- Sensor Test ----
//
// A submenu rather than one screen because the two live boards and the input
// diagnostic answer different questions — and Diagnostics is at the five-item
// limit, so a screen that wants a sixth thing wants splitting, not squeezing.
// Item themes and captions mirror Test_Menu's Diagnostics entries for the
// same three screens.
static const MenuItem kSensorsItems[] = {
    { "Color Sensors", nullptr, actSensorColors, "Live, per board.",
      nullptr, 0, MenuTheme::Blue },
    { "Motor Sensors", nullptr, actSensorMotors, "Raw encoder angles.",
      nullptr, 0, MenuTheme::Green },
    { "Input Report",  nullptr, actInputReport,  "Live wheel and buttons.",
      nullptr, 0, MenuTheme::Purple },
};
const MenuScreen kScreenSensors = { "Sensor Test", kSensorsItems, 3, MenuTheme::Purple };

// ---- Parameters ----
//
// The value-only tuning sections. The old flat menu's "Align Log" toggle
// (CubeSystem::debugAlignLog) lives here now, as a row of the Alignment
// section rather than a menu item of its own.
static const MenuItem kParamsItems[] = {
    { "Face Motors",    nullptr, actFaceMot,   "Speed and settling time.",
      nullptr, 0, MenuTheme::Blue },
    { "Alignment",      nullptr, actAlignPar,  "How square is square enough.",
      nullptr, 0, MenuTheme::Green },
    { "Color",          nullptr, actColorPar,  "How a sticker is judged.",
      nullptr, 0, MenuTheme::Yellow },
    { "Reset Defaults", nullptr, actResetTune, "Throw away every change.",
      nullptr, 0, MenuTheme::Red },
};
const MenuScreen kScreenParams = { "Parameters", kParamsItems, 4, MenuTheme::Yellow };

// ---------------------------------------------------------------------------
//  Display helpers
// ---------------------------------------------------------------------------
using Op = CubeDisplay::OpKind;

// Every screen the machine shows while it is working goes through here, so all
// of them get the same frame, the same title position and the same hint bar as
// the menu. `kind` only picks the frame color — it is what tells an operator
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
//  Stats — the lifetime counters behind the Stats screen
// ---------------------------------------------------------------------------
//  The RAM copy of the cubeStats EEPROM block. Loaded once at boot in
//  setup(), mutated only at the recording points — the two solve-completion
//  hooks and fail() — and written back only by statsCommit() from those same
//  points. Never on a timer and never per tick: every commit is EEPROM wear,
//  and a periodic save would spend it on numbers that are only advisory.

static uint32_t statsVals[CubeStats::FieldCount];

// Uptime folds into RunSec as DELTAS, never as an absolute read: the obvious
// runBase + millis()/1000 wraps with millis() at 49.7 days, and the first
// commit after the wrap would write a SMALLER value over the stored lifetime
// counter — quietly erasing up to 49 days of run time. The delta since the
// last fold survives the wrap, because unsigned subtraction does.
static uint32_t runLastMs = 0;   // millis() as of the last fold

static uint32_t statsRunSecNow() {
    const uint32_t secs = (millis() - runLastMs) / 1000;
    statsVals[CubeStats::RunSec] += secs;
    runLastMs += secs * 1000;    // carry the sub-second remainder forward
    return statsVals[CubeStats::RunSec];
}

static void statsCommit() {
    statsRunSecNow();
    cubeStats.save(statsVals, CubeStats::FieldCount);
}

// Record one completed, machine-paced solve. Two callers: the shared
// Unloading completion, and Demo's run-completion transition — Demo keeps
// the cube clamped between runs and never reaches Unloading, so it must
// record for itself. Step Solve deliberately does NOT come here: it is
// human-paced, and a thinking pause must not become the best time.
static void statsRecordTimedSolve(uint32_t ms) {
    statsVals[CubeStats::Solves]++;
    statsVals[CubeStats::LastMs]   = ms;
    statsVals[CubeStats::TotalMs] += ms;
    if (statsVals[CubeStats::BestMs] == 0 || ms < statsVals[CubeStats::BestMs]) {
        statsVals[CubeStats::BestMs] = ms;
    }
    statsCommit();
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
        case 90: return "Color sensor board offline - check wiring";
        default:
            if (code >= 10 && code < 20) return "Sensor 1 face rejected";
            if (code >= 20 && code < 30) return "Sensor 2 face rejected";
            if (code >= 30 && code < 40) return "Could not set orientation";
            if (code >= 40 && code < 50) return "Wrong number of a color";
            if (code >= 50 && code < 60) return "Could not build cube";
            return "Scan failed";
    }
}

static const char* solveErrorText(int code) {
    switch (code) {
        case 11: return "Cube not scanned yet";
        case 12: return "No solution - illegal cube or timeout";
        case 13: return "Orientation misread. Rescan.";
        case 14: return "Color misread (impossible piece). Rescan.";
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
        case 90: return "Color sensor board offline - check wiring";
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

// Raw executeMove() codes. The modes run single moves outside executeSolve(),
// so their failures arrive without the +100 offset execErrorText() decodes —
// and the scan and calibration tables reuse the same small integers for
// different faults, which is why this cannot be folded into any of them.
static const char* moveErrorText(int code) {
    switch (code) {
        case 3:  return "Invalid move";
        case 21: return "Motors not calibrated";
        case 22: return "Move jammed - alignment timed out";
        case 24: return "Encoder unreadable - check wiring";
        case 25: return "Move aborted";
        default:
            if (code >= 10 && code < 20) return "Virtual move failed - rescan";
            return "Move failed";
    }
}

static void fail(const char* what, const char* detail, int code, uint8_t src) {
    // Every error path in the sketch already funnels through here, which
    // makes this the fault log's single recording point — one EEPROM append
    // per fault, nothing per tick. `src` names the code SPACE, not the
    // caller: the text tables above reuse the same small integers for
    // different faults, and a bare code in the log would be ambiguous the
    // same way one shared table would mislabel. It is a required parameter,
    // no default, so the compiler holds every call site to classifying its
    // fault. Recorded before the screen goes up: the red screen waits on a
    // human, and a power cut while it waits should still leave the record.
    // Aborts are logged too — an abort history is worth having, and the
    // Fault Log screen marks them as the user's doing, not the machine's.
    // The Faults counter commits here as well, for the same
    // record-before-the-wait reason.
    cubeFaultLog.append(millis() / 1000, (uint16_t)code, src);
    statsVals[CubeStats::Faults]++;
    statsCommit();

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
//  Mode helpers
// ---------------------------------------------------------------------------

// Fill scramblePtrs with kScrambleLen random moves, no two in a row on the
// same face — "R R'" cancels and "R R" merges, so a same-face pair makes the
// scramble lie about its length. Same-AXIS runs like "R L R" can still merge
// through commutation, but rejecting a whole axis buys little extra disorder
// for another rule; the bench sketch's canned scramble applies the same-face
// rule only, and this matches it. Pointers land on the shared static table,
// so nothing here has a lifetime to manage.
static void makeScramble() {
    int prevFace = -1;
    for (int i = 0; i < CubeSystem::kScrambleLen; ++i) {
        int f;
        do { f = random(6); } while (f == prevFace);
        prevFace = f;
        scramblePtrs[i] = CubeSystem::kFaceMoves[f][random(3)];
    }
}

// Shared failure tail for the moves the modes run OUTSIDE a solution — the
// scramble, a pattern fold, an idle turn. safeStop() FIRST: fail() paints a
// screen that waits on a human, and the machine must not sit clamped on a
// jammed cube while it does. Solution moves never come here — they follow
// executeSolve()'s +100 contract in ModeExecuting instead, so both kinds of
// failure read the same as their core-path equivalents.
static void modeMoveFailed(const char* what, int e) {
    Cube.safeStop(e);
    if (e == 20 + CubeSystem::ERR_ABORTED) {
        fail("Aborted", "Cube released", e, CubeFaultLog::Mode);
    } else {
        fail(what, moveErrorText(e), e, CubeFaultLog::Mode);
    }
}

// "Is the cube solved" is derived from the model, never tracked in a flag.
// The bench sketch carries a cubeScrambled bool for this; a flag here would
// have to be set by everything that disorders the cube and cleared by
// everything that solves it, and would eventually disagree with the model —
// the same argument syncMenuRoot() makes about the menu root. It matters
// because kociemba hands back a 13-move identity maneuver for a cube that is
// already solved, and this check is what stops the machine grinding through
// one to change nothing.
static const char kSolvedFacelets[55] =
    "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB";

static bool cubeIsSolved() {
    // getCubeArray() is NOT NUL-terminated: memcmp over the 54 facelets,
    // never strcmp.
    return Cube.virtualCube.isReady()
        && memcmp(Cube.virtualCube.getCubeArray(), kSolvedFacelets, 54) == 0;
}

// Hand the shared mode screen over to ModeComputing. The frame recolors NOW
// rather than at the first solve move, because this is the moment the
// scrambling stops — and the ribbon and the bar go, because a finished ribbon
// and a full bar under "Computing" read as a solve that finished before it
// started. `kind` carries the phase color: green for the modes that roll
// straight into the solve, yellow — thinking — for Step Solve, which stops
// for a human the moment the computation is done.
static void toComputing(Op kind) {
    cubeDisplay.setOpKind(kind);
    cubeDisplay.setMessage("Computing the solution");
    cubeDisplay.setStatus("");
    cubeDisplay.setOpRibbon(nullptr, 0, -1);
    cubeDisplay.setOpProgress(0, 0);
    state = AppState::ModeComputing;
}

// Demo Mode's graceful exit: SELECT or LEFT ends the demo. Checked at every
// move boundary BEFORE the next move dispatches — after the dispatch the
// press would cost one extra move — and safe to take wherever it lands,
// because every demo move is tracked: even a cube abandoned mid-scramble
// leaves with the model in sync, so the menu stays honestly post-scan.
static bool demoEndRequested(MenuEvent ev) {
    if (runMode != RunMode::Demo) return false;
    if (ev != MenuEvent::Select && ev != MenuEvent::Back) return false;

    char sub[24];
    snprintf(sub, sizeof(sub), "Run %d", demoRuns);
    Cube.unloadCube();
    showOp(Op::Done, "Demo", "Demo ended");
    cubeDisplay.setStatus(sub);
    Cube.displayUpdate();
    state = AppState::Done;
    return true;
}

// Step Solve's between-moves screen. A full rebuild per press is deliberate:
// this redraws on a keypress, not on a 20 Hz tick, so the Serial-flood
// argument that bans showOperation() from the animated states does not
// apply. The ribbon is the whole screen — where you are in the solution,
// what just happened, and what is coming — which a "Move 7/21" counter
// alone cannot show.
static void drawStepReady() {
    const bool last = (execAt >= Cube.solutionLength - 1);
    char head[32];
    snprintf(head, sizeof(head), "Move %d of %d", execAt + 1, Cube.solutionLength);
    cubeDisplay.showOperation(Op::Solve, "Step Solve", head,
                              last ? "SELECT to finish"
                                   : "SELECT for the next move");
    cubeDisplay.setOpRibbon(s_solveRibbon, Cube.solutionLength, execAt);
    // Progress out of length-1, not length, so the bar reads full ON the
    // last move rather than one press after it — matching the bench demo.
    // A one-move solution (a nearly solved cube) would make that (0, 0),
    // which HIDES the bar; its only move is the last move, so show it full.
    if (Cube.solutionLength > 1) {
        cubeDisplay.setOpProgress(execAt, Cube.solutionLength - 1);
    } else {
        cubeDisplay.setOpProgress(1, 1);
    }
    Cube.displayUpdate();
}

// ---------------------------------------------------------------------------
//  Idle Mode
// ---------------------------------------------------------------------------
//  The machine turning to look alive: a random quarter turn every so often,
//  waiting for someone to walk past and press SELECT.
//
//  Three things share one screen and none of them needs a mode of its own,
//  because the wheel and SELECT are free here — there is no cursor to move and
//  nothing to enter:
//
//      wheel      how long between moves
//      SELECT     stop idling and solve it
//      LEFT       back, as everywhere
//
//  It carries the move count and the gap because those are the two things worth
//  reading from across the room, and because a screen that only said "Idle"
//  would not tell you whether the machine was working or hung.
//
//  The frame color advances with each MOVE rather than on a timer of its own.
//  That is the one place in this UI where a frame color is decorative, and
//  tying it to the moves at least makes it honest: a color change means
//  something happened, so the machine is visibly alive from further away than
//  the move counter can be read.
static const MenuTheme kIdleCycle[6] = {
    MenuTheme::Green,  MenuTheme::Blue,   MenuTheme::Violet,
    MenuTheme::Purple, MenuTheme::Yellow, MenuTheme::Red,
};

// Seconds, not milliseconds, because that is the unit the wheel steps in and
// storing what is displayed avoids a rounding disagreement between the two.
// The gap deliberately survives leaving and re-entering the mode: it is a
// setting, not per-run state, and the counters below are reset at entry.
static const int kIdleGapMin =  1;
static const int kIdleGapMax = 60;
static int      idleGapS   = 10;
static uint8_t  idleStep   = 0;      // where in the color cycle
static uint16_t idleMoves  = 0;
static uint32_t idleNextAt = 0;
static char     idleLast[4] = "";    // last move's notation; "" = none yet

// Updates pieces, not the whole screen. Called on every move and on every
// wheel detent, and showOperation() would rebuild the frame and reprint the
// title each time.
//
// The last move is the headline because it is the biggest thing on the screen
// and the only part that changes on its own. Before the first move there is
// nothing to report, so it says what it is doing instead.
static void drawIdle() {
    char gap[12], moves[24];
    snprintf(gap,   sizeof(gap),   "%d s", idleGapS);
    snprintf(moves, sizeof(moves), "%u moves", (unsigned)idleMoves);

    cubeDisplay.setMessage(idleLast[0] ? idleLast : "Ready");

    // The move count goes in the sub-line rather than a row of its own. One
    // number is not a table, and putting it there leaves the middle of the
    // screen for the dial - which is the only thing here worth looking at
    // from any distance.
    cubeDisplay.setStatus(moves);
    cubeDisplay.setOpDial(idleGapS, kIdleGapMin, kIdleGapMax, gap, "between moves");

    // setOpTheme, not setOpKind: everywhere else the frame color reports
    // machine state, and this decorative cycle is the one sanctioned
    // exception — Purple is not reachable through the OpKind mapping at all.
    cubeDisplay.setOpTheme(kIdleCycle[idleStep]);
    Cube.displayUpdate();
}

// One random quarter turn, for real. Columns 0-1 of the shared move table —
// quarter turns only, the same set the bench demo shows and the jog page
// sends.
static void idleTurn() {
    const char* mv = CubeSystem::kFaceMoves[random(6)][random(2)];

    // moveVirtual ON, unlike the bench demo's canned version: the mode is
    // only reachable post-scan, so the model is ready and must follow every
    // turn — SELECT hands the result to the solver, and an untracked turn
    // would have it solve a state the cube is no longer in. Aligned, so a
    // jam is caught on the turn that causes it, not hours of turns later.
    int e = Cube.executeMove(mv, true, true);
    if (e != 0) {
        modeMoveFailed("Idle stopped", e);
        return;
    }

    snprintf(idleLast, sizeof(idleLast), "%s", mv);
    idleMoves++;
    idleStep = (uint8_t)((idleStep + 1) % 6);

    idleNextAt = millis() + (uint32_t)idleGapS * 1000UL;
    drawIdle();
}

// ---------------------------------------------------------------------------
//  Hardware Test — the jog page
// ---------------------------------------------------------------------------
//  Ported from Test_Menu's Actuators page — change both. A menu is the wrong
//  shape for this: driving a servo through Extend, Partial and Retract is nine
//  menu entries across three screens once the ring is in too, and the face
//  motors are twelve more — all of it clicked through one item at a time, when
//  what you actually want is to pick a thing and nudge it while you watch it
//  move.
//
//  So this page is direct manipulation instead, and it can be because the
//  wheel and the UP/DOWN BUTTONS are separate inputs on this encoder. The menu
//  collapses them into one meaning; here they get two:
//
//      wheel        choose which part
//      UP / DOWN    move that part
//      LEFT         back, exactly as everywhere else
//
//  One selector over the whole machine: three grippers, six face motors, two
//  whole-cube rotations. Eleven things, no submenus, and the wheel wraps — so
//  nothing is more than five or six detents away.
//
//  Keeping them on ONE page is not only tidiness. A face motor cannot turn
//  until the grippers are clear, so being able to see where the grippers are
//  WHILE jogging a face is the difference between a considered press and a jam.
//
//  Two levels, because a gripper POSITION is a choice and a face turn is not.
//  Scroll to a servo or the ring, SELECT to enter it, pick the position, SELECT
//  again to send. Faces and rotations are momentary — UP/DOWN fires them where
//  they stand, because a turn you want to repeat should not cost three presses.
//
//  The frame goes yellow while a gripper is entered, so "I am about to move
//  something" is visible without reading a word.
static const int kJogGrips = 3;                        // rows 0..2
static const int kJogCube  = kJogGrips;                // row 3: load OR eject
static const int kJogRows  = kJogGrips + 1;
static const int kJogFaces = 6;
static const int kJogRots  = 2;
static const int kJogCount = kJogRows + kJogFaces + kJogRots;   // 12

static int8_t jogSel    = 0;
static bool   jogArmed  = false;   // entered whatever is selected
static int8_t jogTarget = 0;       // the position SELECT would send

// Loading and ejecting are the same button, because the cube is either in the
// machine or it is not. The row is named for what pressing it will DO.
static bool   cubeLoaded = false;

static const char* const kJogRowName[3] = { "Top servo", "Bottom servo", "Ring" };
static const char* const kGripPos[3][3] = {
    { "Retract", "Partial", "Extend" },
    { "Retract", "Partial", "Extend" },
    { "Retract", "Middle",  "Extend" },
};

// Where each gripper is, as an index into the row above. -1 until it has been
// driven from here: the servos remember their position across a reset, but
// nothing exposes it, and guessing would be worse than admitting we do not know.
static int8_t gripAt[3] = { -1, -1, -1 };

// Face moves come from CubeSystem::kFaceMoves — the shared table, not a copy,
// so the notation this page sends cannot drift from the grammar the machine
// parses. Columns 0-1 are plain and prime; the jog wheel has no gesture for a
// double turn.

// The chip strip: the six faces, then the two whole-cube rotations. They belong
// in the same row because they are the same gesture — point at a thing, turn it.
static const char* const kJogCaps[8] = { "U", "R", "F", "D", "L", "B",
                                         "RotX", "RotZ" };
static const char* const kRotMove[2] = { "ROTX", "ROTZ" };

static void drawJog(const char* busy) {
    static char rows[kJogRows][40];
    const char* lines[kJogRows];
    CubeDisplay::RowMark marks[kJogRows];

    for (int i = 0; i < kJogRows; ++i) {
        char value[24] = "";
        if (i < kJogGrips) {
            if (i == jogSel && busy) {
                snprintf(value, sizeof(value), "%s", busy);
            } else if (i == jogSel && jogArmed) {
                // The candidate, not the current position: this is what SELECT
                // will send. Angle brackets in plain ASCII — the baked fonts
                // carry 0x20-0x7F and nothing else, and a missing glyph draws
                // as an empty box with no complaint.
                snprintf(value, sizeof(value), "< %s >", kGripPos[i][jogTarget]);
            } else {
                snprintf(value, sizeof(value), "%s",
                         (gripAt[i] < 0) ? "?" : kGripPos[i][gripAt[i]]);
            }
        }
        const char* name = (i < kJogGrips) ? kJogRowName[i]
                         : (cubeLoaded ? "Eject cube" : "Load cube");
        snprintf(rows[i], sizeof(rows[i]), "%s\t%s", name, value);
        lines[i] = rows[i];
        // The cursor is a marked row, not a bar: these are a readout you are
        // steering, and bar art would promise a selection that is not there.
        marks[i] = (i == jogSel) ? CubeDisplay::RowMark::Busy
                                 : CubeDisplay::RowMark::Plain;
    }

    // The hint bar carries what the buttons do, and it changes with what is
    // selected — there is no one sentence true of a servo and a face motor.
    const char* hint;
    if (busy)                            hint = "Working";
    else if (jogArmed && jogSel < kJogGrips) hint = "wheel picks - SELECT sends";
    else if (jogArmed)                   hint = "wheel turns the motor";
    else if (jogSel < kJogGrips)         hint = "SELECT to choose a position";
    else if (jogSel == kJogCube)         hint = cubeLoaded ? "SELECT to eject"
                                                           : "SELECT to load";
    else                                 hint = "SELECT to take the wheel";

    // "Hardware Test", the name of the menu item that opens this — the bench
    // sketch titles the same page "Actuators" after its own item.
    cubeDisplay.showOperation(jogArmed ? Op::Info : Op::Calibrate,
                              "Hardware Test", nullptr, hint);
    cubeDisplay.setOpLines(lines, kJogRows, marks);

    // Everything that turns lives in the strip: the six faces, then the two
    // whole-cube rotations. Same gesture — point at a thing, turn it.
    const int8_t fill[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    const int active = (jogSel >= kJogRows) ? jogSel - kJogRows : -1;
    cubeDisplay.setOpChipRow(0, fill, kJogCaps, kJogFaces + kJogRots, active, 142);

    Cube.displayUpdate();
}

static void driveGripper(int part, int pos) {
    if (part == 0) {
        if (pos == 0)      Cube.topServoRetract();
        else if (pos == 1) Cube.topServoPartial();
        else               Cube.topServoExtend();
    } else if (part == 1) {
        if (pos == 0)      Cube.botServoRetract();
        else if (pos == 1) Cube.botServoPartial();
        else               Cube.botServoExtend();
    } else {
        if (pos == 0)      Cube.ringRetract();
        else if (pos == 1) Cube.ringMiddle();
        else               Cube.ringExtend();
    }
}

// Send the entered gripper to its chosen position.
static void jogSend() {
    drawJog(kGripPos[jogSel][jogTarget]);
    driveGripper(jogSel, jogTarget);
    gripAt[jogSel] = jogTarget;
    jogArmed = false;
    drawJog(nullptr);
}

// Fire whatever turns: a face in the given direction, or a whole-cube rotation,
// which has no inverse in the move set so both buttons send the same thing.
static void jogTurn(int dir) {
    const int i = jogSel - kJogRows;
    const char* mv = (i < kJogFaces) ? CubeSystem::kFaceMoves[i][dir > 0 ? 0 : 1]
                                     : kRotMove[i - kJogFaces];

    // A jogged turn is untracked on purpose — this page is reachable pre-scan,
    // so it cannot pass moveVirtual=true and promise a ready model. Which
    // means a model that IS ready stops describing this cube the moment the
    // motor moves: the same "user turned a face while we were not looking"
    // desync the Ejecting state guards against, and keeping it would let a
    // later Solve replay a solution against a cube it no longer matches. So
    // wipe it before the move, and the main menu dropping back to its
    // pre-scan form afterwards is syncMenuRoot() reporting that honestly.
    // Gripper rows never come through here — they move no stickers, so they
    // keep the model.
    if (Cube.virtualCube.isReady()) {
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
    }

    drawJog(mv);
    // align = true: this is the screen for checking a motor lands on its
    // detent, so let the alignment pass run and report if it cannot.
    const int e = Cube.executeMove(mv, false, true);
    if (e) {
        // The firmware's failure idiom, not the bench sketch's inline error
        // screen: fail() names the fault and SELECT acknowledges it.
        // Deliberately NO auto-release — direct manipulation means an
        // operator standing at the machine, and unloading on a failed turn
        // would destroy the exact state being diagnosed.
        fail("Move failed", moveErrorText(e), e, CubeFaultLog::Jog);
        return;                          // a failure is worth stopping for
    }
    drawJog(nullptr);
}

// Load and eject drive several parts in an order that is mechanically
// load-bearing, so they go through the sequences that own it rather than being
// reassembled here. Both end at a KNOWN state, so the rows can say so instead
// of falling back to "?".
static void jogCube() {
    if (!cubeLoaded) {
        drawJog("clamping");
        Cube.botServoExtend();
        Cube.ringExtend();
        Cube.topServoExtend();
        gripAt[0] = gripAt[1] = gripAt[2] = 2;      // all extended
    } else {
        drawJog("releasing");
        Cube.unloadCube();      // ring, then top, then bottom - all retracted
        Cube.botServoEject();   // then present the cube, at the tuned height
        // The virtual state goes with the cube, as in the Ejecting state:
        // once it is out of the machine's grip we have no idea whether the
        // user turned a face, so anything derived from the old scan is a
        // guess. syncMenuRoot() reverts the menu to pre-scan on the way back.
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
        gripAt[0] = 0;          // top    retracted
        gripAt[1] = -1;         // bottom sits at the EJECT height, which is
                                //        not one of this page's three stops -
                                //        it was the same value as Partial
                                //        until the tuning table pinned them
                                //        apart, and "?" beats a label that is
                                //        only true until someone tunes it
        gripAt[2] = 0;          // ring   retracted
    }
    cubeLoaded = !cubeLoaded;
    drawJog(nullptr);
}

static void actJog() {
    // First line, like every action that runs moves: a latched abort would
    // fail every jog turn with code 25 before the operator touched anything —
    // executeMove() checks the latch per move.
    Cube.clearAbort();
    // The claimed positions only hold within one visit: between visits every
    // core operation and every tuning-editor preview moves the servos without
    // telling this page, so a remembered "Extend" would assert a position that
    // stopped being true the moment a scan ran. "?" until driven from HERE.
    // cubeLoaded stays: both of that row's actions end at a known state and
    // are safe to repeat, which is more use than a third question mark.
    gripAt[0] = gripAt[1] = gripAt[2] = -1;
    jogSel   = 0;
    jogArmed = false;
    state    = AppState::Jog;
    drawJog(nullptr);
}

// ---------------------------------------------------------------------------
//  Tuning — the value editor
// ---------------------------------------------------------------------------
//  Ported from Test_Menu's editor — change both. The one interaction the menu
//  cannot express: the wheel has to change a NUMBER, not move a cursor. Two
//  levels, the same shape the grippers on the jog page use — scroll to a row,
//  SELECT to enter it, wheel to change, SELECT to keep or LEFT to put it back.
//
//  Kept out of CubeMenu deliberately. CubeMenu is navigation-only and testable
//  on a host without a screen; editing values is a different job.
//
//  The parameter table itself lives in the library — CubeTuneTable — because
//  the index is the EEPROM slot and both sketches read the same block: two
//  per-sketch copies could drift by a row and silently hand values to the
//  wrong owners, which is why the table is shared and only this editor UI is
//  duplicated. (See CubeTuneTable.h for the accessors-not-globals and
//  frozen-order essays.)
static const TuneSection* parSec  = nullptr;
static int8_t             parSel  = 0;
static bool               parEdit = false;
static int32_t            parWas  = 0;   // value on entering edit, for LEFT
static bool               parGate = false;  // showing the confirm for a gated row
static bool               parMoved = false; // a live row moved a servo

// Render one value. Everything the flags mean, in one place.
static void tuneFormat(const TuneParam& p, int32_t v, char* out, size_t n) {
    if (p.flags & TP_BOOL)      snprintf(out, n, "%s", v ? "On" : "Off");
    else if (p.flags & TP_ENUM) snprintf(out, n, "%s",
                                         p.names ? p.names[v % 6] : "?");
    // Two decimals by hand: %f drags in floating-point printf, which on this
    // core is a linker flag away and several KB of flash for one screen.
    else if (p.flags & TP_HUND) snprintf(out, n, "%ld.%02ld",
                                         (long)(v / 100), (long)(v % 100));
    else                        snprintf(out, n, "%ld", (long)v);
}

static void drawTune() {
    static char rows[7][44];
    const char* lines[7];
    CubeDisplay::RowMark marks[7];

    const int n = parSec->count;
    for (int i = 0; i < n; ++i) {
        const TuneParam& p = kTune[parSec->first + i];
        char value[24], shown[32];
        tuneFormat(p, p.get(), value, sizeof(value));
        if (i == parSel && parEdit) {
            // Angle brackets in plain ASCII: the baked fonts carry 0x20-0x7F
            // and nothing else, and a missing glyph draws as an empty box
            // without a word of complaint.
            snprintf(shown, sizeof(shown), "< %s >", value);
        } else {
            snprintf(shown, sizeof(shown), "%s", value);
        }
        snprintf(rows[i], sizeof(rows[i]), "%s\t%s", p.name, shown);
        lines[i] = rows[i];
        marks[i] = (i == parSel) ? CubeDisplay::RowMark::Busy
                                 : CubeDisplay::RowMark::Plain;
    }

    const TuneParam& sel = kTune[parSec->first + parSel];

    // The hint bar answers "what am I allowed to enter", which only matters
    // once you are entering something. Browsing, it says how to start.
    char hint[48];
    if (parEdit && (sel.flags & (TP_BOOL | TP_ENUM))) {
        snprintf(hint, sizeof(hint), "wheel picks - SELECT keeps");
    } else if (parEdit && (sel.flags & TP_HUND)) {
        snprintf(hint, sizeof(hint), "%ld.%02ld to %ld.%02ld",
                 (long)(sel.lo / 100), (long)(sel.lo % 100),
                 (long)(sel.hi / 100), (long)(sel.hi % 100));
    } else if (parEdit) {
        snprintf(hint, sizeof(hint), "%s - %ld to %ld",
                 sel.units, (long)sel.lo, (long)sel.hi);
    } else {
        snprintf(hint, sizeof(hint), "SELECT to change");
    }

    // Yellow while editing: "you are changing something" without reading a
    // word, the same signal the grippers use when entered.
    cubeDisplay.showOperation(parEdit ? Op::Info : Op::Calibrate,
                              parSec->title, nullptr, hint);

    // The description goes in the sub-line, above the rows and below the title.
    // It has to be set BEFORE setOpLines(), which reads it to decide where the
    // rows start — a sub-line added afterwards lands on top of row one.
    cubeDisplay.setStatus(sel.help);
    cubeDisplay.setOpLines(lines, n, marks);
    Cube.displayUpdate();
}

// The confirm shown before a gated row can be edited.
//
// It has to come BEFORE editing, not before committing. These rows preview as
// the wheel turns, so by the time you would confirm a value the horn has
// already been to it — the damage is done during the edit, not at the end of
// it. What is being confirmed is "I am about to move this part", which is why
// it names the part and says what will follow the wheel.
static void drawTuneGate() {
    const TuneParam& p = kTune[parSec->first + parSel];
    char head[64];
    snprintf(head, sizeof(head), "Move %s %s?", parSec->title, p.name);

    cubeDisplay.showOperation(Op::Error, parSec->title, head,
                              "SELECT to go on - LEFT to stop");
    cubeDisplay.setStatus((p.flags & TP_LIVE)
                          ? "The part follows the wheel at once"
                          : "Takes effect on the next move");
    Cube.displayUpdate();
}

static void tuneEnter(const TuneSection* sec) {
    parSec   = sec;
    parSel   = 0;
    parEdit  = false;
    parGate  = false;
    parMoved = false;
    state    = AppState::Params;
    drawTune();
}

// Section entry clears the abort latch, like every action that can move
// hardware: the LIVE rows preview through pumped servo sweeps, and a latched
// abort would collapse every preview to a no-op with nothing on screen to
// say why.
static void actTopServo() { Cube.clearAbort(); tuneEnter(&kSecTopServo); }
static void actBotServo() { Cube.clearAbort(); tuneEnter(&kSecBotServo); }
static void actRingPos()  { Cube.clearAbort(); tuneEnter(&kSecRing);     }
static void actFaceMot()  { Cube.clearAbort(); tuneEnter(&kSecFaces);    }
static void actAlignPar() { Cube.clearAbort(); tuneEnter(&kSecAlign);    }
static void actColorPar() { Cube.clearAbort(); tuneEnter(&kSecColor);    }

// Reset is gated like the hardware rows are, and for the same reason: it is the
// one action here that cannot be undone by turning the wheel back. The servos
// are NOT driven to their default positions afterwards - the values are what
// reset, and moving three parts at once because a menu item was picked would be
// a much bigger surprise than a stale horn. A state of its own rather than
// Test_Menu's resetConfirm flag, because this sketch already is a state
// machine; the answer is read in loop()'s ResetConfirm case.
static void actResetTune() {
    showOp(Op::Error, "Reset Defaults", "Reset ALL tuning to defaults?",
           "SELECT resets - LEFT keeps");
    state = AppState::ResetConfirm;
}

// Leaving a section. A live row has left the horn wherever it was last
// previewed, and CubeServo::begin() trusts the stored position to decide how
// far its first sweep travels — so a stale one is what arms a full-travel slam
// on the next power-up. Write it once here rather than once per detent.
static void parLeave() {
    if (parMoved) {
        topServo.persist();
        botServo.persist();
        parMoved = false;
    }
    tuneSaveAll();
    toMenu();
}

// ---------------------------------------------------------------------------
//  Sensor Test — live hardware readouts
// ---------------------------------------------------------------------------
//  Ported from Test_Menu's sensor screens — change both — with the canned
//  readings replaced by the real reads their porting comments name. Split in
//  two because they answer different questions. The color boards want "is any
//  sensor disagreeing with its neighbours", which is a picture. The motor
//  encoders want "what angle is each one reading", which is a list of numbers.
//
//  In the simulator every VEML read returns 0 and every encoder read fails,
//  so these pages show hollow chips, "unusable" and "err -3" rows. Layout and
//  navigation are what the sim verifies; representative data is what
//  Test_Menu's canned demos exist for — both stay. Real values are a bench
//  check.

// One caption per sticker on a color board. Nine of them, in the order the
// sensors are read.
static const char* const kSensorCaps[9] = { "1","2","3","4","5","6","7","8","9" };

static int8_t  senSel  = 0;    // cursor, 0..17 across both boards
static uint8_t senNext = 0;    // round-robin scan cursor, same 0..17 space

// Last classification per sensor, as a chip color index or -1 = hollow. -1
// until each sensor has actually been read: a guessed color on a diagnostic
// would be worse than an empty box.
static int8_t senFill[2][9];

// One throttle clock for whichever diagnostic page is up — only one of them
// can be, and every entry and drill-down resets it, so they need not carry
// one each.
static uint32_t diagLastTick = 0;

// One board's health, formatted as the value half of a "Board N\t..." row.
// Extracted from the Calibration Status screen so that screen and the Color
// Sensors page compute "healthy" the same way — two copies of this arithmetic
// would eventually disagree about the same hardware. Returns the healthy
// count so a caller can mark the row Good or Bad.
//
// Separation is reported x1000 as an integer. LV_USE_FLOAT is 0 in lv_conf.h
// and printf("%f") on this toolchain is a size/behaviour question nobody
// needs to answer for a status screen.
static int boardHealthRow(ColorSensor& s, char* out, size_t n) {
    int worst = 9999, ok = 0;
    for (int i = 0; i < 9; ++i) {
        const int sep = (int)(s.getSensorSeparation(i) * 1000.0f);
        if (sep < worst) worst = sep;
        if (s.checkSensorHealth(i) == 0) ok++;
    }
    snprintf(out, n, "%d/9 healthy, sep %d", ok, worst);
    return ok;
}

// Every path out of the color-sensor pages funnels through here, so the
// illumination LEDs cannot be left burning by the one exit that forgot them.
static void sensorsLeave() {
    colorSensor1.setLED(false);
    colorSensor2.setLED(false);
}

// Color letter -> name for the "Reads as" row. classify() can also return
// 'E' — the calibrated empty-chamber reference — which is a real answer worth
// naming, not a fault to file under "unusable".
static const char* sensorColorName(char c) {
    switch (c) {
        case 'W': return "White";   case 'Y': return "Yellow";
        case 'R': return "Red";     case 'O': return "Orange";
        case 'G': return "Green";   case 'B': return "Blue";
        case 'E': return "empty slot";
        default:  return "unusable";    // 'U': the classifier declined
    }
}

// Both chip rows, from the classification cache. Row 0 carries the captions —
// the display only has one caption strip — and the cursor lights whichever
// row holds the selection.
static void sensorsPaintChips() {
    cubeDisplay.setOpChipRow(0, senFill[0], kSensorCaps, 9,
                             (senSel < 9) ? senSel : -1, 104);
    cubeDisplay.setOpChipRow(1, senFill[1], nullptr, 9,
                             (senSel < 9) ? -1 : senSel - 9, 140);
}

// The board-list screen, rebuilt whole. Entry and selection changes only —
// those are keypresses, not ticks, so the Serial-flood argument that bans
// showOperation() from the animated states does not apply. The per-scan
// updates go through sensorsPaintChips() alone.
static void drawSensors() {
    char b1[32], b2[32], row1[48], row2[48], hint[40];
    const int ok1 = boardHealthRow(colorSensor1, b1, sizeof(b1));
    const int ok2 = boardHealthRow(colorSensor2, b2, sizeof(b2));
    snprintf(row1, sizeof(row1), "Board 1\t%s", b1);
    snprintf(row2, sizeof(row2), "Board 2\t%s", b2);
    const char* lines[2] = { row1, row2 };
    const CubeDisplay::RowMark marks[2] = {
        (ok1 == 9) ? CubeDisplay::RowMark::Good : CubeDisplay::RowMark::Bad,
        (ok2 == 9) ? CubeDisplay::RowMark::Good : CubeDisplay::RowMark::Bad,
    };

    snprintf(hint, sizeof(hint), "SELECT for board %d sensor %d",
             (senSel < 9) ? 1 : 2, (senSel % 9) + 1);

    cubeDisplay.showOperation(Op::Scan, "Color Sensors", nullptr, hint);
    cubeDisplay.setOpLines(lines, 2, marks);
    sensorsPaintChips();
    Cube.displayUpdate();
}

// One sensor per tick, round-robin. A full 18-sensor sweep is ~16 s of
// integration waits, which would freeze the page for exactly that long — one
// scan per tick keeps every wait under a second, and the pump inside it keeps
// the panel alive even through that.
static void sensorsScanNext() {
    const int board = senNext / 9;
    const int idx   = senNext % 9;
    ColorSensor& s  = (board == 0) ? colorSensor1 : colorSensor2;

    s.scanSingle(idx);
    if (Cube.abortPending()) return;    // The integration wait was cut short, so
                                        // currentRGBW still holds the PREVIOUS
                                        // sensor's window — filing that under this
                                        // one is the wrong-but-plausible color
                                        // scanSingle() itself bails to avoid. The
                                        // state exits on the next pass.

    // scanSingle() switches its board's LED off on the way out; re-light it so
    // the page keeps its promise that the sensors stay lit while it is up.
    s.setLED(true);

    // scanSingle() fills currentRGBW — NOT the scanVals row getScanValRow()
    // serves, which belongs to scanFace() and would still hold the last full
    // face scan, or zeros.
    //
    // Board 2 sensor 2 classifies as unusable here on the real machine — the
    // dead green channel (README). That is this screen doing its job, not a
    // bug in it.
    const ColorReading r = s.classify(idx, s.currentRGBW);
    senFill[board][idx] = r.ok ? CubeSystem::chipIndexForColor(r.color)
                               : (int8_t)-1;

    sensorsPaintChips();
    Cube.displayUpdate();
    senNext = (uint8_t)((senNext + 1) % 18);
}

// The drill-down's frame. The rows arrive with the first scan a tick later —
// the title and hint going up at once is what says the press landed.
static void drawSensorRaw() {
    char title[32];
    snprintf(title, sizeof(title), "Board %d  Sensor %d",
             (senSel < 9) ? 1 : 2, (senSel % 9) + 1);
    cubeDisplay.showOperation(Op::Scan, title, nullptr, "LEFT to go back");
    Cube.displayUpdate();
}

// One sensor, in the numbers behind the color. This is the screen for "why
// did it call that sticker orange" — the classification is a judgement made
// from four values, and until you can see them the answer is a guess.
static void sensorRawTick() {
    const int board = (senSel < 9) ? 0 : 1;
    const int idx   = senSel % 9;
    ColorSensor& s  = (board == 0) ? colorSensor1 : colorSensor2;

    s.scanSingle(idx);
    if (Cube.abortPending()) return;    // same stale-window bail as the list page
    s.setLED(true);                     // same re-light as the list page

    const ColorReading r = s.classify(idx, s.currentRGBW);

    static char rows[5][40];
    const char* lines[5];
    CubeDisplay::RowMark marks[5];
    static const char* const kChan[4] = { "Red", "Green", "Blue", "White" };
    for (int k = 0; k < 4; ++k) {
        snprintf(rows[k], sizeof(rows[k]), "%s\t%d", kChan[k], s.currentRGBW[k]);
        lines[k] = rows[k];
        marks[k] = CubeDisplay::RowMark::Plain;
    }

    // The letter is the nearest match even when the classifier would not act
    // on it — this is the screen for seeing why. The MARK carries the verdict:
    // Good only when classify() vouches for the reading.
    snprintf(rows[4], sizeof(rows[4]), "Reads as\t%s", sensorColorName(r.color));
    lines[4] = rows[4];
    marks[4] = r.ok ? CubeDisplay::RowMark::Good : CubeDisplay::RowMark::Bad;

    cubeDisplay.setOpLines(lines, 5, marks);
    Cube.displayUpdate();

    // The list page repaints from senFill when LEFT backs out; this sensor
    // was just read, so keep its chip current too.
    senFill[board][idx] = r.ok ? CubeSystem::chipIndexForColor(r.color)
                               : (int8_t)-1;
}

static void actSensorColors() {
    // First line, like every action that runs pumped waits: scanSingle()
    // waits out its integration through pumpDelay(), and a latched abort
    // collapses that wait to ~0 ms — every scan would bail without reading.
    Cube.clearAbort();
    colorSensor1.setLED(true);
    colorSensor2.setLED(true);
    senSel  = 0;
    senNext = 0;
    diagLastTick = 0;
    for (int b = 0; b < 2; ++b)
        for (int i = 0; i < 9; ++i) senFill[b][i] = -1;
    state = AppState::Sensors;
    drawSensors();
}

// Seven encoders, seven numbers. Nothing here is a picture, because an angle
// is not one — what you are checking is whether a value moves when you turn a
// face, and whether any of them is reporting an I2C error instead.
static void motorsTick() {
    static const char* const kMotorRow[7] = { "Up", "Right", "Front", "Down",
                                              "Left", "Back", "Ring" };
    static char rows[7][40];
    const char* lines[7];
    CubeDisplay::RowMark marks[7];

    for (int i = 0; i < 7; ++i) {
        // scan() returns a raw 12-bit angle, or a negative I2C error. Showing
        // the error rather than a plausible number is the point of the screen.
        const int raw = MotorEncoders[i]->scan();
        if (raw < 0) {
            snprintf(rows[i], sizeof(rows[i]), "%s\terr %d", kMotorRow[i], raw);
            marks[i] = CubeDisplay::RowMark::Bad;
        } else {
            snprintf(rows[i], sizeof(rows[i]), "%s\t%d", kMotorRow[i], raw);
            marks[i] = CubeDisplay::RowMark::Plain;
        }
        lines[i] = rows[i];
    }

    cubeDisplay.setOpLines(lines, 7, marks);
    Cube.displayUpdate();
}

static void actSensorMotors() {
    diagLastTick = 0;
    state = AppState::Motors;
    cubeDisplay.showOperation(Op::Solve, "Motor Sensors", nullptr,
                              "raw angle 0-4095   LEFT back");
    Cube.displayUpdate();
}

// Live readout of the wheel and every button. The one screen that shows a
// flaky encoder or a dead button directly, instead of leaving you to infer it
// from a menu that scrolls oddly.
static void inputReportTick() {
    if (!Cube.encoderInitialized) {
        const char* lines[] = { "Menu encoder not found on Wire1." };
        cubeDisplay.setOpLines(lines, 1);
        Cube.displayUpdate();
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
    Cube.displayUpdate();
}

static void actInputReport() {
    diagLastTick = 0;
    state = AppState::InputReport;
    cubeDisplay.showOperation(Op::Info, "Input Report", nullptr,
                              "SELECT or LEFT to go back");
    Cube.displayUpdate();
}

// ---------------------------------------------------------------------------
//  Fault Log — the EEPROM ring, on the panel
// ---------------------------------------------------------------------------
//  Screen shape rehearsed in Test_Menu's Fault Log demo — change both. The
//  entries come from cubeFaultLog, which fail() appends to; this page only
//  reads.

static int8_t faultTop = 0;     // first visible entry, 0 = newest

// "12m34s" / "3h05m". The ring stores seconds since that boot, because uptime
// is the only clock this machine has — no RTC, so a wall-clock time would be
// an invention. After a reboot an old entry still shows how long ITS boot had
// been running when it faulted, which is what "when" can honestly mean here.
static void fmtUptime(uint32_t s, char* out, size_t n) {
    if (s >= 3600) {
        snprintf(out, n, "%luh%02lum",
                 (unsigned long)(s / 3600), (unsigned long)((s % 3600) / 60));
    } else {
        snprintf(out, n, "%lum%02lus",
                 (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
}

// The codes the abort chord produces, across every code space that can land
// in the log: ERR_ABORTED raw (5), through executeMove (25), the calibration
// abort (9), the scan abort (70), and the two solution forms (105 seen at a
// move boundary, 125 unwound from inside a move).
static bool isAbortCode(uint16_t code) {
    return code == CubeSystem::ERR_ABORTED || code == 9 || code == 25 ||
           code == 70 || code == 105 || code == 125;
}

// A list nobody selects from, so status rows rather than bars — the rule the
// scan screen learned the hard way. More entries than fit, so it scrolls; the
// position goes in the HINT BAR rather than beside a scrollbar, because the
// theme has no scrollbar art and "7-13 of 24" says more than a thumb on a
// track does anyway. Rebuilt whole on entry and on scroll — keypresses, not
// ticks, so the Serial-flood argument against showOperation() does not apply.
static void drawFaultLog() {
    const int count = (int)cubeFaultLog.count();

    if (count == 0) {
        // Covers a virgin block and an invalidated one alike: an EEPROM that
        // predates the fault log reads back an invalid magic here, and the
        // honest report of that is an empty log, not garbage rows.
        cubeDisplay.showOperation(Op::Info, "Fault Log", nullptr,
                                  "SELECT or LEFT to go back");
        const char* lines[] = { "No faults recorded." };
        cubeDisplay.setOpLines(lines, 1);
        Cube.displayUpdate();
        return;
    }

    const int rows = (count < CubeDisplay::kOpLines)
                   ? count : CubeDisplay::kOpLines;

    static char text[CubeDisplay::kOpLines][40];
    const char* lines[CubeDisplay::kOpLines];
    CubeDisplay::RowMark marks[CubeDisplay::kOpLines];

    for (int i = 0; i < rows; ++i) {
        CubeFaultLog::Entry e = { 0, 0, 0, 0 };
        cubeFaultLog.get((uint8_t)(faultTop + i), e);   // in range: faultTop
                                                        // is clamped to count
        char when[12];
        fmtUptime(e.upSec, when, sizeof(when));
        // Guarded, because the byte is from EEPROM: an entry written by some
        // future build with more sources should read as "?", not index off
        // the end of the name table.
        const char* src = (e.source < CubeFaultLog::SourceCount)
                        ? CubeFaultLog::kSourceNames[e.source] : "?";
        snprintf(text[i], sizeof(text[i]), "%s  %s\t%d", when, src, (int)e.code);
        lines[i] = text[i];
        // An abort is the user stopping the machine, not the machine failing.
        // Colouring it like a fault would teach the wrong thing.
        marks[i] = isAbortCode(e.code) ? CubeDisplay::RowMark::Plain
                                       : CubeDisplay::RowMark::Bad;
    }

    char hint[40];
    snprintf(hint, sizeof(hint), "%d-%d of %d   wheel scrolls",
             faultTop + 1, faultTop + rows, count);

    cubeDisplay.showOperation(Op::Info, "Fault Log", nullptr, hint);
    cubeDisplay.setOpLines(lines, rows, marks);
    Cube.displayUpdate();
}

static void actFaultLog() {
    faultTop = 0;
    state = AppState::FaultLog;
    drawFaultLog();
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
    // The identity-maneuver guard, same as Idle's SELECT — see cubeIsSolved().
    // kociemba hands back a 13-move maneuver for a solved cube, and without
    // this the machine would clamp and grind through all of it to change
    // nothing. No unloadCube() here, unlike Idle: the core solve computes
    // BEFORE clamping, so the cube is still at rest in the bay.
    if (cubeIsSolved()) {
        showOp(Op::Done, "Solve", "Already solved", "Press SELECT");
        cubeDisplay.setStatus("Scramble it under Modes, or Eject it.");
        Cube.displayUpdate();
        state = AppState::Done;
        return;
    }
    // The core solve owns the shared completion screen while it runs; a mode
    // run earlier would otherwise leave its own name on the "Solved!" screen.
    // runMode goes back to None for the same reason — later consumers branch
    // on it, and a stale Step would misfile this solve.
    s_opTitle = "Solve";
    runMode   = RunMode::None;
    showOp(Op::Solve, "Solve", "Computing a solution");
    state = AppState::Solving;
}

static void actEject() {
    Cube.clearAbort();
    showOp(Op::Info, "Eject", "Releasing the cube");
    state = AppState::Ejecting;
}

// Scramble the cube, then solve it — the show-off round trip. Reachable only
// from the post-scan menu, so the model is ready on entry; the guard is for
// the day that stops being true, because a scramble tracked against a dead
// model would desync the cube silently.
static void actModeScramble() {
    if (!Cube.virtualCube.isReady()) {
        const char* rows[] = {
            "The cube state is not known.",
            "Run Load & Scan Cube first.",
        };
        showInfo("Scramble Solve", rows, 2, "No cube state");
        return;
    }
    Cube.clearAbort();
    // Seeded from the clock so two runs do not scramble the same way. Entry
    // time depends on how long someone spent in the menu, which is enough.
    randomSeed(millis());
    runMode   = RunMode::ScrambleSolve;
    s_opTitle = "Scramble Solve";
    // Red from the very start: the frame color carries the phase, and the
    // clamp belongs to the scramble half.
    showOp(Op::Error, "Scramble Solve", "Clamping the cube", "SELECT+LEFT to abort");
    state = AppState::ModeClamp;
}

// Clamp the cube and fidget with it until someone presses SELECT. Same
// entry guard as Scramble Solve, for the same reason: idle turns are tracked
// against the model, and tracking against a dead one would desync the cube
// silently.
static void actModeIdle() {
    if (!Cube.virtualCube.isReady()) {
        const char* rows[] = {
            "The cube state is not known.",
            "Run Load & Scan Cube first.",
        };
        showInfo("Idle Mode", rows, 2, "No cube state");
        return;
    }
    Cube.clearAbort();
    // Seeded from the clock so two runs do not turn the same way. Entry time
    // depends on how long someone spent in the menu, which is enough.
    randomSeed(millis());
    runMode   = RunMode::IdleSolve;
    s_opTitle = "Idle";
    idleStep    = 0;
    idleMoves   = 0;
    idleLast[0] = '\0';
    // The hint is this mode's own, not the stock abort line: SELECT does
    // something quite different from going back here, and the hint bar is
    // the one place that gets said.
    showOp(Op::Solve, "Idle", "Clamping the cube",
           "wheel sets the gap - SELECT solves");
    state = AppState::ModeClamp;
}

// Scramble, solve, repeat, unattended — the show-off loop. Same entry guard
// as the other modes, for the same reason.
//
// The unattended posture is decided by what this mode does NOT add: a fault
// anywhere in the loop lands on the shared red screen and waits for a human —
// no retry, because an unattended machine that retries a jam grinds itself —
// and nothing writes EEPROM per cycle, so a power cut mid-demo interrupts no
// write and the next boot's servo begin() sweeps release the cube.
static void actModeDemo() {
    if (!Cube.virtualCube.isReady()) {
        const char* rows[] = {
            "The cube state is not known.",
            "Run Load & Scan Cube first.",
        };
        showInfo("Demo Mode", rows, 2, "No cube state");
        return;
    }
    Cube.clearAbort();
    // Seeded from the clock so two runs do not scramble the same way. Entry
    // time depends on how long someone spent in the menu, which is enough.
    randomSeed(millis());
    runMode   = RunMode::Demo;
    s_opTitle = "Demo";
    demoRuns  = 1;
    // Red from the start, as Scramble Solve: the first thing a run does is
    // scramble. The hint names the graceful exit instead of the abort chord —
    // ending the demo is the expected gesture here, and the chord still works.
    showOp(Op::Error, "Demo", "Clamping the cube", "SELECT or LEFT ends the demo");
    state = AppState::ModeClamp;
}

// One solution move per SELECT press — the solve at showing-someone pace.
// Scrambles first only when the cube needs it; ModeClamp makes that call,
// because only there is the cube clamped and the model worth asking.
static void actModeStep() {
    if (!Cube.virtualCube.isReady()) {
        const char* rows[] = {
            "The cube state is not known.",
            "Run Load & Scan Cube first.",
        };
        showInfo("Step Solve", rows, 2, "No cube state");
        return;
    }
    Cube.clearAbort();
    // Seeded from the clock so two runs do not scramble the same way. Entry
    // time depends on how long someone spent in the menu, which is enough.
    randomSeed(millis());
    runMode   = RunMode::Step;
    s_opTitle = "Step Solve";
    // Red for the scramble that usually comes first; when the cube turns out
    // to be scrambled already, the compute handover recolors it.
    showOp(Op::Error, "Step Solve", "Clamping the cube", "SELECT+LEFT to abort");
    state = AppState::ModeClamp;
}

// Fold the cube into the selected pattern. One action serves all four items:
// the row picked tells it which, so a fifth pattern is a table row, not a
// function.
//
// The solved-cube gate is an honest refusal, not a hidden solve. The menu's
// preview net is a promise, and folding a scrambled cube produces
// not-the-preview; quietly solving first would run an operation the user
// never asked for. Requiring Solve keeps the promise and keeps the machine
// predictable.
static void actPattern() {
    // Read the selection FIRST, before any screen changes: the fold states
    // run long after the menu has moved on, and selectedIndex() only means
    // this row while the menu still shows it.
    patIdx = Menu.selectedIndex();
    const MenuItem* it = Menu.selectedItem();
    patName = (it && it->label) ? it->label : "Pattern";

    if (!Cube.virtualCube.isReady()) {
        const char* rows[] = {
            "The cube state is not known.",
            "Run Load & Scan Cube first.",
        };
        showInfo("Patterns", rows, 2, "No cube state");
        return;
    }
    if (!cubeIsSolved()) {
        const char* rows[] = {
            "Patterns start from a solved cube.",
            "Run Solve first.",
        };
        showInfo("Patterns", rows, 2, "Cube not solved");
        return;
    }
    Cube.clearAbort();
    runMode   = RunMode::Pattern;
    s_opTitle = "Patterns";
    // Green from the start: a fold is the machine building something, and
    // unlike the scramble-first modes there is no red disordering half here.
    showOp(Op::Solve, "Patterns", "Clamping the cube", "SELECT+LEFT to abort");
    cubeDisplay.setStatus(patName);
    Cube.displayUpdate();
    state = AppState::ModeClamp;
}

static void actCalMotors() {
    Cube.clearAbort();
    showOp(Op::Calibrate, "Motor Calibration", "Finding home positions",
           "Do not touch the machine");
    state = AppState::CalMotors;
}

// Color calibration cannot be started blind.
//
// calibrateColorSensors() does not identify what it is looking at — it assumes
// the cube is loaded a particular way and files whatever the sensors return
// under the color it expects. Wrong orientation means a wrong calibration
// written to EEPROM with nothing to catch it, which then misreads every scan
// afterwards. So the machine shows the required orientation and waits.
static void actCalColors() {
    Cube.clearAbort();
    cubeDisplay.showOperation(Op::Calibrate, "Color Calibration", nullptr,
                              "SELECT to start, LEFT to cancel");
    cubeDisplay.setOpCubeNet(CubeSystem::kCalStartFacelets);
    cubeDisplay.setStatus(CubeSystem::kCalStartText);
    Cube.displayUpdate();
    state = AppState::CalColorsPrompt;
}

static void actCalStatus() {
    // The board rows share boardHealthRow() with the Color Sensors page, so
    // the two screens cannot disagree about what "healthy" means — the x1000
    // separation convention is explained there.
    char b1[32], b2[32];
    boardHealthRow(colorSensor1, b1, sizeof(b1));
    boardHealthRow(colorSensor2, b2, sizeof(b2));

    char rowMotors[48], rowColor[48], rowB1[48], rowB2[48];
    snprintf(rowMotors, sizeof(rowMotors), "Motors\t%s",
             Cube.getMotorCalibration() ? "CALIBRATED" : "NOT CALIBRATED");
    snprintf(rowColor, sizeof(rowColor), "Color\t%s",
             Cube.getColorCalibration() ? "CALIBRATED" : "NOT CALIBRATED");
    snprintf(rowB1, sizeof(rowB1), "Board 1\t%s", b1);
    snprintf(rowB2, sizeof(rowB2), "Board 2\t%s", b2);

    const char* rows[] = {
        rowMotors, rowColor, rowB1, rowB2,
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

// "12.4 s" from milliseconds, or "-" when there is nothing to show. Zero
// doubles as the never-recorded marker — no real solve finishes inside a
// millisecond — so a fresh machine shows dashes instead of inventing "0.0 s".
static void fmtSolveTime(uint32_t ms, char* out, size_t n) {
    if (ms == 0) { snprintf(out, n, "-"); return; }
    snprintf(out, n, "%lu.%01lu s",
             (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
}

// The lifetime solve records, from the statsVals the hooks maintain. Screen
// shape rehearsed in Test_Menu's Stats demo — change both. Six rows is
// exactly enough; resist a seventh. No reset gesture yet, deliberately:
// whether wiping a machine's history deserves a menu item at all is a design
// question, not a missing feature.
static void actStats() {
    static char rows[6][40];
    char t[16];

    const uint32_t solves  = statsVals[CubeStats::Solves];
    const uint32_t stepped = statsVals[CubeStats::UntimedSolves];
    if (stepped > 0) {
        // Step solves are counted but untimed, so they get their own tally
        // rather than silently inflating the count the average divides by.
        snprintf(rows[0], sizeof(rows[0]), "Solves\t%lu (+%lu step)",
                 (unsigned long)solves, (unsigned long)stepped);
    } else {
        snprintf(rows[0], sizeof(rows[0]), "Solves\t%lu", (unsigned long)solves);
    }

    fmtSolveTime(statsVals[CubeStats::BestMs], t, sizeof(t));
    snprintf(rows[1], sizeof(rows[1]), "Best\t%s", t);

    // Division guarded: a fresh block has zero solves. The zero it passes on
    // comes back from fmtSolveTime as the same "-" the other empty rows show.
    fmtSolveTime(solves ? statsVals[CubeStats::TotalMs] / solves : 0,
                 t, sizeof(t));
    snprintf(rows[2], sizeof(rows[2]), "Average\t%s", t);

    fmtSolveTime(statsVals[CubeStats::LastMs], t, sizeof(t));
    snprintf(rows[3], sizeof(rows[3]), "Last\t%s", t);

    // Live, not the stored field: EEPROM's RunSec is only as fresh as the
    // last commit. Folding the uptime in (rather than saving it) keeps the
    // screen current without spending an EEPROM write on looking at it.
    const uint32_t rs = statsRunSecNow();
    if (rs >= 3600) {
        snprintf(rows[4], sizeof(rows[4]), "Run time\t%luh %02lum",
                 (unsigned long)(rs / 3600), (unsigned long)((rs % 3600) / 60));
    } else {
        snprintf(rows[4], sizeof(rows[4]), "Run time\t%lum",
                 (unsigned long)(rs / 60));
    }

    snprintf(rows[5], sizeof(rows[5]), "Faults\t%lu",
             (unsigned long)statsVals[CubeStats::Faults]);

    const char* lines[6] = { rows[0], rows[1], rows[2],
                             rows[3], rows[4], rows[5] };

    // Hand-rolled rather than showInfo(): the status sub-line has to be set
    // BEFORE setOpLines(), which reads it to decide where the rows start, and
    // showInfo() has no slot for one.
    cubeDisplay.showOperation(Op::Info, "Stats", nullptr,
                              "SELECT or LEFT to go back");
    cubeDisplay.setStatus("Since first use");
    cubeDisplay.setOpLines(lines, 6);
    Cube.displayUpdate();
    state = AppState::Info;
}

// The stored virtual cube, unfolded. This is the screen that answers "does the
// machine think it is holding the cube I am holding", which until now could only
// be checked by reading a 54-character dump over Serial.
static void actCubeState() {
    char net[CubeDisplay::kNetFacelets];
    const char* fac = nullptr;
    const char* what = nullptr;

    if (Cube.virtualCube.isReady()) {
        // executeMove() mutates cubeArray only, so the color array this
        // screen reads goes stale after any mode move — an idle turn, a
        // scramble, a fold. Despite the UNFINISHED label on its header,
        // rebuildFromCubeArray() is the working "refresh colorCubeArray"
        // call, and it is all this screen needs.
        Cube.virtualCube.rebuildFromCubeArray();
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

    // Nine of each color is the cheapest check that the stored state is a
    // cube at all, and the one an operator can act on: a count that is not nine
    // says which color was misread, which is more use than "invalid".
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

// Input for the jog page.
//
// Copied from Test_Menu verbatim — change both. Separate from pollEvent()
// because it needs the wheel and the UP/DOWN buttons to mean DIFFERENT things,
// and pollEvent() deliberately collapses them into one event — which is right
// for a menu and wrong here. Only one of the two runs per pass, so they can
// share the edge-detection state up with the other globals.
struct JogInput {
    int  turn;      // wheel detents, signed
    bool up;
    bool down;
    bool select;
    bool back;
};

static JogInput pollJog() {
    JogInput in = { 0, false, false, false, false };
    if (!Cube.encoderInitialized) return in;

    const uint32_t now = millis();
    if (now - lastPoll < 25) return in;
    lastPoll = now;

    const uint8_t b = menuEncoder.readButtons();
    const bool upDown   = b & RotaryEncoder::BTN_UP;
    const bool downDown = b & RotaryEncoder::BTN_DOWN;
    const bool leftDown = b & RotaryEncoder::BTN_LEFT;
    const bool selDown  = b & RotaryEncoder::BTN_SELECT;

    in.up     = upDown   && !prevUp;
    in.down   = downDown && !prevDown;
    in.select = selDown  && !prevSelect && !leftDown;   // never the abort chord
    in.back   = leftDown && !prevLeft   && !selDown;

    prevUp = upDown; prevDown = downDown; prevLeft = leftDown; prevSelect = selDown;

    const int32_t pos = menuEncoder.getPosition();
    in.turn = (int)(pos - prevPos);
    prevPos = pos;
    return in;
}

// The jog page's per-pass handler — the body Test_Menu runs inline in loop().
// A function here because the firmware's loop() dispatches it BEFORE
// pollEvent() and returns, and folding twelve rows of interaction into that
// one-line guard would bury the guard.
static void jogLoop() {
    const JogInput in = pollJog();
    const int step = (in.turn > 0) ? 1 : (in.turn < 0) ? -1 : 0;

    if (jogArmed) {
        if (in.back) {
            jogArmed = false;
            drawJog(nullptr);
            return;
        }

        const int d = step ? step : (in.up ? 1 : in.down ? -1 : 0);

        if (jogSel < kJogGrips) {
            // A gripper is a POSITION: the wheel picks one, SELECT sends
            // it, and nothing moves until you say so.
            if (in.select) {
                jogSend();
            } else if (d) {
                int t = jogTarget + d;
                if (t < 0) t = 0;           // clamp: a position has ends
                if (t > 2) t = 2;
                jogTarget = (int8_t)t;
                drawJog(nullptr);
            }
        } else {
            // A motor is not a position, it is a thing you turn. Once you
            // have taken the wheel, every detent IS a turn — which is what
            // a jog wheel should feel like, and it means watching a motor
            // through several turns costs no button presses at all.
            if (d)             jogTurn(d);
            else if (in.select) jogTurn(+1);
        }
        return;
    }

    if (in.back) {
        toMenu();
    } else if (step) {
        int sel = jogSel + step;
        if (sel < 0)          sel = kJogCount - 1;   // wrap, as the menu does
        if (sel >= kJogCount) sel = 0;
        jogSel = (int8_t)sel;
        drawJog(nullptr);
    } else if (in.select) {
        if (jogSel < kJogGrips) {
            // Start from where it is, so the first turn of the wheel moves
            // off the current position rather than re-proposing it.
            jogArmed  = true;
            jogTarget = (gripAt[jogSel] < 0) ? 0 : gripAt[jogSel];
            drawJog(nullptr);
        } else if (jogSel == kJogCube) {
            jogCube();
        } else {
            // Take the wheel for this motor.
            jogArmed = true;
            drawJog(nullptr);
        }
    }
}

// The value editor's per-pass handler — the body Test_Menu runs inline in its
// loop(). It needs the same split pollJog() gives the jog page: the wheel
// changes a number while the buttons stay buttons, which pollEvent() cannot
// give it. A function for the same reason jogLoop() is one — loop()
// dispatches it before pollEvent() and returns.
static void paramsLoop() {
    const JogInput in = pollJog();
    const int step = (in.turn > 0) ? 1 : (in.turn < 0) ? -1 : 0;
    const TuneParam& p = kTune[parSec->first + parSel];

    // The confirm owns the input while it is up. Nothing else is reachable
    // from here, so a gated row cannot be edited by any path that skips it.
    if (parGate) {
        if (in.select) {
            parGate = false;
            parWas  = p.get();
            parEdit = true;
            drawTune();
        } else if (in.back) {
            parGate = false;
            drawTune();
        }
        return;
    }

    if (parEdit) {
        if (in.select) {                    // keep it
            parEdit = false;
            drawTune();
        } else if (in.back) {               // put it back
            p.set(parWas);
            if (p.preview) p.preview(parWas);   // and move the part back too
            parEdit = false;
            drawTune();
        } else {
            const int d = step ? step : (in.up ? 1 : in.down ? -1 : 0);
            if (d) {
                // One detent is one step, however fast the wheel is spun.
                // A live row writes the servo on every change, and honouring
                // a burst of detents at once would turn a nudge into a jump
                // the horn takes in a single instant.
                int32_t v = p.get() + (int32_t)d * p.step;
                if (v < p.lo) v = p.lo;     // clamp: a range has ends
                if (v > p.hi) v = p.hi;
                p.set(v);
                // The edit path is the ONE place a preview runs: the operator
                // is watching, and the gate has already been shown. Boot and
                // reset call set() alone.
                if (p.preview) p.preview(v);
                if (p.flags & TP_LIVE) parMoved = true;
                drawTune();
            }
        }
        return;
    }

    if (in.back) {
        parLeave();
    } else if (step) {
        int sel = parSel + step;
        if (sel < 0)                 sel = parSec->count - 1;   // wrap
        if (sel >= (int)parSec->count) sel = 0;
        parSel = (int8_t)sel;
        drawTune();
    } else if (in.select) {
        // A toggle has no range to scroll through, so edit mode would be a
        // press to enter, a press to flip and a press to leave. Flip it.
        if (p.flags & TP_BOOL) {
            p.set(p.get() ? 0 : 1);
            drawTune();
        } else if (p.flags & TP_GATE) {
            parGate = true;
            drawTuneGate();
        } else {
            parWas  = p.get();          // what LEFT restores
            parEdit = true;
            drawTune();
        }
    }
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
    if (!Cube.colorSensorsOk)     addLine("Color sensor board offline");
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

    // Push stored tuning (or the compiled defaults) into every value's owner.
    // AFTER Cube.begin() — the owners must exist and have finished their own
    // begin() before values land in them; Test_Menu's begin-then-load order is
    // the proven one — and BEFORE the eject below, which must lift the cube to
    // the TUNED height, not whatever the compiled default happens to be.
    tuneLoadAll();

    // Pull the stored stats into their RAM copy, and mark where this boot's
    // run-time counting starts. A block that is missing, corrupt or from a
    // reordered layout reads as zeros — statsVals is zero-initialized, so
    // the invalid case needs no branch. Deliberately NO save here: the block
    // stamps itself on the first completed solve or fault, so a boot costs no
    // EEPROM wear and a power cut before the first commit loses only the
    // minutes of run time since the last one. Stats are advisory; a write per
    // boot to protect them would be wear spent on nothing.
    if (cubeStats.isValid()) {
        for (uint8_t f = 0; f < CubeStats::FieldCount; ++f) {
            statsVals[f] = cubeStats.get(f);
        }
    }
    // The seconds already on the clock when the block loads still count as
    // run time; starting the fold at zero is what claims them.
    runLastMs = 0;

    // Present the cube for removal.
    //
    // begin() leaves both servos retracted, which parks a cube already in the
    // machine right down inside the color-sensor box where it cannot be got at
    // by hand. Lifting the bottom servo is what makes it grabbable, so it is
    // part of coming up, not an optional convenience.
    //
    // This used to say botServoPartial() "for now": the calibrated eject
    // endpoint it promised exists — kTune's bottom-servo Eject row — and its
    // default equals the old partial position, so an untuned machine comes up
    // exactly as it always did.
    Cube.botServoEject();

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

    // The jog page owns the input while it is up: it needs the wheel and the
    // buttons separated, which pollEvent() cannot give it. Dispatched here
    // with a return, never folded into the switch below — the two pollers
    // share the edge-detection state, so exactly one may run per pass.
    if (state == AppState::Jog) {
        jogLoop();
        return;
    }

    // The value editor owns the input for the same reason — the wheel changes
    // a NUMBER there, not a cursor — and it is under the same
    // one-poller-per-pass rule.
    if (state == AppState::Params) {
        paramsLoop();
        return;
    }

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
            // TODO: a scan review screen belongs here — the per-sticker color,
            // runner-up and confidence are all recorded in Cube.scanColor/
            // scanAlt/scanConf, which is exactly what is needed to show WHICH
            // sticker was ambiguous instead of just a code.
            fail("Scan failed", scanErrorText(e), e, CubeFaultLog::Scan);
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
            fail("Solve failed", solveErrorText(e), e, CubeFaultLog::Solve);
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
            fail("Aborted", "Cube released", CubeSystem::ERR_ABORTED,
                 CubeFaultLog::Solve);
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
            fail("Solve stopped", execErrorText(e), e, CubeFaultLog::Solve);
        } else {
            state = AppState::Unloading;
        }
        break;
    }

    case AppState::Unloading: {
        Cube.unloadCube();

        // Every entry into this state is a completed solve — Executing,
        // ModeExecuting and StepReady all finish here with solveMillis final
        // (Demo excepted: it never unloads between runs and records at its
        // own transition). Step Solve counts as an UNTIMED solve: it is
        // human-paced, so its wall time would poison Best and Average with
        // however long the operator stood thinking.
        if (runMode == RunMode::Step) {
            statsVals[CubeStats::UntimedSolves]++;
            statsCommit();
        } else {
            statsRecordTimedSolve(solveMillis);
        }

        char sub[64];
        snprintf(sub, sizeof(sub), "%d moves in %lu.%02lu s",
                 Cube.solutionLength,
                 (unsigned long)(solveMillis / 1000),
                 (unsigned long)((solveMillis % 1000) / 10));
        // s_opTitle, not "Solve": the modes share this completion state, and
        // the Done screen should name the operation that actually ran.
        showOp(Op::Done, s_opTitle, "Solved!", sub);
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
        Cube.botServoEject();   // at the tuned height, not the mid-scan partial
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
        showOp(Op::Done, "Eject", "Cube ejected",
               "Take the cube out, then press SELECT");
        state = AppState::Done;
        break;

    case AppState::CalMotors: {
        int e = Cube.calibrateMotorRotations();
        if (e) fail("Motor calibration failed", calibErrorText(e), e,
                    CubeFaultLog::Cal);
        else   { showOp(Op::Done, "Motor Calibration", "Motors calibrated", "Press SELECT");
                 state = AppState::Done; }
        break;
    }

    case AppState::CalColorsPrompt:
        if (ev == MenuEvent::Select) {
            showOp(Op::Calibrate, "Color Calibration", "Learning the six colors",
                   "SELECT+LEFT to abort");
            state = AppState::CalColors;
        } else if (ev == MenuEvent::Back) {
            toMenu();
        }
        break;

    case AppState::CalColors: {
        int e = Cube.calibrateColorSensors();
        if (e) fail("Color calibration failed", calibErrorText(e), e,
                    CubeFaultLog::Cal);
        else   { showOp(Op::Done, "Color Calibration", "Colors calibrated", "Press SELECT");
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

    case AppState::Jog:
    case AppState::Params:
        break;      // handled before pollEvent() ever ran; nothing here
                    // consumes a MenuEvent

    case AppState::ResetConfirm:
        // The answer to actResetTune()'s red confirm — see the comment there
        // for why the servos are not driven to the restored defaults.
        if (ev == MenuEvent::Select) {
            tuneResetAll();
            showOp(Op::Done, "Reset Defaults", "Defaults restored");
            state = AppState::Done;
        } else if (ev == MenuEvent::Back) {
            toMenu();
        }
        break;

    // ---- Sensor Test -------------------------------------------------------

    case AppState::Sensors: {
        // Nothing in this state blocks between scans, so nothing else runs
        // pumpTick() — and without this the abort check below could never
        // fire: the chord's hold timer resets whenever the pump goes quiet
        // for 200 ms, and the gaps between scans are longer than that. The
        // scans themselves pump from inside their integration wait.
        pumpOnce();
        if (Cube.abortPending()) {
            // The chord means stop everywhere, so honour it here too — but as
            // an exit, not a safeStop: nothing is moving and nothing holds
            // the cube. clearAbort() now, because no error screen follows to
            // do it on acknowledge.
            sensorsLeave();
            Cube.clearAbort();
            toMenu();
            break;
        }

        if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
            // Wrapped, as the menu wraps: 18 stops is too many to bump along
            // an end stop.
            senSel = (int8_t)((senSel + (ev == MenuEvent::Down ? 1 : 17)) % 18);
            drawSensors();
        } else if (ev == MenuEvent::Select) {
            // Drill into the one selected.
            diagLastTick = 0;       // scan the chosen sensor on the next pass
            state = AppState::SensorRaw;
            drawSensorRaw();
        } else if (ev == MenuEvent::Back) {
            sensorsLeave();
            toMenu();
        } else if (millis() - diagLastTick >= 250) {
            // The gap between scans, not the scan rate — each scan blocks
            // ~0.9 s of pumped integration on its own, and back-to-back scans
            // would leave almost no fast passes for pollEvent() to run in.
            diagLastTick = millis();
            sensorsScanNext();
        }
        break;
    }

    case AppState::SensorRaw: {
        // Pumped for the same reason as the Sensors case above.
        pumpOnce();
        if (Cube.abortPending()) {
            sensorsLeave();
            Cube.clearAbort();
            toMenu();
            break;
        }

        if (ev == MenuEvent::Back) {
            // Back out to the board list, not the menu — the drill-down
            // mirrors the bench sketch's two-level shape.
            diagLastTick = 0;
            state = AppState::Sensors;
            drawSensors();
        } else if (ev == MenuEvent::Select) {
            sensorsLeave();
            toMenu();
        } else if (millis() - diagLastTick >= 350) {
            // One sensor, re-read a touch slower than the list page walks its
            // eighteen — the four numbers should be readable between updates.
            diagLastTick = millis();
            sensorRawTick();
        }
        break;
    }

    case AppState::Motors:
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            toMenu();
        } else if (millis() - diagLastTick >= 100) {
            // Seven reads per tick is cheap — a couple of I2C transactions
            // each — but unthrottled they would saturate the encoder mux bus
            // for a screen no faster than the eye can read anyway.
            diagLastTick = millis();
            motorsTick();
        }
        break;

    case AppState::InputReport:
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            toMenu();
        } else if (millis() - diagLastTick >= 50) {
            // ~20 Hz. The report reads the seesaw over I2C on every tick, and
            // an unthrottled loop() would hammer the same bus pollEvent() is
            // trying to use.
            diagLastTick = millis();
            inputReportTick();
        }
        break;

    // ---- Fault Log ---------------------------------------------------------

    case AppState::FaultLog:
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            toMenu();
        } else if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
            // Clamped, not wrapped: a log has a top and a bottom, and
            // wrapping from the oldest entry back to the newest would misread
            // as more history than there is.
            int span = (int)cubeFaultLog.count() - CubeDisplay::kOpLines;
            if (span < 0) span = 0;
            int top = faultTop + (ev == MenuEvent::Down ? 1 : -1);
            if (top < 0)    top = 0;
            if (top > span) top = span;
            if (top != faultTop) {
                faultTop = (int8_t)top;
                drawFaultLog();
            }
        }
        break;

    // ---- Modes ------------------------------------------------------------

    case AppState::ModeClamp: {
        // The Loading state's clamp, same order for the same mechanical
        // reason: bottom, then ring, then top.
        Cube.botServoExtend();
        Cube.ringExtend();
        Cube.topServoExtend();

        // If an abort landed during the clamp, release rather than starting
        // the mode. safeStop() suspends the latch internally so the unload is
        // a real unload, not a pumped-out no-op.
        if (Cube.abortPending()) {
            Cube.safeStop(CubeSystem::ERR_ABORTED);
            fail("Aborted", "Cube released", CubeSystem::ERR_ABORTED,
                 CubeFaultLog::Mode);
            break;
        }

        switch (runMode) {
        case RunMode::ScrambleSolve:
        case RunMode::Demo:            // Demo's first cycle is the same handover
            makeScramble();
            s_runList  = scramblePtrs;
            s_runCount = CubeSystem::kScrambleLen;
            s_runAt    = 0;
            cubeDisplay.setMessage("Scrambling");
            state = AppState::ModeScrambling;
            break;
        case RunMode::Step:
            if (cubeIsSolved()) {
                makeScramble();
                s_runList  = scramblePtrs;
                s_runCount = CubeSystem::kScrambleLen;
                s_runAt    = 0;
                cubeDisplay.setMessage("Scrambling");
                state = AppState::ModeScrambling;
            } else {
                // Already disordered — by Idle Mode, or by an abandoned run —
                // and scrambling an already scrambled cube would be a lie
                // about what the machine does, and thirty moves of one. The
                // check is the model, not a flag every mode would have to
                // remember to set. Yellow: thinking, Step's phase color.
                toComputing(Op::Info);
            }
            break;
        case RunMode::Pattern:
            // The scramble runner is phase-agnostic: a fold is a fixed move
            // list where the scramble is a random one, and the same state
            // runs both. The list comes from the shared tables actPattern()
            // indexed at selection time.
            s_runList  = CubeSystem::kPatternMoves[patIdx];
            s_runCount = CubeSystem::kPatternMoveCounts[patIdx];
            s_runAt    = 0;
            cubeDisplay.setMessage("Folding");
            state = AppState::ModeScrambling;
            break;
        case RunMode::IdleSolve:
            // Arm the first turn a full gap out, then paint the idle screen
            // (dial, count, decorative frame) over the clamp message.
            idleNextAt = millis() + (uint32_t)idleGapS * 1000UL;
            drawIdle();
            state = AppState::Idle;
            break;
        default:
            // Nothing sets ModeClamp without setting runMode first — but a
            // clamped cube with no job wants releasing, not guessing.
            Cube.unloadCube();
            toMenu();
            break;
        }
        break;
    }

    case AppState::ModeScrambling: {
        // Demo's SELECT/LEFT dismissal, before this pass's move dispatches —
        // after it, the press would cost one extra move.
        if (demoEndRequested(ev)) break;

        // Paint before the move, and pieces only: showOperation() here would
        // rebuild the screen and reprint the title to Serial once per move —
        // the flood the bench demos already ran into.
        char sub[48];
        if (runMode == RunMode::Demo) {
            // The run counter rides the status line — it is the one number
            // that says the loop is looping, and it displaces the move name.
            snprintf(sub, sizeof(sub), "Run %d   -   Move %d of %d",
                     demoRuns, s_runAt + 1, s_runCount);
        } else {
            snprintf(sub, sizeof(sub), "Move %d of %d   %s",
                     s_runAt + 1, s_runCount, s_runList[s_runAt]);
        }
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(s_runList, s_runCount, s_runAt);
        cubeDisplay.setOpProgress(s_runAt, s_runCount);

        // Virtual tracking ON: the modes are only reachable post-scan, so the
        // model is ready and must follow every turn, or the solve that comes
        // next would solve a state the cube is no longer in. Aligned, so a
        // jam is caught on the move that caused it.
        int e = Cube.executeMove(s_runList[s_runAt], true, true);
        if (e != 0) {
            // The runner serves two phases; name the one that failed.
            modeMoveFailed(runMode == RunMode::Pattern ? "Pattern stopped"
                                                       : "Scramble stopped", e);
            break;
        }

        if (++s_runAt >= s_runCount) {
            // The handover — the recolor-and-clear lives in toComputing().
            switch (runMode) {
            case RunMode::Step:
                // Yellow, not green: Step stops for a human right after the
                // computation, and green is the color of a machine solving.
                toComputing(Op::Info);
                break;
            case RunMode::Pattern:
                // The fold IS the whole operation — nothing to compute and
                // nothing to run, so this is a completion, not a handover.
                // Release, then LIFT: unloadCube() alone parks the cube down
                // in the color-sensor box where it cannot be got at by hand
                // (see setup()), which would make the hint's "take it out" a
                // lie. The eject lift is what makes it true — and "Solve to
                // undo" still works, because Loading's botServoExtend()
                // sweeps up from the eject height exactly as it does for a
                // freshly inserted cube.
                Cube.unloadCube();
                Cube.botServoEject();
                // executeMove() mutates cubeArray only; the color array the
                // net is drawn from is stale until this refresh. Despite the
                // UNFINISHED label on its header, rebuildFromCubeArray() is
                // the working "refresh colorCubeArray" call.
                Cube.virtualCube.rebuildFromCubeArray();
                // The MODEL's net, not the stored preview: the screen shows
                // what the machine believes it built, and a mismatch against
                // the menu's preview IS the diagnostic. No headline — the
                // net owns the middle of the screen, the way Cube State and
                // the calibration prompt draw it — so the pattern's name
                // rides the line beneath the net instead.
                showOp(Op::Done, "Patterns", nullptr,
                       "Take it out, or Solve to undo");
                cubeDisplay.setOpCubeNet(Cube.virtualCube.getColorArray());
                cubeDisplay.setStatus(patName);
                Cube.displayUpdate();
                state = AppState::Done;
                break;
            default:
                // Scramble Solve and Demo roll straight into the solve.
                toComputing(Op::Solve);
                break;
            }
        }
        break;
    }

    case AppState::ModeComputing: {
        // The screen already says "Computing" — painted at the transition in,
        // because solveVirtual() blocks unpumped for up to ~10 s and a stale
        // "Scrambling" headline for that long reads as a hang. The abort
        // chord goes unnoticed for the same stretch; the pre-painted message
        // is what makes that survivable.
        //
        // But painted is not FLUSHED: LVGL repaints only when its refresh
        // timer comes due, and one displayUpdate() right after a pumped move
        // almost never lands on that tick — so without this the panel would
        // freeze on the LAST scramble frame (or Idle's dial) for the whole
        // compute. One refresh period of pumping is what actually gets the
        // "Computing" screen onto the glass before the block starts.
        pumpDelay(40);
        int e = Cube.solveVirtual();
        if (e) {
            // Unlike the core Solving state, the cube is CLAMPED here — the
            // modes compute after loading, Solve computes before it. Release
            // the cube before the error screen starts waiting on a human.
            Cube.safeStop(e);
            fail("Solve failed", solveErrorText(e), e, CubeFaultLog::Mode);
            break;
        }

        for (int i = 0; i < Cube.solutionLength; ++i) {
            s_solveRibbon[i] = Cube.solveMoves[i].c_str();
        }
        execAt     = 0;
        solveStart = millis();
        if (runMode == RunMode::Step) {
            // Step waits for a human between moves. solveStart still runs so
            // the shared "Solved!" screen can report a wall time — but that
            // time is human-paced, and any solve records must not treat it
            // as the machine's.
            drawStepReady();
            state = AppState::StepReady;
        } else {
            cubeDisplay.setMessage("Solving");
            state = AppState::ModeExecuting;
        }
        break;
    }

    case AppState::ModeExecuting: {
        // Demo's SELECT/LEFT dismissal, before the move dispatches — same
        // argument as in ModeScrambling.
        if (demoEndRequested(ev)) break;

        // One solution move per pass. This mirrors CubeSystem::executeSolve()
        // move for move — the boundary abort check, the +100 code scheme, the
        // safeStop-then-fail order — and must stay in step with it. The modes
        // cannot call it directly: it has no ribbon, and it returns only when
        // the whole solution is done, which is one event poll per solve
        // instead of one per move.
        char sub[48];
        if (runMode == RunMode::Demo) {
            snprintf(sub, sizeof(sub), "Run %d   -   Move %d/%d",
                     demoRuns, execAt + 1, Cube.solutionLength);
        } else {
            snprintf(sub, sizeof(sub), "Move %d/%d   %s",
                     execAt + 1, Cube.solutionLength, s_solveRibbon[execAt]);
        }
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(s_solveRibbon, Cube.solutionLength, execAt);
        cubeDisplay.setOpProgress(execAt, Cube.solutionLength);

        // The move boundary is the only mechanically safe stop point.
        if (Cube.abortPending()) {
            const int code = 100 + CubeSystem::ERR_ABORTED;
            Cube.safeStop(code);
            fail("Solve stopped", execErrorText(code), code, CubeFaultLog::Mode);
            break;
        }

        int e = Cube.executeMove(Cube.solveMoves[execAt], true, true);
        if (e != 0) {
            // A failed move leaves physical and virtual out of step, so
            // safeStop() wipes both — a stale solution can never be replayed.
            Cube.safeStop(100 + e);
            fail("Solve stopped", execErrorText(100 + e), 100 + e,
                 CubeFaultLog::Mode);
            break;
        }

        if (++execAt >= Cube.solutionLength) {
            solveMillis = millis() - solveStart;
            if (runMode == RunMode::Demo) {
                // Each demo run is a real machine solve, and since Demo never
                // reaches Unloading this transition is its only recording
                // point — one commit per completed run, nothing per cycle
                // beyond that.
                statsRecordTimedSolve(solveMillis);

                // Demo's own "Solved!", not the shared Unloading screen: the
                // cube stays clamped between runs, because releasing and
                // re-gripping it back to back would cost two servo round
                // trips and buy nothing.
                char rest[48];
                snprintf(rest, sizeof(rest), "Run %d   -   %d moves in %lu.%02lu s",
                         demoRuns, Cube.solutionLength,
                         (unsigned long)(solveMillis / 1000),
                         (unsigned long)((solveMillis % 1000) / 10));
                showOp(Op::Done, "Demo", "Solved!", "SELECT or LEFT ends the demo");
                cubeDisplay.setStatus(rest);
                // Full ribbon, full bar: this screen is a result, and an
                // empty bar under "Solved!" would read as a run that never
                // happened.
                cubeDisplay.setOpRibbon(s_solveRibbon, Cube.solutionLength,
                                        Cube.solutionLength - 1);
                cubeDisplay.setOpProgress(Cube.solutionLength, Cube.solutionLength);
                Cube.displayUpdate();
                restUntil = millis() + 2500;
                state = AppState::DemoRest;
            } else {
                state = AppState::Unloading;
            }
        }
        break;
    }

    case AppState::Idle: {
        // Nothing in this state blocks, so nothing pumps: without this call
        // pumpTick() would never run between moves, and the abort check below
        // would be dead code — the chord held through a 60 s gap for nothing.
        // pumpOnce() carries its own 25 ms input throttle, so coexisting with
        // pollEvent() above costs one extra seesaw read per 25 ms, not one
        // per pass.
        pumpOnce();
        if (Cube.abortPending()) {
            Cube.safeStop(CubeSystem::ERR_ABORTED);
            fail("Idle stopped", "Cube released", CubeSystem::ERR_ABORTED,
                 CubeFaultLog::Mode);
            break;
        }

        if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
            // Clamped, not wrapped. Rolling from a one second gap round to a
            // minute because the wheel went one detent too far is the kind of
            // surprise a wrapping cursor is fine with and a duration is not.
            int g = idleGapS + (ev == MenuEvent::Down ? 1 : -1);
            if (g < kIdleGapMin) g = kIdleGapMin;
            if (g > kIdleGapMax) g = kIdleGapMax;
            if (g != idleGapS) {
                idleGapS = g;
                // Re-time the pending move from NOW, so shortening the gap
                // takes effect immediately instead of after the old one runs
                // out. Turning the wheel down to 1 s and then waiting 30 s
                // for the next move would read as the setting not working.
                idleNextAt = millis() + (uint32_t)idleGapS * 1000UL;
            }
            drawIdle();
        } else if (ev == MenuEvent::Select) {
            if (cubeIsSolved()) {
                // The identity-maneuver guard — see cubeIsSolved(). Handing
                // a solved cube to the solver would run 13 moves to change
                // nothing.
                Cube.unloadCube();
                showOp(Op::Done, "Idle", "Already solved");
                state = AppState::Done;
            } else {
                showOp(Op::Solve, "Idle", "Computing the solution",
                       "SELECT+LEFT to abort");
                // The rebuild above already cleared the dial — but say so,
                // because a dial left up beside the solve's progress bar
                // would be two different claims about how far along the
                // same operation is.
                cubeDisplay.setOpDial(0, 0, 0, nullptr, nullptr);
                state = AppState::ModeComputing;
            }
        } else if (ev == MenuEvent::Back) {
            // Graceful exit, back to the at-rest state a finished scan
            // leaves: cube in the bay, model still valid, menu still
            // post-scan. Every idle turn was tracked, so nothing is stale.
            Cube.unloadCube();
            toMenu();
        } else if ((int32_t)(millis() - idleNextAt) >= 0) {
            // Compared as a difference rather than millis() >= idleNextAt, so
            // the 49-day rollover is a non-event instead of a machine that
            // stops turning until someone reboots it.
            idleTurn();
        }
        break;
    }

    case AppState::DemoRest: {
        // A wait state holding the cube, so it pumps — same argument as the
        // Idle case above: nothing here blocks, so nothing else runs
        // pumpTick(), and without this the abort check would be dead code.
        pumpOnce();
        if (Cube.abortPending()) {
            Cube.safeStop(CubeSystem::ERR_ABORTED);
            fail("Demo stopped", "Cube released", CubeSystem::ERR_ABORTED,
                 CubeFaultLog::Mode);
            break;
        }
        if (demoEndRequested(ev)) break;

        if ((int32_t)(millis() - restUntil) >= 0) {
            // Next run. Straight back to ModeScrambling, no ModeClamp: the
            // cube never left the grip, and re-running the clamp would sweep
            // servos that are already there. Difference compare for the same
            // rollover reason as the idle timer.
            demoRuns++;
            makeScramble();
            showOp(Op::Error, "Demo", "Scrambling", "SELECT or LEFT ends the demo");
            s_runAt = 0;
            state = AppState::ModeScrambling;
        }
        break;
    }

    case AppState::StepReady: {
        // Pumped like DemoRest: the abort chord must be noticed while the
        // machine sits waiting for the next press.
        pumpOnce();
        if (Cube.abortPending()) {
            // A solution is mid-flight, so this follows executeSolve()'s +100
            // contract like ModeExecuting — not the raw wait-state abort the
            // Idle case uses, where no solution move is in play.
            const int code = 100 + CubeSystem::ERR_ABORTED;
            Cube.safeStop(code);
            fail("Solve stopped", execErrorText(code), code, CubeFaultLog::Mode);
            break;
        }

        if (ev == MenuEvent::Select) {
            int e = Cube.executeMove(Cube.solveMoves[execAt], true, true);
            if (e != 0) {
                // Same failure tail as ModeExecuting: physical and virtual
                // are out of step, so safeStop() wipes both.
                Cube.safeStop(100 + e);
                fail("Solve stopped", execErrorText(100 + e), 100 + e,
                     CubeFaultLog::Mode);
                break;
            }
            if (++execAt >= Cube.solutionLength) {
                solveMillis = millis() - solveStart;
                state = AppState::Unloading;
            } else {
                drawStepReady();
            }
        } else if (ev == MenuEvent::Back) {
            // Abandon. SELECT means "next move" here, not "done looking" —
            // only LEFT leaves, which is the one meaning it has everywhere.
            // Every executed move was tracked, so the model is in sync and
            // the post-scan menu can re-Solve. The half-consumed solution is
            // wiped: every solve entry recomputes anyway, but a stale one
            // left lying around is a replay waiting for the one path that
            // forgets to.
            Cube.clearSolution();
            Cube.unloadCube();
            toMenu();
        }
        break;
    }
    }
}
