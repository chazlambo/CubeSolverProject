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
//  Left out on purpose
//  -------------------
//  Every menu item has a real screen behind it, backed by the machine.
//  (Stats' reset gesture — SELECT+RIGHT held five seconds on the page — spares
//  nothing: solves, times, faults and the run clock all go. CubeStats::clear()
//  only drops the magic, so an accidental wipe is recoverable with a
//  programmer, which is what made an all-of-it reset acceptable.)
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
    SolveConfirm,// the solution, over the net of the cube it was computed for
                 // -> SELECT runs it, LEFT walks away having changed nothing
    Loading,     // clamp the cube, IF it is not already clamped
    Executing,   // executeSolve()
    Displaying,  // the plain solve's ending: ring and top released, the cube
                 // turning on the bottom gripper under its result, until
                 // SELECT clamps it again. The modes do NOT come through here
                 // — see the essay above drawSolveDisplay().
    Unloading,   // release the cube
    Ejecting,    // release the ring and top, lift the cube into reach
    EjectWait,   // the cube is up and grabbable; watch the color sensors for it
                 // going away, and put the gripper down when it does. SELECT
                 // still finishes by hand — see the essay above ejectWatch().
    // Motor calibration is a FLOW, not one blocking call — see the essay
    // above actCalMotors(). The sweep at the end derives every value from
    // wherever the motors happen to be standing, so a human squaring each
    // face first is what makes that reference true.
    CalMotorsPrompt, // "the cube must be OUT"           -> SELECT clamps
    CalMotorsClamp,  // close bottom -> ring -> top on the empty centre
    CalMotorsPick,   // the six motors + Save; wheel scrolls, SELECT on a motor
                     // row opens MotorDial (below, with Jog and Params).
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
    ParamsLeave,     // red confirm before walking away from tuning edits that
                     // have not been through the Apply row

    // Not modes: the Hardware Test jog page, the tuning value editor, the
    // motor dial and the Cube State viewer. Each owns the input while it is
    // up — loop() hands it to jogLoop(), paramsLoop(), calDialLoop() or
    // cubeStateLoop() and returns BEFORE pollEvent() runs, because all four
    // pages need the wheel and the UP/DOWN buttons to mean different things
    // and pollEvent() deliberately collapses them into one.
    //
    // MotorDial is one screen with two ways in — the Motor Calibration flow's
    // motor rows, and the Motor Sensors diagnostic's — so it is named for what
    // it is rather than for either page. What differs between the two is one
    // enum; see the essay above calEnterDial().
    //
    // CubeState is the odd one out: the wheel points at a face and UP/DOWN
    // turn it, and the thing turning is the MODEL — no motor moves and the
    // model is put back on the way out. Read the essay above csTurn() before
    // touching it; the choice it records is the whole page.
    Jog,
    Params,
    MotorDial,
    CubeState,

    // The Stats page. Info-shaped — a static table, SELECT or LEFT leaves —
    // but it owns its input like the four above, because its reset gesture is
    // a CHORD (SELECT+RIGHT held five seconds) and pollEvent() cannot see
    // one: it collapses RIGHT into Select and fires on the press edge, so the
    // page would exit the instant the chord began to form.
    Stats,

    // The Sensor Test pages. Live readouts: Sensors and SensorRaw are the
    // color-board pair — the two 3x3 grids, then one sensor's raw numbers —
    // Motors is the seven encoder angles, InputReport the wheel and buttons.
    //
    // All three use pollEvent(), not pollJog(): here the wheel moves a CURSOR,
    // which is exactly the meaning pollEvent() gives it. Motors can start
    // things that move — Home, and the dial one of its rows opens — but the
    // moment a button has to mean "move this motor" the page has handed over
    // to MotorDial above, which owns its input for that reason. Full quarter
    // turns live THERE too (the diagnostic owner's UP/DOWN — see
    // calDialLoop()), deliberately behind that SELECT: on a list you are
    // navigating, a button that turns a motor is an accident waiting for a
    // wrong row.
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

// Title for whichever completion screen a run ends on — Unloading's for the
// modes, the display spin's for the plain Solve. Scramble Solve and the other
// modes share Unloading's, and a "Solved!" screen titled "Solve" after a
// Scramble Solve would name the wrong operation.
static const char* s_opTitle = "Solve";

// The frame color every operation screen wears until the operator is back in
// the menu. A third of the same kind as runMode and s_opTitle above: one fact
// about the run, captured once at the moment it starts and read by every
// screen it draws afterwards.
//
// The frame band is wayfinding, not machine state: a branch of the menu keeps
// its color all the way through the screens it leads to, so the color says
// WHERE YOU ARE rather than what the motors are doing. Which means the color
// is a fact about the menu row that was selected, and that is exactly where
// loop() reads it from — CubeMenu::themeOf() on the row under the cursor, one
// assignment before Menu.handle(). Nothing per-action to remember, and nothing
// that can drift when a table is recolored.
//
// Blue is the boot value because the self-test and the startup screens belong
// to the main line of work, which is blue.
//
// Two deliberate exceptions: an Error screen is always red (enforced inside
// CubeDisplay::setOpTheme(), not here), and Idle Mode cycles all six colors
// to look alive — see kIdleCycle.
static MenuTheme s_opTheme = MenuTheme::Blue;

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

// Input for the jog page, built from the state above by pollJog(), further
// down. The type has to be declared this early because Arduino inserts its
// generated prototypes just below here, and one of them returns a JogInput —
// declared any later, the sketch does not compile. The simulator builds the
// .ino as plain C++, generates no prototypes, and so never sees this.
struct JogInput {
    int  turn;      // wheel detents, signed
    bool up;
    bool down;
    bool select;
    bool back;
};

// Which page opened the motor dial. Declared up here for the same reason
// JogInput is: it is a parameter type of calEnterDial(), and Arduino's
// generated prototypes go in just below.
//
// The dial itself is far down, with the Motor Calibration flow. It is one
// screen with two ways in — that flow's motor rows and the Motor Sensors
// diagnostic's — and this is the ONLY thing that differs between them; see the
// essay above calEnterDial() for what it does and does not change.
enum class DialOwner : uint8_t { CalFlow, Diagnostic };
static DialOwner dialOwner = DialOwner::CalFlow;

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
extern const MenuScreen kScreenPatternsMore;
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
static void actPatternMore();
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
//
//  Color is WAYFINDING
//  -------------------
//  A branch of this tree is one color from the row that opens it all the way
//  down, and out through the operation screens that branch leads to. That is
//  the whole point: the frame band is the one thing readable from across the
//  room, and what it should say is where you are, not what a servo is doing.
//
//      Solve / Load & Scan   BLUE     the main line of work
//      Modes                 RED      but see below
//      Eject Cube            GREEN
//      Settings              YELLOW   and every screen under it
//      Stats                 PURPLE
//
//  Modes is the one branch that fans out: the screen is red, but each mode
//  owns a color and keeps it through its own operation screens — Scramble
//  Solve red, Demo blue, Idle yellow, Patterns green, Step Solve purple. The
//  modes are five different things that happen to be filed together, and a
//  running mode is somewhere you can be for minutes.
//
//  Screens under Settings omit `theme` and inherit yellow — one branch, one
//  color, as the menu this is modelled on does it — so the branch cannot be
//  recolored a row at a time by accident.
//
//  Two exceptions, both documented where they live: an Error screen is red
//  whatever branch it happened in (CubeDisplay::setOpTheme() enforces it), and
//  Idle Mode cycles all six colors to look alive (kIdleCycle).

// Preview lists. These deliberately repeat their submenu's item labels rather
// than being generated from them: the pane is a teaser, it is written to fit,
// and a screen is free to show fewer entries there than it really has. The
// pane is about 54 px wide at 9 px type — roughly 13 characters — so these are
// abbreviated where the real item name would not fit.
static const char* const kPrevSettings[]    = { "Diagnostics", "Calibration", "Parameters",
                                                "About" };
static const char* const kPrevModes[]       = { "Scramble", "Idle Mode", "Demo Mode",
                                                "Step Solve", "Patterns" };
static const char* const kPrevPatternsMore[] = { "Cube^3", "Anaconda", "Python",
                                                 "Tetris", "Twister" };
static const char* const kPrevCalibration[] = { "Status", "Colors", "Motors", "Servos" };
static const char* const kPrevDiagnostics[] = { "Hardware", "Sensor Test", "Cube State",
                                                "Fault Log" };
static const char* const kPrevServoTune[]   = { "Top Servo", "Bottom Servo", "Ring" };
static const char* const kPrevSensors[]     = { "Color", "Motors", "Input" };
static const char* const kPrevParams[]      = { "Face Motors", "Alignment", "Color",
                                                "Reset" };

// ---- Main, before a cube is scanned ----
static const MenuItem kMainPreItems[] = {
    { "Load & Scan Cube", nullptr,          actLoadScan, "Read all six faces.",
      nullptr, 0, MenuTheme::Blue },
    { "Settings",         &kScreenSettings, nullptr,     "Setup and machine info.",
      kPrevSettings, 4, MenuTheme::Yellow },
    { "Stats",            nullptr,          actStats,    "View solve records.",
      nullptr, 0, MenuTheme::Purple },
};
static const MenuScreen kScreenMainPre = { "Cube Solver", kMainPreItems, 3, MenuTheme::Blue };

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
      nullptr, 0, MenuTheme::Green },
    { "Settings",   &kScreenSettings, nullptr,  "Setup and machine info.",
      kPrevSettings, 4, MenuTheme::Yellow },
    { "Stats",      nullptr,          actStats, "View solve records.",
      nullptr, 0, MenuTheme::Purple },
};
static const MenuScreen kScreenMainPost = { "Cube Ready", kMainPostItems, 5, MenuTheme::Blue };

// ---- Modes ----
//
// The one screen whose items each own a color rather than inheriting the
// screen's. A mode is somewhere you stay, so the color the row is wearing is
// the color its operation screens wear — loop() copies it into s_opTheme when
// the row is selected, and every screen the mode draws from then on carries
// it. Red stays the screen's own, and Scramble Solve's, because it is the mode
// this menu was named after.
static const MenuItem kModesItems[] = {
    { "Scramble Solve", nullptr, actModeScramble, "Scramble, then solve it.",
      nullptr, 0, MenuTheme::Red },
    { "Idle Mode",      nullptr, actModeIdle,     "Turn slowly while waiting.",
      nullptr, 0, MenuTheme::Yellow },
    { "Demo Mode",      nullptr, actModeDemo,     "Show off, unattended.",
      nullptr, 0, MenuTheme::Blue },
    { "Step Solve",     nullptr, actModeStep,     "One move at a time.",
      nullptr, 0, MenuTheme::Purple },
    { "Patterns",       &kScreenPatterns, nullptr, "Fold the cube into shapes.",
      nullptr, 0, MenuTheme::Green },
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
// under the right preview. Nine patterns is four more than a screen holds,
// hence the split: this screen owns table rows 0-3, More Patterns rows
// kPatMoreBase up — actPatternMore() adds the offset and everything after
// the index capture is shared.
//
// The per-item themes here are NOT wayfinding — they are different colors so
// adjacent rows of preview net do not all sit in the same frame. The branch
// color is the screen's green, and actPattern() sets that explicitly so a
// fold's operation screens do not inherit whichever row was picked. The More
// Patterns row is the one navigation row, and it stays the branch's green.
static const MenuItem kPatternItems[] = {
    { "Checkerboard", nullptr, actPattern, "U2 D2 R2 L2 F2 B2",
      nullptr, 0, MenuTheme::Blue,   CubeSystem::kPatternNets[0] },
    { "Cube in Cube", nullptr, actPattern, "Fifteen moves.",
      nullptr, 0, MenuTheme::Green,  CubeSystem::kPatternNets[1] },
    { "Six Spot",     nullptr, actPattern, "U D' R L' F B' U D'",
      nullptr, 0, MenuTheme::Yellow, CubeSystem::kPatternNets[2] },
    { "Superflip",    nullptr, actPattern, "Every edge flipped.",
      nullptr, 0, MenuTheme::Purple, CubeSystem::kPatternNets[3] },
    { "More Patterns", &kScreenPatternsMore, nullptr, "Five more shapes.",
      kPrevPatternsMore, 5 },
};
const MenuScreen kScreenPatterns = { "Patterns", kPatternItems, 5, MenuTheme::Green };

// Where More Patterns' rows start in the kPattern* tables: one screen's worth
// of patterns in, not five — the More Patterns row itself is not a pattern.
static const int kPatMoreBase = 4;

static const MenuItem kPatternMoreItems[] = {
    { "Cube^3",   nullptr, actPatternMore, "Cube in a cube in a cube.",
      nullptr, 0, MenuTheme::Blue,   CubeSystem::kPatternNets[4] },
    { "Anaconda", nullptr, actPatternMore, "A snake wound round the cube.",
      nullptr, 0, MenuTheme::Green,  CubeSystem::kPatternNets[5] },
    { "Python",   nullptr, actPatternMore, "The other snake.",
      nullptr, 0, MenuTheme::Yellow, CubeSystem::kPatternNets[6] },
    { "Tetris",   nullptr, actPatternMore, "L R F B U' D' L' R'",
      nullptr, 0, MenuTheme::Purple, CubeSystem::kPatternNets[7] },
    { "Twister",  nullptr, actPatternMore, "Two colors twist on every face.",
      nullptr, 0, MenuTheme::Red,    CubeSystem::kPatternNets[8] },
};
const MenuScreen kScreenPatternsMore = { "More Patterns", kPatternMoreItems, 5, MenuTheme::Green };

// ---- Settings ----
//
// Diagnostics first: looking at the machine comes before changing it, and the
// question that brings anyone here is usually "what is it doing" rather than
// "let me retune it". Parameters is a sibling of Calibration rather than a
// child of Diagnostics for the same reason — it CHANGES the machine, and
// filing a write under a menu named for reading was always a lie about it.
//
// No item names a theme. The whole branch is Settings' yellow, and inheriting
// it is what makes that true by construction rather than by four rows
// agreeing.
static const MenuItem kSettingsItems[] = {
    { "Diagnostics", &kScreenDiagnostics, nullptr,  "Test and inspect hardware.",
      kPrevDiagnostics, 4 },
    { "Calibration", &kScreenCalibration, nullptr,  "Tune motors and sensors.",
      kPrevCalibration, 4 },
    { "Parameters",  &kScreenParams,      nullptr,  "Tunable machine settings.",
      kPrevParams, 4 },
    { "About",       nullptr,             actAbout, "Firmware and build info." },
};
const MenuScreen kScreenSettings = { "Settings", kSettingsItems, 4, MenuTheme::Yellow };

// ---- Calibration ----
static const MenuItem kCalibrationItems[] = {
    { "Calibration Status", nullptr, actCalStatus, "What is calibrated so far." },
    { "Color Sensors",      nullptr, actCalColors, "Learn the six face colors." },
    { "Motor Positions",    nullptr, actCalMotors, "Find the motor home points." },
    { "Servo Positions",    &kScreenServoTune, nullptr, "Set the gripper travel.",
      kPrevServoTune, 3 },
};
const MenuScreen kScreenCalibration = { "Calibration", kCalibrationItems, 4, MenuTheme::Yellow };

// ---- Servo Positions ----
//
// The three tuning sections that move something you can watch, kept under
// Calibration and away from the numbers that only take effect on the next
// move — those live under Settings > Parameters. Each item opens the value
// editor on one section of the shared CubeTuneTable.
static const MenuItem kServoTuneItems[] = {
    { "Top Servo",    nullptr, actTopServo, "Grips from above." },
    { "Bottom Servo", nullptr, actBotServo, "Grips, centres and ejects." },
    { "Ring",         nullptr, actRingPos,  "Stepper, not a servo." },
};
const MenuScreen kScreenServoTune = { "Servo Positions", kServoTuneItems, 3, MenuTheme::Yellow };

// ---- Diagnostics ----
//
// Four items that answer "what is it doing" — Hardware Test drives things, but
// only where you point it, and nothing here survives the screen. Parameters is
// not among them: it WRITES tuning to EEPROM, which is a different question.
static const MenuItem kDiagnosticsItems[] = {
    { "Hardware Test", nullptr, actJog,            "Exercise every actuator." },
    { "Sensor Test",   &kScreenSensors, nullptr,   "Watch the sensors live.",
      kPrevSensors, 3 },
    { "Cube State",    nullptr, actCubeState,      "Show the stored cube." },
    { "Fault Log",     nullptr, actFaultLog,       "Recent faults and errors." },
};
const MenuScreen kScreenDiagnostics = { "Diagnostics", kDiagnosticsItems, 4, MenuTheme::Yellow };

// ---- Sensor Test ----
//
// A submenu rather than one screen because the two live boards and the input
// diagnostic answer different questions — and Diagnostics was at the five-item
// limit when this was split out, so a screen that wanted a sixth thing wanted
// splitting, not squeezing. Captions mirror Test_Menu's Diagnostics entries
// for the same three screens.
static const MenuItem kSensorsItems[] = {
    { "Color Sensors", nullptr, actSensorColors, "Live, per board." },
    { "Motor Sensors", nullptr, actSensorMotors, "Raw encoder angles." },
    { "Input Report",  nullptr, actInputReport,  "Live wheel and buttons." },
};
const MenuScreen kScreenSensors = { "Sensor Test", kSensorsItems, 3, MenuTheme::Yellow };

// ---- Parameters ----
//
// The value-only tuning sections. The Align Log toggle
// (CubeSystem::debugAlignLog) is a row of the Alignment section, not a menu
// item.
//
// Reset Defaults inherits yellow like its siblings. The warning it needs is on
// the confirm screen, which is an Op::Error and therefore red however this row
// is themed — and a single red row inside a yellow branch reads as "you are
// somewhere else", which is what a color change is for.
static const MenuItem kParamsItems[] = {
    { "Face Motors",    nullptr, actFaceMot,   "Speed and settling time." },
    { "Alignment",      nullptr, actAlignPar,  "How square is square enough." },
    { "Color",          nullptr, actColorPar,  "How a sticker is judged." },
    { "Reset Defaults", nullptr, actResetTune, "Throw away every change." },
};
const MenuScreen kScreenParams = { "Parameters", kParamsItems, 4, MenuTheme::Yellow };

// ---------------------------------------------------------------------------
//  Display helpers
// ---------------------------------------------------------------------------
using Op = CubeDisplay::OpKind;

// Every screen the machine shows while it is working goes through here, so all
// of them get the same frame, the same title position and the same hint bar as
// the menu — and, since the frame band became wayfinding, the color of the
// branch the operator walked down to get here.
//
// That second job is why nothing in this sketch calls showOperation() directly
// any more. One funnel, one setOpTheme(), and a screen added later cannot
// forget to wear its branch's color. `kind` still names what sort of screen
// this is; on an Error it is also what keeps the frame red, because
// setOpTheme() refuses to repaint an Error screen.
//
// No displayUpdate() here: the decorated screens set rows, chips, nets or a
// ribbon before they flush, and flushing twice would cost a full repaint.
static void opScreen(Op kind, const char* title, const char* headline,
                     const char* hint = nullptr) {
    cubeDisplay.showOperation(kind, title, headline, hint);
    cubeDisplay.setOpTheme(s_opTheme);
}

// opScreen() plus the flush, for the screens that are finished as drawn.
static void showOp(Op kind, const char* title, const char* headline,
                   const char* hint = nullptr) {
    opScreen(kind, title, headline, hint);
    Cube.displayUpdate();
}

// A read-only status screen. SELECT or LEFT returns to wherever the menu was.
//
// Rows are "Label\tValue"; the display right-aligns the value half, which is
// what makes these read as a table rather than as a wall of text. A row with no
// tab spans the full width.
static void showInfo(const char* title, const char* const* lines, int count,
                     const char* headline = nullptr) {
    opScreen(Op::Info, title, headline, "SELECT or LEFT to go back");
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

    // Every return to the menu re-sends the whole frame.
    //
    // The panel driver is differential and cannot see damage done to the glass
    // while a servo or stepper was running under an operation screen, so the
    // one cheap moment to guarantee the picture is the moment the machine
    // stops moving. ~125 ms, hidden behind the wheel-in. This is also what
    // scrubs the boot: setup() reaches here only after every boot actuator
    // has finished.
    //
    // History, because this spot has scar tissue. A forced repaint used to
    // live here and was removed after three attempts — tft->clear(),
    // tft->update(internal_fb) and lv_refr_now() — each took the boot down.
    // The update() one is now explained: handed its own internal buffer it
    // rotates that buffer onto itself and scrambles it (see the comment in
    // CubeDisplay::begin()). The other two were never diagnosed and date from
    // when LV_MEM_SIZE was 32-48 KB and a whole-screen render sat within a
    // kilobyte of the pool's silent while(1). repaintAll() takes none of those
    // paths: it flips the driver's mirror flag and lets the ordinary refresh
    // do the render and the upload. If it ever stalls, Serial says where —
    // "Display: full repaint requested" with nothing after it is the pool;
    // "Hanging in _waitUpdateAsyncComplete()" once a second is the DMA.
    cubeDisplay.repaintAll();
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

// A solve this short is not a record, it is a lucky draw: a nearly-solved
// cube hands the solver a handful of moves, finishes in seconds, and would
// stand as "Best" forever. Solves must be LONGER than this to feed Best and
// Average; everything still counts toward the Solves tally and Last.
static const int kStatMinMoves = 15;

// Record one completed, machine-paced solve. Three callers, one per ending a
// solve can have: the shared Unloading completion; Demo's run-completion
// transition, which keeps the cube clamped between runs and never reaches
// Unloading; and the plain solve's handover to the display spin, which does
// not reach Unloading either. Each ending is reached exactly once per solve —
// that, not a flag, is what keeps the count honest. Step Solve deliberately
// does NOT come here: it is human-paced, and a thinking pause must not become
// the best time.
static void statsRecordTimedSolve(uint32_t ms, int moves) {
    statsVals[CubeStats::Solves]++;
    statsVals[CubeStats::LastMs]   = ms;
    statsVals[CubeStats::TotalMs] += ms;
    if (moves > kStatMinMoves) {
        statsVals[CubeStats::QualSolves]++;
        statsVals[CubeStats::QualMs] += ms;
        if (statsVals[CubeStats::BestMs] == 0 || ms < statsVals[CubeStats::BestMs]) {
            statsVals[CubeStats::BestMs]    = ms;
            statsVals[CubeStats::BestMoves] = (uint32_t)moves;
        }
    }
    statsCommit();
}

// ---------------------------------------------------------------------------
//  Fault text
// ---------------------------------------------------------------------------
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
        case 91: return "Motors failed to home - check for a jam";
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
        case 82: return "Cube rotation failed - jam or encoder";
        case 90: return "Color sensor board offline - check wiring";
        case CubeSystem::ERR_ENCODER_FAULT: return "Encoder unreadable";
        // Unreachable from a rotation — an align = false executeMove() always
        // returns 0 — but kept: when these fell through to the default below,
        // an operator who had just aborted deliberately was sent off to check
        // his sensors and his cube seating. Nothing is written until every
        // face has been read, so an abort here really does leave EEPROM alone.
        case 20 + CubeSystem::ERR_ABORTED:        return "Aborted - EEPROM left untouched";
        case 20 + CubeSystem::ERR_ENCODER_FAULT:  return "Encoder unreadable";
        default: return "Check sensors and cube seating";
    }
}

// homeMotors()'s own code space. It shares the ERR_* constants with the move
// path but not the small integers — 1 here is "motors not calibrated", which is
// 21 in moveErrorText() and something else again in the scan table. One table
// per code space, for the same reason fail() demands a `src`.
static const char* homeErrorText(int code) {
    switch (code) {
        case 1: return "Motors not calibrated";
        case CubeSystem::ERR_ALIGN_TIMEOUT: return "Alignment timed out - check for a jam";
        case CubeSystem::ERR_ENCODER_FAULT: return "Encoder unreadable - check wiring";
        case CubeSystem::ERR_ABORTED:       return "Homing aborted";
        default: return "Homing failed";
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

// Close the grippers on whatever is in the bay: bottom, then ring, then top.
//
// The order is mechanically load-bearing and is NOT the mirror of the release
// (see CubeSystem::unloadCube). Every clamp in the sketch goes through here,
// so one function is one place to correct if the mechanism ever changes.
//
// Deliberately does NOT check the abort latch: the callers disagree about what
// an abort means here — which fault source it is filed under, what the screen
// says — and every one of them checks the moment this returns.
static void clampCube() {
    Cube.botServoExtend();
    Cube.ringExtend();
    Cube.topServoExtend();
}

// How long to let the mechanism settle after a clamp before driving a face.
//
// Only the solve path waits: it is about to turn a face against a cube that
// three grippers have just closed on, and the servos report position, not
// stillness. The clamps that end in a screen (the scan, the calibration
// prompt) have a human's reaction time in front of them already.
static const uint32_t kClampSettleMs = 500;

// Is the machine actually gripping the cube right now?
//
// Asked of the hardware, not of a flag. CubeServo::coarseState() and
// CubeMotors::getRingState() are both persisted to EEPROM and reconciled by
// begin() before anything can ask, so they answer honestly even on the first
// pass after a reset. The sketch's cubeLoaded is NOT this — that is the jog
// page's local bookkeeping for its own two actions.
//
// Clamped means all three at their EXTENDED stop; see gripStateOf() below for
// the numbering, which the servos and the ring do not share (a servo calls
// extended 1, the ring calls it 2). Every other value — mid-sweep, partial,
// the ring's in-motion sentinel — counts as not clamped, because the only safe
// direction to be wrong in is re-driving something that was already there.
static bool cubeIsClamped() {
    return topServo.coarseState()   == 1
        && botServo.coarseState()   == 1
        && cubeMotors.getRingState() == 2;
}

// Hand the shared mode screen over to ModeComputing. The ribbon and the bar
// go, because a finished ribbon and a full bar under "Computing" read as a
// solve that finished before it started.
//
// The frame reports the BRANCH, not the phase, and the branch has not changed
// just because the scrambling stopped — so the setOpTheme() below is a no-op
// in the common case and a repair in the one that is not: Idle Mode's
// decorative cycle can leave any of six colors on the band (see kIdleCycle),
// and Idle solves through this state.
static void toComputing() {
    cubeDisplay.setOpTheme(s_opTheme);
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
//
// The cube STAYS CLAMPED, like every other graceful end of a mode (see the
// Unloading state). Nothing is mid-move at a move boundary, the model is
// sound, and the menu it returns to assumes a held cube: Solve skips its
// clamp when cubeIsClamped(), and Eject is the one item that lets go.
static bool demoEndRequested(MenuEvent ev) {
    if (runMode != RunMode::Demo) return false;
    if (ev != MenuEvent::Select && ev != MenuEvent::Back) return false;

    char sub[24];
    snprintf(sub, sizeof(sub), "Run %d", demoRuns);
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
    opScreen(Op::Solve, "Step Solve", head,
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
//  The post-solve display spin
// ---------------------------------------------------------------------------
//  A solve does not end by dropping the cube back in the bay. The machine lets
//  go of everything except the bottom gripper and turns the cube slowly on the
//  spot, so the finished faces can be seen from every side, with the result
//  standing on the screen until someone presses SELECT — which clamps it again
//  and goes back to the menu.
//
//  PLAIN SOLVE ONLY. The Modes still finish through Unloading exactly as they
//  did: Demo loops continuously and would stall on a screen that waits for a
//  press, and Step Solve is human-paced and already interactive. Making it
//  universal is a one-line change — send ModeExecuting's and StepReady's
//  completion to Displaying instead of Unloading, and move their stats
//  recording with them — but it is not what was asked for.

// The result line. One formatter for both endings, the straight unload and the
// spin, so the two screens cannot come to disagree about how a solve is
// reported.
static void formatSolveResult(char* out, size_t n) {
    snprintf(out, n, "%d moves in %lu.%02lu s",
             Cube.solutionLength,
             (unsigned long)(solveMillis / 1000),
             (unsigned long)((solveMillis % 1000) / 10));
}

// How fast the cube turns.
//
// Eight seconds a turn: slow enough to read a face off it, fast enough that a
// whole side comes round while someone is still watching, and short enough that
// the drivers are not asked to hold torque for long.
//
// Expressed as a RATE, not an increment: a burst of steps every 80 ms is a few
// milliseconds of motion and seventy-odd of stillness, and the eye reads that
// duty cycle as a stutter however small each nudge is. The step interval is
// kept by the clock, not by how often loop() comes round.
//
// 4 * getTurnStep() is one revolution, DERIVED rather than written as 400 so a
// machine geared differently still turns at one revolution per kSpinSecs.
static const float kSpinSecs = 8.0f;
static float spinStepsPerSec() {
    const float rev = 4.0f * (float)cubeMotors.getTurnStep();
    return rev / kSpinSecs;
}

// Stop turning after this long and drop torque. The screen and its button stay
// exactly as they are; this is only about not leaving six steppers energised
// indefinitely because a machine was left standing on a desk.
static const uint32_t kSpinMaxMs = 60000;

// The bottom face's index into the pos[] six, same U R F D L B order as
// kJogCaps and CubeMotors::executeMove().
static const int kSpinMotor = 3;

static uint32_t spinStopAt = 0;
static bool     spinning   = false;

// Bring the cube back to square before anything grips it again.
//
// The spin stops wherever the display left it, and a cube standing at
// 137 degrees is not a cube the ring and the face grippers can close on.
// Finishing the revolution — forward, never back, so the backlash is not taken
// up the other way — leaves the cube in the exact orientation the solve ended
// in. That is what keeps the model honest as well as the mechanism: a whole
// number of turns changes nothing, a fraction of one silently re-labels every
// side face the solver thinks it knows.
//
// 4 x getTurnStep() is one full turn and, on this gearing, exactly one AS5600
// revolution (4096 counts at ~10.24 counts a step), so the D encoder also
// lands back on the calibration index it started from.
//
// The completion CONTINUES the slow display spin rather than snapping the
// remainder in one coordinated move. The snap version drove up to a full
// revolution at solve speed, unramped, with the entire cube's mass hanging on
// one finger — it missed steps and the cube's inertia carried it past the
// detent when torque dropped, so the "squared" cube stood visibly yawed and
// D's tracked position was a lie. The NEXT solve's pre-move drift check then
// found D off every calibration mark, burned its homing timeout walking it
// back one step a pass, and aborted the solve with the cube released
// (code 122 on the bench). At the display rate the remainder takes at most
// kSpinSecs to finish, momentum at 50 steps/s is nothing, and the count
// stays honest.
static void spinToSquare() {
    const long rev = 4L * (long)cubeMotors.getTurnStep();

    // Freshest count FIRST, while spinService() still has its base — after
    // spinEnd() it reports 0 and the remainder would be unknowable.
    long total = cubeMotors.spinService();

    // Same clock-paced step train the display used; runSpeed() emits each
    // step when it is due, so this loop's only job is to keep calling. The
    // pump keeps the panel alive and the chord accumulating, and its return
    // is deliberately ignored: squaring cannot be cut short — a half-squared
    // cube is exactly the thing this function exists to prevent, abort or no
    // abort. Whoever checks abortPending() next still sees the latch.
    while (total % rev != 0) {
        total = cubeMotors.spinService();
        pumpOnce();
    }

    // Hand D's real position back to the pos[] MultiStepper works from, and
    // let the cube come fully to rest under torque before a caller drops it
    // or closes a gripper on it.
    cubeMotors.spinEnd();
    delay(cubeMotors.getStepDelay());
}

// The two screens of a solve's thinking half — the one solveVirtual() blocks
// under, and the confirm that follows it. One function because they are the
// same picture asking the same question, "is this the cube you loaded"; only
// the line under it and the hint change.
//
// The net rather than a headline. solveVirtual() blocks unpumped for up to
// ~10 s, so this screen is up long enough to be read, and what is worth reading
// in that window is the cube the solution is about to be computed FOR — the one
// chance to catch "the machine is not holding what I think it is" before twenty
// moves are run against a bad model.
//
// So there is no headline, and that is structural rather than a choice: the net
// occupies y=60..150 and showOperation() puts the headline at 58, so a screen
// carrying both draws one through the other. setOpCubeNet() moves the sub-line
// below the net for exactly this reason, which is where the text goes instead.
// Cube State and the pattern result are built the same way.
//
// rebuildFromCubeArray() first: executeMove() mutates cubeArray only, so the
// color array the net is drawn from is stale after any mode move — a scramble,
// an idle turn, a pattern fold — and Solve is reachable straight after all
// three. Despite the UNFINISHED label on its header it is the working "refresh
// colorCubeArray" call, which is all this needs.
//
// s_opTitle, not "Solve": actSolve() has already set it, and if a mode ever
// borrows this pair of screens the title should name the operation that ran.
static void drawSolveNet(const char* sub, const char* hint) {
    Cube.virtualCube.rebuildFromCubeArray();
    opScreen(Op::Solve, s_opTitle, nullptr, hint);
    cubeDisplay.setOpCubeNet(Cube.virtualCube.getColorArray());
    cubeDisplay.setStatus(sub);
    Cube.displayUpdate();
}

// The result screen the spin runs under. Built once, on entry: nothing on it
// changes while the cube turns, and showOperation() on a tick would rebuild the
// frame twelve times a second.
//
// Action in the hint, result in the status — the same shape as Demo's "Solved!"
// screen, and for the same reason: both are a finished solve that is still
// holding the cube and waiting on a human.
static void drawSolveDisplay() {
    // Two operations end on this screen and they have different things to say.
    // Branching on runMode rather than passing a flag keeps the caller in
    // AppState::Displaying ignorant of which one it is showing — the same
    // reason every other shared Mode* state branches on it.
    if (runMode == RunMode::Pattern) {
        // The MODEL's net, not the stored preview: the screen shows what the
        // machine believes it built, and a mismatch against the menu's preview
        // IS the diagnostic. No headline — the net owns the middle of the
        // screen, the way Cube State and the calibration prompt draw it — so
        // the pattern's name rides the line beneath the net instead.
        showOp(Op::Done, s_opTitle, nullptr, "SELECT clamps the cube and finishes");
        cubeDisplay.setOpCubeNet(Cube.virtualCube.getColorArray());
        cubeDisplay.setStatus(patName);
    } else {
        char sub[64];
        formatSolveResult(sub, sizeof(sub));
        showOp(Op::Done, s_opTitle, "Solved!", "SELECT clamps the cube and finishes");
        cubeDisplay.setStatus(sub);
    }
    Cube.displayUpdate();
}

// ---------------------------------------------------------------------------
//  Eject — noticing that the cube has been taken
// ---------------------------------------------------------------------------
//  A keypress to say "the cube is out" carries no information — the operator
//  standing there with it in hand already knows, and so could the machine. So
//  it watches instead: both color boards stay illuminated, all eighteen
//  sensors are polled, and when the light coming back from the cube collapses
//  the bottom servo retracts on its own. SELECT still finishes by hand,
//  because none of this is guaranteed to work on a given machine — see the
//  give-up timeout below.
//
//  What it deliberately is NOT is a scan. presenceSweep() hands back raw
//  white-channel counts with no calibration and no filtering; the only
//  question asked of them is "is a lot less light coming back than a moment
//  ago", which is the one question an uncalibrated channel can answer honestly.
//
//  HOW IT DECIDES: tare, pick witnesses, watch for the fall.
//
//  At the eject height the cube's lowest row sits in front of the TOP row of
//  each scanner board and the other two rows are held clear for the hand. So
//  a few sensors are looking at a sticker from a couple of millimetres and
//  read the illumination LED bounced straight back, and the rest are looking
//  at the bay. WHICH few is a thing this source tree cannot settle: the index
//  names {UL, UM, UR, ...} in CubeHardwareConfig.cpp were written before the
//  scanner assembly was rotated 90 degrees in CAD (README), so the physical
//  top row may be 0/1/2 or may be a column of that table. The first version
//  of this watch guessed one sensor, guessed wrong, and stood for two minutes
//  waiting on a reading of the empty bay that could never fall. This one does
//  not guess:
//
//    1. TARE. Once the LEDs have warmed, sweep all eighteen sensors
//       kEjectTareSweeps times and average each into a baseline — taken
//       while the cube is certainly still there, against the very sticker
//       that is about to leave.
//    2. PICK WITNESSES. Any sensor whose baseline is at least
//       kEjectWitnessPct of the brightest one is looking at the cube. A
//       sticker at 2 mm returns the LED far harder than a bay wall at 20 mm,
//       whichever of the six colors it is, so brightness alone sorts them.
//       The rest are ignored for the remainder of the watch.
//    3. WATCH. The cube is gone when MORE THAN HALF the witnesses have fallen
//       below kEjectGonePct of their own baselines for kEjectGoneReads sweeps
//       in a row.
//
//  Why a FALL and not "any change", which would be the simpler rule: a hand
//  coming into the chamber is also a change. It reflects the LED into the
//  sensors that were looking at the bay, and it gets there a second or so
//  before the cube leaves — act on that and the gripper drops while the
//  fingers are still closing, which is the one outcome this feature exists to
//  avoid. A hand cannot get between a sticker and the sensor it sits 2 mm
//  from, so a witness FALLING means the cube moved and nothing else; a rise
//  anywhere means nothing and is never consulted.
//
//  Why a majority and not all of them: a sensor that qualified as a witness
//  on a bright bay wall rather than on a sticker would otherwise veto forever.
//  Why a majority and not any one: a cube tilted in the fingers can uncover
//  one sensor while still firmly on the platform.
//
//  The tare and the verdict are printed to Serial, because the bench is where
//  these thresholds get confirmed, and eighteen numbers say more than a
//  screen could.

// Poll interval. A sweep of both boards is on the order of 20 ms of bus time
// and does not block on anything else, so this is not about cost — it is
// about not hogging the bus more often than the job needs. Five looks a
// second is far quicker than a hand can lift a cube out.
static const uint32_t kEjectPollMs = 200;

// Sweeps averaged into the tare. Three at kEjectPollMs is 0.6 s on top of the
// LED warm-up: long enough to shave the single-window noise off the baseline,
// short enough that nobody has reached for the cube yet.
static const uint8_t kEjectTareSweeps = 3;

// A sensor is a witness if its baseline is at least this percentage of the
// brightest baseline on either board.
//
// Blue reflects roughly a third of what white does under the LED, so at 50% a
// blue sticker beside a white one does NOT qualify — and it does not have to.
// The verdict needs a majority of the witnesses to fall, not every cube-facing
// sensor to be a witness; missing a dark sticker costs one vote, while letting
// a bay-facing sensor in costs a vote that can never be cast. Set this lower
// and the bay starts qualifying; set it higher and more stickers stop. 50% is
// the middle of a range nobody has measured yet, and the Serial tare line is
// how to measure it.
static const int kEjectWitnessPct = 50;

// "Fallen" is a drop to this percentage of the witness's own baseline.
// RELATIVE, never an absolute count: the reflected level depends on ambient
// light, on LED output, and on which sticker color happens to be facing the
// sensor, so a number tuned on one machine on one afternoon would be wrong on
// the next one.
//
// 40% is a deliberately wide margin. A cube a few millimetres away reflects
// the LED far harder than an empty chamber leaks ambient, so the real gap is
// expected to be much larger than this; the slack absorbs the cube being
// nudged or turned in the operator's fingers on its way out.
static const int kEjectGonePct = 40;

// How many consecutive "majority fallen" sweeps it takes to believe it.
//
// This is NOT pointless latency, and it is the part that will look like it. A
// cube is not removed instantly: fingers close around it, it tilts, it lifts,
// over something like a second, and halfway through that it can be clear of
// its witnesses while still resting on the platform under a loose grip. Act
// on a single sweep and the platform drops out from under a cube nobody is
// holding yet. Four in a row at kEjectPollMs is 0.8 s of the cube being
// reliably away from its sensors.
static const uint8_t kEjectGoneReads = 4;

// Give up on watching after two minutes and finish anyway.
//
// Generous on purpose — this is not a timeout on the operator, it is a timeout
// on the SENSORS. If the cube cannot be seen at this height at all the fall
// never comes, and without this the machine would stand with the cube up and
// both illumination LEDs burning until someone happened to press a button.
// Retracting is the safe place to leave it: the cube drops back into the bay,
// which is exactly where unloadCube() has always put it.
static const uint32_t kEjectGiveUpMs = 120000UL;

static uint32_t ejectLastTick = 0;          // throttle clock, as the diag pages use
static uint32_t ejectGiveUpAt = 0;
static long     ejectTareSum[2][9];         // running sums while taring
static uint8_t  ejectTareN[2][9];           // good readings behind each sum
static uint8_t  ejectTareCount = 0;         // sweeps taken so far
static bool     ejectTared     = false;     // baseline and witnesses are valid
static int      ejectBaseline[2][9];        // per-sensor tare
static bool     ejectWitness[2][9];         // which sensors saw the cube at tare
static uint8_t  ejectWitnessCount = 0;
static uint8_t  ejectLowRuns      = 0;      // consecutive majority-fallen sweeps

static ColorSensor& ejectBoard(int b) {
    return (b == 0) ? colorSensor1 : colorSensor2;
}

// Open the watch on both boards and forget everything the last one learned.
// The first sweep happens on the very next pass; presenceSweep() reports its
// own warm-up, so there is nothing to wait for here.
static void ejectWatchBegin() {
    for (int b = 0; b < 2; b++) {
        ejectBoard(b).presenceBegin();
        for (int i = 0; i < 9; i++) {
            ejectTareSum[b][i]  = 0;
            ejectTareN[b][i]    = 0;
            ejectBaseline[b][i] = 0;
            ejectWitness[b][i]  = false;
        }
    }
    ejectTareCount    = 0;
    ejectTared        = false;
    ejectWitnessCount = 0;
    ejectLowRuns      = 0;
    ejectLastTick     = millis() - kEjectPollMs;
    ejectGiveUpAt     = millis() + kEjectGiveUpMs;
}

// Close the watch: LEDs off, muxes released. Every exit from EjectWait calls
// this — detection, SELECT, the give-up timeout and the abort chord — and it
// is idempotent, so the one that gets called twice costs nothing and the one
// that forgets is the only real bug available here.
static void ejectWatchEnd() {
    ejectBoard(0).presenceEnd();
    ejectBoard(1).presenceEnd();
}

// The ONE way out of EjectWait, for all four of them. Written once because the
// order matters and the LEDs are easy to forget: stop illuminating, put the
// horn down, go back to the menu — where syncMenuRoot() reverts to the
// pre-scan list, which is the real confirmation that the eject happened.
//
// The abort path calls clearAbort() first and for a reason; see there.
static void ejectFinish() {
    ejectWatchEnd();
    Cube.botServoRetract();
    toMenu();
}

// Turn the averaged sums into a baseline, choose the witnesses, and say so on
// Serial. Called exactly once per watch, by ejectWatch().
static void ejectTareDone() {
    // Each sensor is averaged over the readings IT gave, so one that faulted
    // on a sweep is not dragged down by it. One that never answered at all
    // gets a baseline of zero, which below makes it no witness: a sensor that
    // cannot be read cannot testify, and must not block the ones that can.
    int brightest = 0;
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 9; i++) {
            ejectBaseline[b][i] = ejectTareN[b][i]
                ? (int)(ejectTareSum[b][i] / ejectTareN[b][i]) : 0;
            if (ejectBaseline[b][i] > brightest) brightest = ejectBaseline[b][i];
        }
    }

    // Multiplied rather than divided so a dim baseline does not lose the
    // threshold to truncation. A board where everything reads zero — LED
    // dead, sensors dark — makes everything a witness of a zero baseline, and
    // nothing can fall below 40% of zero, so the give-up timer takes it:
    // the right answer for a board that cannot see.
    ejectWitnessCount = 0;
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 9; i++) {
            ejectWitness[b][i] = ((long)ejectBaseline[b][i] * 100 >= (long)brightest * kEjectWitnessPct);
            if (ejectWitness[b][i]) ejectWitnessCount++;
        }
    }
    ejectTared = true;

    Serial.println(F("Eject tare (white counts; * = witness):"));
    for (int b = 0; b < 2; b++) {
        Serial.print(F("  board "));
        Serial.print(b + 1);
        Serial.print(F(":"));
        for (int i = 0; i < 9; i++) {
            Serial.print(' ');
            Serial.print(ejectBaseline[b][i]);
            if (ejectWitness[b][i]) Serial.print('*');
        }
        Serial.println();
    }
    Serial.print(F("  witnesses: "));
    Serial.println(ejectWitnessCount);
}

// One throttled look. Returns true once the cube is believed to be gone.
static bool ejectWatch() {
    if (millis() - ejectLastTick < kEjectPollMs) return false;
    ejectLastTick = millis();

    // Board 1 then board 2, never interleaved: they share a bus and an I2C
    // address, and presenceSweep() clears its muxes on the way out precisely
    // so the next board's sweep finds the bus to itself.
    int w[2][9];
    for (int b = 0; b < 2; b++) {
        const int r = ejectBoard(b).presenceSweep(w[b]);

        // A NEGATIVE IS NOT AN EMPTY BAY. -1 is "no session" and -2 is "still
        // warming up": neither is data, and the sweep that did not happen
        // wrote nothing into w[]. The other board may be mid-sweep-able, but
        // one board's numbers without the other's would skew the tare, so the
        // whole look is skipped. The give-up timer is what covers a session
        // that never warms.
        if (r < 0) return false;

        // r > 0 is "this many sensors faulted", each marked -1 in w[]. Those
        // are handled per sensor below: a fault is no evidence either way.
    }

    if (!ejectTared) {
        // Fold in whatever answered. A faulted sensor is skipped for this
        // sweep, not averaged in as -1 — that would make it look dim and drop
        // it from the witnesses over a bus hiccup.
        for (int b = 0; b < 2; b++) {
            for (int i = 0; i < 9; i++) {
                if (w[b][i] < 0) continue;
                ejectTareSum[b][i] += w[b][i];
                ejectTareN[b][i]++;
            }
        }
        if (++ejectTareCount >= kEjectTareSweeps) ejectTareDone();
        return false;
    }

    // Count the witnesses that have fallen. A faulted witness is neither
    // fallen nor standing — leave it out of the numerator and keep it in the
    // denominator, so a bus hiccup can only ever delay the verdict, never
    // hasten it.
    uint8_t fallen = 0;
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 9; i++) {
            if (!ejectWitness[b][i] || w[b][i] < 0) continue;
            // Integer arithmetic, and multiplied rather than divided so a
            // baseline under 100 does not lose the threshold to truncation.
            if ((long)w[b][i] * 100 < (long)ejectBaseline[b][i] * kEjectGonePct) fallen++;
        }
    }

    if (fallen * 2 > ejectWitnessCount) {
        if (++ejectLowRuns >= kEjectGoneReads) {
            Serial.print(F("Eject: cube gone ("));
            Serial.print(fallen);
            Serial.print('/');
            Serial.print(ejectWitnessCount);
            Serial.println(F(" witnesses fell)"));
            return true;
        }
    } else {
        ejectLowRuns = 0;       // the run has to be consecutive to mean anything
    }
    return false;
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
//  That is the ONE INTENTIONAL EXCEPTION to the frame band being wayfinding:
//  everywhere else the color says which branch of the menu you are standing
//  in, and here it cycles all six on purpose. Tying it to the moves is what
//  keeps it honest — a color change means something happened, so the machine
//  is visibly alive from further away than the move counter can be read. Idle
//  Mode's other screens (the clamp, the solve it hands off to) wear the mode's
//  own yellow like every other mode does.
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

    // The decorative cycle, overriding s_opTheme's yellow for as long as this
    // screen is up — see kIdleCycle. Every other setOpTheme() call in this
    // sketch passes s_opTheme; this is the exception, and the only one.
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
//  again to send. A face or rotation row instead takes the wheel on SELECT:
//  every detent, UP or DOWN is then a turn, because a turn you want to repeat
//  should not cost three presses.
//
//  The frame is Settings' yellow throughout — it says where you are, not what
//  is armed — so "I am about to move something" is CubeDisplay::setOpArmed():
//  a breathing ARMED badge on the title line, backed by the hint line and the
//  row mark. A shape that appears and a motion that continues survive a frame
//  colour that does not change.
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

// Every named stop each gripper has — four apiece, so nothing the machine can
// be commanded to do is unreachable from the page whose whole job is exercising
// it. Eject is there because the bottom servo parks at it after every unload.
//
// The order is DECLARATION order and deliberately NOT travel order, which looks
// wrong on the bottom servo: its Eject is tuned to 120 deg against Partial's
// 195, so Eject sits physically BELOW Partial and the wheel does not walk the
// horn monotonically down the list.
//
// Travel order was rejected because it is not a constant. It is a property of
// four editable numbers, so it would re-sort itself the moment somebody changed
// one on Parameters — the same row would mean a different pose between two
// visits, on the one page whose job is saying where a part IS. This order is
// fixed, it is the order the ring's own tuning section already lists (Retract /
// Partial / Middle / Extend), and every row is labelled with the pose it sends,
// so nothing here depends on guessing which way the wheel walks the horn.
//
// The top servo has no Partial and no Eject tuning row, so ejectTarget() falls
// back to partialTarget() and its two middle rows send the horn to the SAME
// ANGLE. That is left visible rather than collapsed into one row: the coarse
// state each leaves behind IS distinct (2 against 3), so the row reads back
// whichever was actually sent, and the day someone pins a top-servo eject this
// page needs no change. Hiding the row would hide the missing tuning row too.
static const int kJogPos = 4;                          // stops per gripper
static const char* const kGripPos[3][kJogPos] = {
    { "Retract", "Partial", "Eject",  "Extend" },
    { "Retract", "Partial", "Eject",  "Extend" },
    { "Retract", "Partial", "Middle", "Extend" },
};

// Where each gripper is, as an index into the row above. -1 renders as "?".
//
// Seeded from the machine itself on every entry to this page —
// CubeServo::coarseState() and CubeMotors::getRingState(), both of which are
// restored from EEPROM at boot — so the page opens telling the truth instead of
// three question marks.
//
// RE-READ on entry, never cached across visits: between visits a scan, a solve
// or a tuning preview moves all three without telling this page, so a value
// latched at boot would be stale and asserting a position that stopped being
// true hours ago. Within one visit the page's own moves keep it current.
static int8_t gripAt[3] = { -1, -1, -1 };

// Translate a gripper's own state numbering into this page's four stops.
//
// The servo does not number them the way the rows do: it calls extended 1,
// partial 2 and ejected 3, while the rows read Retract / Partial / Eject /
// Extend. Mapping is cheaper than renumbering, because the servo's value is
// persisted in EEPROM and a renumber would misread every machine in the field.
//
// One legacy case is not worth code: a servo whose state byte was written by
// firmware that recorded one value for partial() and eject() alike reads 2
// until it next moves, and is reported Partial while physically ejected.
// Wrong by one name, self-correcting the first time eject() runs under this
// build, and harmless meanwhile because begin() retracts out of 2 and 3 alike.
static int8_t gripStateOf(const CubeServo& s) {
    switch (s.coarseState()) {
        case 0:  return 0;      // retracted
        case 1:  return 3;      // extended
        case 2:  return 1;      // partial
        case 3:  return 2;      // ejected
        default: return -1;     // mid-sweep, aborted, or moved by previewRaw()
    }
}

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
    // sketch titles the same page "Actuators" after its own item. One Op kind:
    // the frame is the Settings branch's yellow throughout, and armed is said
    // by the badge below, the hint line, and the mark on the entered row.
    opScreen(Op::Calibrate, "Hardware Test", nullptr, hint);

    // The badge, and it has to come AFTER opScreen(): showOperation() clears
    // it, so a page that repaints on every interaction must re-assert it or it
    // flickers out. Deliberately non-colour — see CubeDisplay::setOpArmed().
    // It stays up while a move runs, because then the machine is not merely
    // armed, it is going, and dropping the badge mid-move would read as safe.
    cubeDisplay.setOpArmed(jogArmed ? "ARMED" : nullptr);

    cubeDisplay.setOpLines(lines, kJogRows, marks);

    // Everything that turns lives in the strip: the six faces, then the two
    // whole-cube rotations. Same gesture — point at a thing, turn it.
    const int8_t fill[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    const int active = (jogSel >= kJogRows) ? jogSel - kJogRows : -1;
    cubeDisplay.setOpChipRow(0, fill, kJogCaps, kJogFaces + kJogRots, active, 142);

    Cube.displayUpdate();
}

// Send one gripper to one of its four stops. Row index in, motion out; the
// cases run in kGripPos's row order so the two tables can be read side by side.
//
// Always through the CubeSystem wrapper, never topServo/botServo/cubeMotors
// directly: the wrappers are what pump the UI and honour servoDelay, and
// reaching past them is how a jog freezes the panel and stops noticing the
// SELECT+LEFT abort chord halfway through a sweep.
static void driveGripper(int part, int pos) {
    if (part == 0) {
        if (pos == 0)      Cube.topServoRetract();
        else if (pos == 1) Cube.topServoPartial();
        // Lands on the same angle as Partial until someone pins a top-servo
        // Eject row — but records a different coarse state, so the row reads
        // back Eject rather than silently claiming the machine did nothing.
        else if (pos == 2) Cube.topServoEject();
        else               Cube.topServoExtend();
    } else if (part == 1) {
        if (pos == 0)      Cube.botServoRetract();
        else if (pos == 1) Cube.botServoPartial();
        else if (pos == 2) Cube.botServoEject();
        else               Cube.botServoExtend();
    } else {
        if (pos == 0)      Cube.ringRetract();
        else if (pos == 1) Cube.ringPartial();
        else if (pos == 2) Cube.ringMiddle();
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
        clampCube();
        gripAt[0] = gripAt[1] = gripAt[2] = 3;      // all extended
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
        gripAt[1] = 2;          // bottom at the EJECT height
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
    // Ask the machine where it is, ON ENTRY. The claimed positions only hold
    // within one visit — between visits every core operation and every
    // tuning-editor preview moves the servos without telling this page, so a
    // value carried over from last time would assert a position that stopped
    // being true the moment a scan ran. Re-reading here costs nothing and is
    // the difference between a diagnostics page and a page of question marks:
    // all three parts persist their state to EEPROM and begin() has already
    // acted on it, so after a clean boot this opens on Retract/Retract/Retract
    // rather than "?".
    // cubeLoaded stays: both of that row's actions end at a known state and
    // are safe to repeat, which is more use than a third question mark.
    gripAt[0] = gripStateOf(topServo);
    gripAt[1] = gripStateOf(botServo);
    switch (cubeMotors.getRingState()) {
        // The ring's numbering is NOT this page's row order, which is why every
        // case is written out. CubeMotors numbers its stops in the order they
        // were ADDED — 0 retracted, 1 halfway, 2 extended, 3 partial, partial
        // having arrived last — while the rows run Retract / Partial / Middle
        // / Extend. So state 3 maps to row 1 and state 1 maps to row 2. That
        // lone 3 in the middle of the switch looks like a typo and is not:
        // arithmetic or a tidying renumber here is a silent bug that reports
        // the ring one stop away from where it is standing.
        case 0:  gripAt[2] = 0; break;      // retracted -> Retract
        case 3:  gripAt[2] = 1; break;      // partial   -> Partial
        case 1:  gripAt[2] = 2; break;      // halfway   -> Middle
        case 2:  gripAt[2] = 3; break;      // extended  -> Extend
        // -1 is the in-motion sentinel, only observable after a move that was
        // aborted or lost power. Honestly unknown, so it stays "?".
        default: gripAt[2] = -1; break;
    }
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

//  THE APPLY GATE, and the one thing it must not break
//  ---------------------------------------------------
//  Nothing the wheel does reaches an owner or EEPROM by itself. A parameter is
//  easy to change by accident here — one press and one detent — and a machine
//  that quietly kept the accident would offer no way back to the number that
//  worked.
//
//  What is gated is the COMMIT, never the preview. Six rows are TP_LIVE and a
//  servo endpoint is set BY EYE: you turn the wheel and watch the horn follow.
//  Gating that would make servo tuning impossible, so previewRaw() still runs
//  on every single detent, ungated — which is what TuneParam::preview was split
//  away from set() for in the first place. A change too large to have come
//  from a wheel is SWEPT rather than written straight out, so the first detent
//  in a row moves the horn instead of snapping it there. What stops is set().
//  While a section is open the owners and EEPROM still hold parBase[], the wheel
//  moves parVal[], and set() plus tuneSaveAll() are reached from exactly ONE
//  place: the Apply row.
//
//  The consequence worth naming out loud: a previewed servo is PHYSICALLY
//  standing at a value the machine does not otherwise know about. So
//  discarding is not a matter of forgetting numbers — parDiscard() drives
//  every previewed part back to its base first. And LEFT, with edits pending,
//  ASKS in red rather than choosing silently between saving and throwing away.
static const int kParMaxRows = CubeDisplay::kOpLines;  // six rows plus Apply
static int32_t   parBase[kParMaxRows];  // what the machine and EEPROM hold
static int32_t   parVal[kParMaxRows];   // what the wheel has been moving
static bool      parSaved = false;      // "Saved" on the hint until the next
                                        // press, so an Apply whose rows all
                                        // look the same afterwards still says
                                        // that it did something

// The Apply row sits AFTER the section's own rows. Last, because it is the end
// of the job, and because row 0 is where the cursor opens and a write to
// EEPROM should not be the thing one press away.
static int parApplyRow() { return parSec->count; }

static bool parRowDirty(int i) { return parVal[i] != parBase[i]; }

static int parDirtyCount() {
    int n = 0;
    for (int i = 0; i < parSec->count; ++i) if (parRowDirty(i)) ++n;
    return n;
}

// Dirty is "differs from what the machine holds"; tuned is "differs from the
// compiled default". They are independent — a row can be either, both or
// neither — and the screen says them differently: a star for dirty, amber for
// tuned. Tuned follows the SHOWN value, parVal[], not EEPROM: the question it
// answers is "is the number I am looking at the default", and a row the wheel
// has just put back to its default should stop being amber as it does so.
static bool parRowTuned(int i) {
    return parVal[i] != kTune[parSec->first + i].def;
}

static int parTunedCount() {
    int n = 0;
    for (int i = 0; i < parSec->count; ++i) if (parRowTuned(i)) ++n;
    return n;
}

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
    static char rows[kParMaxRows][44];
    const char* lines[kParMaxRows];
    CubeDisplay::RowMark marks[kParMaxRows];

    const int n = parSec->count;
    for (int i = 0; i < n; ++i) {
        const TuneParam& p = kTune[parSec->first + i];
        char value[24], shown[32];
        // parVal[], not p.get(): the row shows what the wheel has been moving,
        // and that is not what the owner holds until Apply has run.
        tuneFormat(p, parVal[i], value, sizeof(value));
        if (i == parSel && parEdit) {
            // Angle brackets in plain ASCII — the baked-font glyph range, see
            // drawJog().
            snprintf(shown, sizeof(shown), "< %s >", value);
        } else {
            snprintf(shown, sizeof(shown), "%s", value);
        }
        // A leading star is the dirty marker, and it goes in the LABEL column
        // rather than becoming a RowMark: the cursor already owns the mark, and
        // a changed row under the cursor is exactly the row whose state matters
        // most to see.
        snprintf(rows[i], sizeof(rows[i]), "%s%s\t%s",
                 parRowDirty(i) ? "*" : "", p.name, shown);
        lines[i] = rows[i];
        // Amber for a value that is not the compiled default, so a page read
        // cold still says which of its numbers someone has tuned. The cursor
        // keeps its own color on its own row — that is what the star is for.
        marks[i] = (i == parSel)     ? CubeDisplay::RowMark::Busy
                 : parRowTuned(i)    ? CubeDisplay::RowMark::Tuned
                                     : CubeDisplay::RowMark::Plain;
    }

    // The Apply row. Its value is the count, so "have I got anything pending"
    // is answered without scanning every row for a star.
    const int dirty = parDirtyCount();
    if (dirty) snprintf(rows[n], sizeof(rows[n]), "Apply\t%d changed", dirty);
    else       snprintf(rows[n], sizeof(rows[n]), "Apply\tno changes");
    lines[n] = rows[n];
    marks[n] = (parSel == n) ? CubeDisplay::RowMark::Busy
             : dirty         ? CubeDisplay::RowMark::Good
                             : CubeDisplay::RowMark::Plain;

    // The Apply row has no TuneParam behind it, so `sel` is only ever read in
    // the branches below that this rules out.
    const bool onApply = (parSel == n);
    const TuneParam& sel = kTune[parSec->first + (onApply ? 0 : parSel)];

    // The hint bar answers "what am I allowed to enter", which only matters
    // once you are entering something. Browsing, it says how to start.
    char hint[48];
    if (parSaved) {
        snprintf(hint, sizeof(hint), "Saved to EEPROM");
    } else if (onApply) {
        // "%s" rather than handing the ternary straight to snprintf as the
        // format: a non-literal format string is what -Wformat-security is for.
        //
        // The row's second job — DOWN loads this page's defaults, see
        // parLoadDefaults() — is written nowhere else on the screen, so it is
        // offered here whenever it would do something. A page already at its
        // defaults with nothing pending says so instead.
        snprintf(hint, sizeof(hint), "%s",
                 dirty            ? "SELECT saves - DOWN = defaults"
               : parTunedCount()  ? "DOWN loads this page's defaults"
                                  : "All defaults - nothing to apply");
    } else if (parEdit && (sel.flags & (TP_BOOL | TP_ENUM))) {
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

    // Editing is said by the hint line, which becomes the value's range, and
    // by the row's own mark — never by the frame, which is Settings' yellow.
    opScreen(Op::Calibrate, parSec->title, nullptr, hint);

    // The description goes in the sub-line, above the rows and below the title.
    // It has to be set BEFORE setOpLines(), which reads it to decide where the
    // rows start — a sub-line added afterwards lands on top of row one. On the
    // Apply row it says what Apply is FOR, since that is the one row whose
    // meaning is not written on it.
    cubeDisplay.setStatus(onApply ? "Nothing is stored until you press this"
                                  : sel.help);
    // n + 1 rows: the section, then Apply. Six is the largest section, so this
    // is at most seven, which is exactly kOpLines.
    cubeDisplay.setOpLines(lines, n + 1, marks);
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

    opScreen(Op::Error, parSec->title, head,
             "SELECT to go on - LEFT to stop");
    // Both strings have been wrong before, each by promising something the
    // code underneath had stopped doing — so each says only what is true:
    // Non-live rows change nothing until Apply, so do not promise "the next
    // move". Live rows MOVE on every detent but not "at once": previewRaw()
    // SWEEPS anything larger than a wheel-sized change, and the first detent
    // always is — parVal is seeded from the stored endpoint, not from where
    // the horn stands — so the opening move is a pumped travel of a couple of
    // seconds that "at once" would make read as a hang.
    cubeDisplay.setStatus((p.flags & TP_LIVE)
                          ? "The part moves as the wheel turns"
                          : "Takes effect once you Apply");
    Cube.displayUpdate();
}

static void tuneEnter(const TuneSection* sec) {
    parSec   = sec;
    parSel   = 0;
    parEdit  = false;
    parGate  = false;
    parMoved = false;
    parSaved = false;
    // The snapshot the whole gate rests on. Read from the OWNER, not from
    // EEPROM: this is the value the machine is actually running on, which is
    // what a discard has to drive the hardware back to, and the two differ
    // whenever a previous section was left without applying.
    for (int i = 0; i < sec->count; ++i) {
        parBase[i] = kTune[sec->first + i].get();
        parVal[i]  = parBase[i];
    }
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
// one action here that cannot be undone by turning the wheel back. It is also
// the WHOLE table — one section at a time is the Apply row's job inside each
// page (parLoadDefaults()), which stages the defaults instead of writing them. The servos
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

// The horn is wherever the last preview left it, and CubeServo::begin() trusts
// the stored POSITION to decide how far its first sweep travels — so a stale
// one is what arms a full-travel slam on the next power-up. Written once, at
// the end, rather than once per detent. It stores where the servo is standing,
// not what its endpoints are, so it is right on the way out of an Apply and
// equally right on the way out of a discard.
static void parPersistHorns() {
    if (parMoved) {
        topServo.persist();
        botServo.persist();
        parMoved = false;
    }
}

// Leaving a section, with nothing left pending. Leaving is not a decision to
// keep anything — the Apply row is, see the apply-gate essay above — and every
// path that reaches here has either applied, discarded, or had nothing to
// apply. So nothing is saved here.
static void parLeave() {
    parPersistHorns();
    toMenu();
}

// The Apply row: the ONE place in this editor that writes an owner or EEPROM.
static void parApply() {
    // Saying so beats a screen that flashes and changes nothing.
    if (parDirtyCount() == 0) { parSaved = false; drawTune(); return; }

    for (int i = 0; i < parSec->count; ++i) {
        if (!parRowDirty(i)) continue;
        const TuneParam& p = kTune[parSec->first + i];
        p.set(parVal[i]);
        // Read it BACK rather than assuming the write took verbatim: several
        // setters clamp (clampServoPos is the obvious one), and a base left
        // disagreeing with its owner by one degree would leave the row starred
        // for ever and Apply for ever offering to save it again.
        parVal[i] = parBase[i] = p.get();
    }

    parPersistHorns();

    // One block for all 28 rows, this section's or not — EEPROM.update() means
    // the rows nothing touched cost no write at all.
    tuneSaveAll();
    parSaved = true;
    drawTune();
}

// The Apply row's second job: put every row of THIS page at its compiled
// default — as PENDING values, never as a write.
//
// Reset Defaults clears the whole EEPROM block — all 28 defaults at once — so
// this is the only way to put ONE section back without losing the other
// five's tuning. It goes through the Apply gate like every other change, for
// the reason every other change does: the stars show exactly which rows a
// press of Apply would alter, and LEFT still asks before the proposal is
// thrown away.
//
// Reached from UP or DOWN on the Apply row rather than from a row of its own,
// because there is no room for one: Ring and Color are six rows, Apply is the
// seventh, and seven is kOpLines.
//
// No preview(), on purpose. A live row is driven only by the wheel, with the
// operator's eye on the part; loading the Bottom Servo page's defaults would
// otherwise sweep two horns the instant a button was pressed — the surprise
// actResetTune() refuses to spring, for the same reason. The part moves when
// Apply stores the value and the machine next uses it, which is how Reset
// Defaults has always behaved. parDiscard() copes with a dirty live row that
// was never previewed: "driving it back" to a base the horn never left is a
// single write to where it already stands.
static void parLoadDefaults() {
    for (int i = 0; i < parSec->count; ++i) {
        parVal[i] = kTune[parSec->first + i].def;
    }
    drawTune();
}

// The answer to the red confirm: throw the pending values away and go.
//
// Restoring the NUMBERS is only half of it. A TP_LIVE row has already driven
// its part to the previewed angle — that is the entire point of those rows —
// so the horn is physically standing at a value that is about to stop existing
// anywhere. Drive each one back to its base BEFORE forgetting it, or a
// cancelled edit leaves a gripper at an endpoint nothing remembers. Rows are
// walked in table order, so per servo the last dirty row wins, which is the
// same rule an ordinary edit already ends on.
static void parDiscard() {
    for (int i = 0; i < parSec->count; ++i) {
        const TuneParam& p = kTune[parSec->first + i];
        if (parRowDirty(i) && p.preview) p.preview(parBase[i]);
        parVal[i] = parBase[i];
    }
    parLeave();
}

// The confirm shown when LEFT would walk away from unapplied edits.
//
// Red, and shaped like Reset Defaults' confirm, because it is the same kind of
// question: one press from here throws work away and there is no undo. It asks
// about DISCARDING rather than about leaving, so the destructive answer is the
// one SELECT gives — the same way round as every other confirm in this sketch
// — and LEFT puts you back on the list, one row away from Apply.
static void drawParamsLeave() {
    const int n = parDirtyCount();
    char head[64];
    snprintf(head, sizeof(head), "Discard %d change%s?", n, (n == 1) ? "" : "s");

    opScreen(Op::Error, parSec->title, head, "SELECT discards - LEFT goes back");
    cubeDisplay.setStatus("Apply, on the last row, keeps them");
    Cube.displayUpdate();
    state = AppState::ParamsLeave;
}

// ---------------------------------------------------------------------------
//  Sensor Test — live hardware readouts
// ---------------------------------------------------------------------------
//  Ported from Test_Menu's sensor screens — change both — with the canned
//  readings replaced by the real reads their porting comments name. Split in
//  two because they answer different questions. The color boards want "is any
//  sensor disagreeing with its neighbours", which is a picture. The motor
//  encoders want "what angle is each one reading", which is a list of numbers
//  — and, because a diagnostic that can only watch is half a diagnostic, a
//  wheel to turn one of them with.
//
//  In the simulator every VEML read returns 0 and every encoder read fails, so
//  these pages show faulty cells, "unusable" and "err -3" rows. Layout and
//  navigation are what the sim verifies; representative data is what
//  Test_Menu's canned demos exist for — both stay. Real values are a bench
//  check.

// ---- Color Sensors --------------------------------------------------------
//
//  Reads go through ColorSensor::liveRead(), never scanSingle(). scanSingle()
//  spends ~900 ms in lamp warm-up and integration per reading, and loop() is
//  inside it for that long: pollEvent() does not run, and the seesaw read is
//  a level check rather than a latched edge, so a button pressed and released
//  in that window is never seen — the page is deaf most of the time and LEFT
//  "does not work". liveRead() holds the mux channel and the lamp between
//  calls, so re-reading the selected sensor costs four I2C reads and no wait,
//  and moving to another costs one integration window. The speed and the
//  navigation are one fix, not two.

// Cursor, 0..17 across BOTH boards — the numbering CubeDisplay::setOpGrid()
// takes for its own cursor, so nothing has to take it apart except the code
// that actually reads a sensor.
static int8_t  senSel  = 0;
static uint8_t senNext = 0;    // the opening sweep's round-robin pointer

// Last classification per sensor, as a chip color index or one of setOpGrid()'s
// sentinels. Every cell starts kCellUnread and stays hollow until that sensor
// has answered: a guessed color on a diagnostic is worse than an empty box.
static int8_t senFill[2][9];

// One bit per sensor that has answered at all, which is NOT the same question
// as "is its cell still hollow". A sensor looking at an empty chamber
// classifies as 'E', which has no chip color and draws hollow — so the cell
// alone cannot say whether the sweep has been there yet, and without this the
// sweep would never finish on a machine with no cube in it.
static uint32_t       senSeen     = 0;
static const uint32_t kSenAllSeen = (1UL << 18) - 1;

// Which board is currently holding a mux channel, or -1 for neither.
static int8_t senHeldBoard = -1;

// The two board captions, built once on entry. Health comes from stored
// calibration, which cannot change while this page is up, and boardHealthRow()
// is nine separations and nine health checks per board — not arithmetic to
// repeat sixteen times a second for an answer that cannot have moved.
static char senCapA[44], senCapB[44];

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
    // worst is a separation x1000 seeded at 9999, so it has an upper bound
    // but not a lower one — a bad calibration can hand back a negative, and
    // that is real information worth showing rather than hiding. Bounded
    // only enough to make the width provable.
    if (worst < -9999) worst = -9999;
    snprintf(out, n, "%d/9 healthy, sep %d", ok, worst);
    return ok;
}

// Every path out of the color-sensor pages funnels through here, so the one
// exit that forgets cannot leave the hardware held.
//
// liveEnd() rather than setLED(false): the lamp is only half of what a live
// session holds. A mux channel left selected on both boards puts two VEML6040s
// — same address, same Wire bus — on the bus together for whoever reads next.
static void sensorsLeave() {
    colorSensor1.liveEnd();
    colorSensor2.liveEnd();
    senHeldBoard = -1;
}

// One live read of a sensor in the 0..17 space.
//
// The release is the part that is not optional. All four color muxes sit on one
// Wire bus and every VEML6040 answers at the same address, so a channel held on
// board 1 while board 2 selects one puts two sensors on the bus at once. A
// ColorSensor cannot see its neighbour, so the page holding both is the only
// thing that can hand one back — and with liveRelease(), not liveEnd(), so the
// lamp stays warm and the trip back costs one integration window instead of
// two.
static int senLiveRead(int idx, int rgbw[4], ColorReading* out) {
    const int board = idx / 9;
    if (senHeldBoard != board) {
        if (senHeldBoard == 0)      colorSensor1.liveRelease();
        else if (senHeldBoard == 1) colorSensor2.liveRelease();
        senHeldBoard = (int8_t)board;
    }
    ColorSensor& s = (board == 0) ? colorSensor1 : colorSensor2;
    return s.liveRead(idx % 9, rgbw, out);
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

// One reading as a grid cell. The two sentinels exist because "we have not
// asked this sensor yet" and "it answered nonsense" are opposite conclusions
// that used to share one hollow box.
static int8_t senCellFor(int rc, const ColorReading& r) {
    // Negative is a fault, never data — the rule liveRead() states in its
    // header, and the reason it returns a code instead of a row of zeros.
    if (rc != 0) return CubeDisplay::kCellFault;
    // Read cleanly, matched nothing it could vouch for. Board 2 sensor 2 sits
    // here on the real machine — the dead green channel (README) — which is
    // this screen doing its job, not a bug in it.
    if (!r.ok)   return CubeDisplay::kCellFault;
    // 'E' is the empty-chamber reference: a real answer whose honest picture is
    // a cell with no color in it. chipIndexForColor() already returns -1 for
    // it, which is kCellUnread — so an empty machine reads as a grid of empty
    // cells, which is what an empty machine looks like. The two meanings of
    // hollow only overlap for the ~3 s the opening sweep takes, and the
    // drill-down names which one it is in words.
    return CubeSystem::chipIndexForColor(r.color);
}

// The grid, from the classification cache. Two captioned 3x3s — the shape of a
// color board — rather than eighteen boxes in a line: "that one, bottom right
// of board 2" is a place, and a strip of eighteen has to be counted along.
//
// Board health lives in the captions, not in status rows: setOpGrid() owns
// the whole body, and a caption sits directly above the nine cells it
// describes anyway.
static void sensorsPaintGrid() {
    cubeDisplay.setOpGrid(senCapA, senFill[0], senCapB, senFill[1], senSel);
}

// The grid screen, rebuilt whole. Entry and cursor moves only — those are
// keypresses, not ticks, so the Serial-flood argument that bans
// showOperation() from the animated states does not apply. The per-read
// updates go through sensorsPaintGrid() alone.
static void drawSensors() {
    char hint[40];
    snprintf(hint, sizeof(hint), "B%d S%d - SELECT for the numbers",
             (senSel < 9) ? 1 : 2, (senSel % 9) + 1);
    opScreen(Op::Scan, "Color Sensors", nullptr, hint);
    sensorsPaintGrid();
    Cube.displayUpdate();
}

// Which sensor this tick reads.
//
// The cursor's — unless some sensor has never been asked, in which case that
// one, so the grid fills itself in about three seconds instead of opening on
// seventeen empty cells. The cursor still comes first while IT is the unasked
// one, so a move always shows its own answer before the sweep gets another
// turn.
//
// Once every cell has been filled the sweep stops for good and the page reads
// nothing but the cursor. That is what keeps a parked cursor free: a background
// sweep would steal the held channel every tick and charge an integration
// window to get it back. It also means the WHEEL is the re-sweep — walking it
// round the grid refreshes every cell it lands on, one window each, which is
// the same price the sweep pays, under the operator's hand instead of a
// timer's.
static int senReadTarget() {
    if (!(senSeen & (1UL << senSel))) return senSel;
    if (senSeen == kSenAllSeen)       return senSel;
    for (int k = 1; k <= 18; ++k) {
        const int i = (senNext + k) % 18;
        if (!(senSeen & (1UL << i))) { senNext = (uint8_t)i; return i; }
    }
    return senSel;      // unreachable: senSeen is not full and all 18 were tried
}

// One live read into the grid. A fault is a CELL, not an early return: "this
// sensor cannot be read" is one of the answers the page exists to give.
static void sensorsTick() {
    const int idx = senReadTarget();
    int rgbw[4];
    ColorReading r = { 'U', 'U', 0.0f, 0.0f, 0.0f, false };
    const int rc = senLiveRead(idx, rgbw, &r);

    if (rc == -2) return;   // the settle was cut short by the abort chord, so
                            // nothing was read and nothing may be filed —
                            // liveRead() closed its own session on the way out
                            // and the state exits on the next pass.

    senFill[idx / 9][idx % 9] = senCellFor(rc, r);
    senSeen |= (1UL << idx);
    sensorsPaintGrid();
    Cube.displayUpdate();
}

// The drill-down's frame. Rebuilt on entry and on every cursor move, because
// the title is what names the sensor being watched.
static void drawSensorRaw() {
    char title[32];
    snprintf(title, sizeof(title), "Board %d  Sensor %d",
             (senSel < 9) ? 1 : 2, (senSel % 9) + 1);
    opScreen(Op::Scan, title, nullptr, "wheel picks - LEFT goes back");
    Cube.displayUpdate();
}

// One sensor, in the numbers behind the color. This is the screen for "why did
// it call that sticker orange" — the classification is a judgement made from
// four values and a separation figure, and until you can see them the answer is
// a guess.
//
// It still earns the second level now that the grid is cheap, for a reason
// about the panel rather than the bus: setOpGrid() owns the whole body of the
// grid screen, deliberately, and there is nowhere on it for six rows of numbers
// to go.
static void sensorRawTick() {
    const int board = senSel / 9;
    const int idx   = senSel % 9;
    int rgbw[4];
    ColorReading r = { 'U', 'U', 0.0f, 0.0f, 0.0f, false };
    const int rc = senLiveRead(senSel, rgbw, &r);
    if (rc == -2) return;           // same abort bail as the grid page

    static char rows[6][40];
    const char* lines[6];
    CubeDisplay::RowMark marks[6];
    static const char* const kChan[4] = { "Red", "Green", "Blue", "White" };

    for (int k = 0; k < 4; ++k) {
        // A failed transaction hands back zeros, and four zeros are a reading —
        // one that classifies as whichever reference is darkest. So a fault
        // prints as a dash and never as a number.
        if (rc == 0) snprintf(rows[k], sizeof(rows[k]), "%s\t%d", kChan[k], rgbw[k]);
        else         snprintf(rows[k], sizeof(rows[k]), "%s\t-", kChan[k]);
        lines[k] = rows[k];
        marks[k] = (rc == 0) ? CubeDisplay::RowMark::Plain
                             : CubeDisplay::RowMark::Bad;
    }

    if (rc == 0) {
        // The letter is the nearest match even when the classifier would not
        // act on it — this is the screen for seeing why. The MARK carries the
        // verdict: Good only when classify() vouches for the reading.
        snprintf(rows[4], sizeof(rows[4]), "Reads as\t%s", sensorColorName(r.color));
        marks[4] = r.ok ? CubeDisplay::RowMark::Good : CubeDisplay::RowMark::Bad;
        // How decisively the winner beat the runner-up, scaled by this sensor's
        // own separation so the number means the same thing on every sensor. It
        // is what says whether a right answer was a confident one or a lucky
        // one, which four raw channels cannot.
        snprintf(rows[5], sizeof(rows[5]), "Confidence\t%d%%  vs %c",
                 (int)(r.confidence * 100.0f + 0.5f), r.alt);
        marks[5] = r.ok ? CubeDisplay::RowMark::Plain : CubeDisplay::RowMark::Bad;
    } else {
        snprintf(rows[4], sizeof(rows[4]), "Reads as\tI2C error %d", rc);
        snprintf(rows[5], sizeof(rows[5]), "Confidence\t-");
        marks[4] = marks[5] = CubeDisplay::RowMark::Bad;
    }
    lines[4] = rows[4];
    lines[5] = rows[5];

    cubeDisplay.setOpLines(lines, 6, marks);
    Cube.displayUpdate();

    // The grid repaints from senFill when LEFT backs out; this sensor was just
    // read, so keep its cell current too.
    senFill[board][idx] = senCellFor(rc, r);
    senSeen |= (1UL << senSel);
}

static void actSensorColors() {
    // First line, like every action that runs pumped waits: liveRead() waits
    // out its integration through pumpDelay(), and a latched abort collapses
    // that wait to ~0 ms — every read would bail with -2 without reading.
    Cube.clearAbort();

    // Start from a closed session on both boards rather than from whatever the
    // last operation left selected. liveEnd() is idempotent, and this is the
    // one place that can promise the first read below starts from a known mux
    // and a known lamp.
    sensorsLeave();

    senSel  = 0;
    senNext = 0;
    senSeen = 0;
    diagLastTick = 0;
    for (int b = 0; b < 2; ++b)
        for (int i = 0; i < 9; ++i) senFill[b][i] = CubeDisplay::kCellUnread;

    // Built once — see senCapA. The health figures share boardHealthRow() with
    // the Calibration Status screen, so the two screens cannot disagree about
    // what "healthy" means.
    char b1[32], b2[32];
    boardHealthRow(colorSensor1, b1, sizeof(b1));
    boardHealthRow(colorSensor2, b2, sizeof(b2));
    snprintf(senCapA, sizeof(senCapA), "BOARD 1  %s", b1);
    snprintf(senCapB, sizeof(senCapB), "BOARD 2  %s", b2);

    state = AppState::Sensors;
    drawSensors();
}

// ---- Motor Sensors --------------------------------------------------------
//
// Seven encoders, seven numbers. Nothing here is a picture, because an angle is
// not one — what you are checking is whether a value moves when you turn a
// face, and whether any of them is reporting an I2C error instead.
//
// Two things on top of that, and they are the same thought: a diagnostic that
// can only watch is half a diagnostic. Home sends the machine to its detents,
// which is what makes the numbers mean anything; entering a face motor puts it
// on the wheel, so the number can be watched moving under your own hand.
//
// THIS PAGE WRITES NO CALIBRATION, and that is structural rather than careful.
// It reaches exactly three things that touch the motors: scanChecked()/scan(),
// which read; CubeSystem::homeMotors(), which reads the stored calibration to
// find each face's nearest detent and drives toward it; and the shared dial
// below, which has never written anything (see the essay above actCalMotors —
// SELECT there records a RAM flag about the operator's judgement, not a value).
// The one routine that writes encoder calibration is
// CubeSystem::calibrateMotorRotations(), and the only thing that calls it is
// the Motor Calibration flow's Save row, which this page does not have and
// cannot reach. A page that looked like the calibration dial and quietly did
// not save would be worse than either screen, so the difference is a missing
// route rather than a remembered restraint.
//
// Rows: the six face motors, the ring, then Home. Eight against
// CubeDisplay::kOpLines' seven, so the window scrolls by one at the bottom —
// the Fault Log's arrangement, and cheaper than dropping one of the encoders
// the page exists to show. Home is LAST because row 0 is where the cursor
// opens, and an operation that drives all six steppers should not be one press
// from arrival.
static const int kMotFaces = 6;
static const int kMotRing  = kMotFaces;        // row 6: watched, never entered
static const int kMotHome  = kMotFaces + 1;    // row 7
static const int kMotRows  = kMotFaces + 2;    // 8

static int8_t motSel = 0;

// Face names come from kJogCaps, the jog page's strip captions, for the reason
// calListRows() gives: three pages address the same six motors by the same
// index, and a name table that exists three times is a table waiting to
// disagree about which motor is which. U R F D L B is what the moves are
// called everywhere else in the machine, including on the dial this page opens.
static const char* motRowName(int row) {
    if (row < kMotFaces) return kJogCaps[row];
    return (row == kMotRing) ? "Ring" : "Home motors";
}

// The live half: seven encoder reads, and seven of the eight rows shown.
static void motorsRows() {
    // The window over the eight rows. Clamped, never wrapped — a window that
    // wrapped would put Home above U.
    const int top = (motSel >= CubeDisplay::kOpLines)
                  ? (kMotRows - CubeDisplay::kOpLines) : 0;

    static char rows[CubeDisplay::kOpLines][40];
    const char* lines[CubeDisplay::kOpLines];
    CubeDisplay::RowMark marks[CubeDisplay::kOpLines];

    for (int i = 0; i < CubeDisplay::kOpLines; ++i) {
        const int row = top + i;
        int raw = 0;
        if (row <= kMotRing) {
            // scan() returns a raw 12-bit angle, or a negative I2C error.
            // Showing the error rather than a plausible number is the point of
            // the screen — and scan() rather than scanChecked() because the
            // raw code says WHICH transaction failed, which retrying flattens
            // to -1. The dial and the alignment paths use scanChecked(); they
            // are acting on the value, and this page is reporting it.
            raw = MotorEncoders[row]->scan();
            if (raw < 0) snprintf(rows[i], sizeof(rows[i]), "%s\terr %d",
                                  motRowName(row), raw);
            else         snprintf(rows[i], sizeof(rows[i]), "%s\t%d",
                                  motRowName(row), raw);
        } else {
            // A tab with an empty value on purpose: setOpLines() puts the row
            // mark on the VALUE when there is one and on the LABEL when there
            // is not, and a row with no tab at all cannot carry the cursor.
            snprintf(rows[i], sizeof(rows[i]), "%s\t", motRowName(row));
        }
        lines[i] = rows[i];
        // The cursor is a marked row, not a bar: eight choices cannot be drawn
        // with bar art at all (it has five slots), and the jog page and the
        // calibration list next door already mark the row.
        marks[i] = (row == motSel)                    ? CubeDisplay::RowMark::Busy
                 : (row <= kMotRing && raw < 0)       ? CubeDisplay::RowMark::Bad
                                                      : CubeDisplay::RowMark::Plain;
    }

    cubeDisplay.setOpLines(lines, CubeDisplay::kOpLines, marks);
    Cube.displayUpdate();
}

// The list screen whole. Rebuilt on entry and on cursor moves only — the hint
// changes with the row, and those are keypresses rather than ticks, so the
// Serial-flood argument that keeps showOperation() out of the animated states
// does not apply. The per-tick refresh goes through motorsRows() alone.
static void drawMotors() {
    static char hint[44];
    if (motSel == kMotHome) {
        snprintf(hint, sizeof(hint), "SELECT homes every face");
    } else if (motSel == kMotRing) {
        // Say why SELECT does nothing here rather than letting it look broken:
        // cubeMotors drives the ring to NAMED states (retract / middle /
        // extend), not by steps, so there is nothing for a wheel to turn.
        snprintf(hint, sizeof(hint), "Ring: angle only, no manual step");
    } else {
        snprintf(hint, sizeof(hint), "SELECT takes the wheel for %s",
                 kJogCaps[motSel]);
    }
    opScreen(Op::Solve, "Motor Sensors", nullptr, hint);
    motorsRows();
}

// Send the machine home from the diagnostic page.
//
// It MOVES the machine, so it is treated like every other operation here: a
// screen goes up first, the abort latch is cleared before and the chord is
// honoured during — every wait inside alignMotorsInternal() is a pumpDelay(),
// so the panel stays alive and SELECT+LEFT unwinds it with ERR_ABORTED.
//
// It writes nothing; see the essay at the top of this page for why that is a
// property of what it can reach rather than of this function being careful.
static void motorsHome() {
    Cube.clearAbort();

    // Homing turns every face until its encoder finds its index, and this page
    // never asked whether the machine is holding a cube — from "Cube Ready" it
    // is, so those are real face turns nothing tells the model about. Same
    // invalidation the jog page and the dial do, before the move rather than
    // after it, for the reasons written out at jogTurn().
    if (Cube.virtualCube.isReady()) {
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
    }

    showOp(Op::Solve, "Motor Sensors", "Homing motors", "SELECT+LEFT to abort");

    const int e = Cube.homeMotors();
    if (e) {
        // The abort return is the one path out of alignMotorsInternal() that
        // does not de-energise on its way, so do it here rather than leave six
        // steppers holding current behind an error screen nobody has read yet.
        cubeMotors.disableMotors();
        // The firmware's failure idiom: fail() names the fault, logs it, and
        // SELECT acknowledges. No auto-release — nothing on this page clamped
        // anything, and unloading would drive actuators the operator did not
        // touch.
        fail("Homing failed", homeErrorText(e), e, CubeFaultLog::Jog);
        return;
    }

    diagLastTick = 0;       // read the encoders again at once: they all moved
    drawMotors();
}

static void actSensorMotors() {
    // First line, like every action that can drive the machine: a latched abort
    // would make the Home row fail before the operator touched anything.
    Cube.clearAbort();
    motSel = 0;
    diagLastTick = 0;
    state = AppState::Motors;
    drawMotors();
}

// Live readout of the wheel and every button. The one screen that shows a
// flaky encoder or a dead button directly, instead of leaving you to infer it
// from a menu that scrolls oddly.
//
// Returns true while SELECT+LEFT is held, which is this page's ONLY way out.
// The detection has to happen here rather than from a MenuEvent because
// pollEvent() deliberately swallows the chord (it is not a menu gesture), and
// because every single-button event this page could exit on is a button the
// operator came here to press. This function already samples all five levels in
// one transaction, so the chord costs nothing extra.
static bool inputReportTick() {
    if (!Cube.encoderInitialized) {
        const char* lines[] = { "Menu encoder not found on Wire1." };
        cubeDisplay.setOpLines(lines, 1);
        Cube.displayUpdate();
        // No seesaw means no chord — but also no MenuEvents at all, since
        // pollEvent() returns None without one. The page was already a dead end
        // in this case; it is not made one here.
        return false;
    }

    const uint8_t b = menuEncoder.readButtons();

    // One row per button, and the row's VALUE is the button itself: a block
    // that lights while it is held. Not a word, and not two buttons to a line
    // — a stuck button is a light that never goes out, which is easier to see
    // than a word that never changes.
    //
    // Still ONE readButtons() transaction for all five. That is what makes the
    // chord below detectable at all: five separate reads would sample the five
    // buttons at five different instants, and a genuinely held chord would look
    // intermittent.
    static const char* const kBtnRow[5] = { "SELECT", "UP", "DOWN", "LEFT", "RIGHT" };
    static const uint8_t     kBtnBit[5] = { RotaryEncoder::BTN_SELECT,
                                            RotaryEncoder::BTN_UP,
                                            RotaryEncoder::BTN_DOWN,
                                            RotaryEncoder::BTN_LEFT,
                                            RotaryEncoder::BTN_RIGHT };

    static char wheel[32];
    snprintf(wheel, sizeof(wheel), "Wheel\t%ld", (long)menuEncoder.getPosition());

    const char* lines[6];
    CubeDisplay::RowValue values[6];
    lines[0]  = wheel;
    values[0] = CubeDisplay::RowValue::Text;    // a count, not a state

    for (int i = 0; i < 5; ++i) {
        // No tab: a row drawn with a block has no text value to align against,
        // and the block stands in that column either way.
        lines[i + 1]  = kBtnRow[i];
        values[i + 1] = (b & kBtnBit[i]) ? CubeDisplay::RowValue::On
                                         : CubeDisplay::RowValue::Off;
    }

    // No marks: a pressed button is neither good nor bad, so the lit block
    // takes the cursor yellow this theme uses for "live". The chord is named
    // in the hint bar, where every other screen says what the buttons do.
    cubeDisplay.setOpLines(lines, 6, nullptr, values);
    Cube.displayUpdate();

    // Level, not edge: pressing two buttons on the same 50 ms tick is a
    // coincidence, not a gesture. Both down on any one tick is the chord.
    return (b & RotaryEncoder::BTN_SELECT) && (b & RotaryEncoder::BTN_LEFT);
}

static void actInputReport() {
    diagLastTick = 0;
    state = AppState::InputReport;
    opScreen(Op::Info, "Input Report", nullptr,
             "Hold SELECT+LEFT to leave");
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
        opScreen(Op::Info, "Fault Log", nullptr,
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

    opScreen(Op::Info, "Fault Log", nullptr, hint);
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
    // nothing. No unloadCube() here, unlike Idle: this guard runs before
    // anything on this path drives a servo, so it changes nothing about how
    // the machine is standing. That is NOT the same as "the cube is at rest in
    // the bay", which it was until the scan learned to clamp — a cube already
    // held is left held, which is a safe place to stand and is what Eject is
    // for.
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
    // No hint: solveVirtual() blocks WITHOUT pumping, so nothing is polled
    // while this screen is up. An "SELECT+LEFT to abort" line here would
    // advertise a gesture the machine cannot hear. (Solving does pump for one
    // refresh period before the search, to get this screen onto the glass —
    // too short a window to be worth advertising.)
    //
    // Painted here, FLUSHED in Solving: drawSolveNet()'s displayUpdate() only
    // marks the widgets dirty. See the pumpDelay() at the top of Solving for
    // why this screen used not to appear at all.
    drawSolveNet("Finding solution...", nullptr);
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
    // Op::Solve: the frame takes Scramble Solve's own red from s_opTheme, and
    // an Op::Error kind would lock it red on every screen this mode draws
    // later.
    showOp(Op::Solve, "Scramble Solve", "Starting", "SELECT+LEFT to abort");
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
    // Seeded from the clock — see actModeScramble().
    randomSeed(millis());
    runMode   = RunMode::IdleSolve;
    s_opTitle = "Idle";
    idleStep    = 0;
    idleMoves   = 0;
    idleLast[0] = '\0';
    // The hint is this mode's own, not the stock abort line: SELECT does
    // something quite different from going back here, and the hint bar is
    // the one place that gets said.
    showOp(Op::Solve, "Idle", "Starting",
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
    // Seeded from the clock — see actModeScramble().
    randomSeed(millis());
    runMode   = RunMode::Demo;
    s_opTitle = "Demo";
    demoRuns  = 1;
    // Blue the whole way through, from the Demo Mode row's own theme — the
    // run scrambles and solves and scrambles again, and coloring the halves
    // differently would say the machine had changed job every few seconds.
    // The hint names the graceful exit instead of the abort chord: ending the
    // demo is the expected gesture here, and the chord still works.
    showOp(Op::Solve, "Demo", "Starting", "SELECT or LEFT ends the demo");
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
    // Seeded from the clock — see actModeScramble().
    randomSeed(millis());
    runMode   = RunMode::Step;
    s_opTitle = "Step Solve";
    // Purple from here to the last move, Step Solve's own color. Nothing
    // recolors mid-run any more: the scramble half and the solve half are the
    // same place as far as an operator finding their way is concerned.
    showOp(Op::Solve, "Step Solve", "Starting", "SELECT+LEFT to abort");
    state = AppState::ModeClamp;
}

// Fold the cube into the selected pattern. One body serves every pattern row:
// the row picked tells it which, so a new pattern is a table row, not a
// function. The two entry actions exist only because the table split across
// two screens — each screen's rows index the kPattern* tables from its own
// base, and the base is the one thing the actions add.
//
// The solved-cube gate is an honest refusal, not a hidden solve. The menu's
// preview net is a promise, and folding a scrambled cube produces
// not-the-preview; quietly solving first would run an operation the user
// never asked for. Requiring Solve keeps the promise and keeps the machine
// predictable.
static void startPatternFold(int tableIdx) {
    // Read the selection FIRST, before any screen changes: the fold states
    // run long after the menu has moved on, and selectedIndex() only means
    // this row while the menu still shows it.
    patIdx = tableIdx;
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
    // The one action that overrides the color loop() captured for it. The
    // pattern ROWS are themed per pattern so adjacent preview nets do not all
    // sit in one frame, and that is a preview device, not a place — the branch
    // is Patterns, and Patterns is green.
    s_opTheme = MenuTheme::Green;
    showOp(Op::Solve, "Patterns", "Starting", "SELECT+LEFT to abort");
    cubeDisplay.setStatus(patName);
    Cube.displayUpdate();
    state = AppState::ModeClamp;
}

static void actPattern()     { startPatternFold(Menu.selectedIndex()); }
static void actPatternMore() { startPatternFold(kPatMoreBase + Menu.selectedIndex()); }

// ---------------------------------------------------------------------------
//  Motor Calibration — square the faces by hand, then sweep
// ---------------------------------------------------------------------------
//  calibrateMotorRotations() turns every face through four quarter turns and
//  files the four encoder readings it collects as that motor's calibration.
//  It reads wherever the motors are STANDING when it starts and derives all
//  four marks from there — so a face sitting out of square when the sweep
//  begins produces four marks that are out of square by the same amount, and
//  nothing downstream can tell. Every later alignment then drives that face to
//  a "centre" that is not one.
//
//  Hence this flow rather than the single blocking call this menu item used to
//  be: confirm the machine is empty, clamp, square each face by hand, save.
//  Only the last step writes anything.
//
//  WHAT SELECT MEANS ON THE DIAL — the one ambiguity worth stating out loud.
//
//  SELECT accepts the motor's PHYSICAL alignment: it is the operator saying
//  "this face is square now", and it writes NOTHING into the encoder's
//  calibration array. The Save row's sweep is what turns six squared faces
//  into twenty-four calibration values.
//
//  The other reading — SELECT writes the current encoder value through
//  MotorEncoder::setCalibration() — was rejected because the sweep overwrites
//  all four values moments later from its own scans, which would make the dial
//  ceremonial. If the bench decides otherwise, calLeaveDial() is the one
//  function to change and this paragraph is the one to correct.
//
//  For that to hold, the motors must still be where the operator put them when
//  the sweep runs. They are: cubeMotors.resetMotorPos(), which is the first
//  thing calibrateMotorRotations() does, only zeroes the step COUNTERS —
//  AccelStepper::setCurrentPosition() plus the internal pos[] array. It
//  commands no travel, so the physical alignment survives it and the sweep's
//  opening scan reads exactly what the dial last showed.

static const int kCalMotors = 6;              // faces only. The ring has an
                                              // encoder but is not rotation-
                                              // calibrated — CubeSystem clamps
                                              // numMotors to 6 for the same
                                              // reason, and the fixed size-6
                                              // arrays there depend on it.
static const int kCalSave   = kCalMotors;     // the last row: confirm and save
static const int kCalRows   = kCalMotors + 1; // 7 — exactly CubeDisplay::kOpLines

static int8_t   calSel = 0;                   // cursor, 0..kCalSave
static bool     calAligned[kCalMotors];       // SELECT accepted this face's pose
static int      calDelta = 0;                 // steps jogged on the open dial
static uint32_t calTick  = 0;                 // throttle for the live encoder
                                              // reads on both pages

// How far one detent moves the selected face: one motor step.
//
// getTurnStep() is steps per quarter turn — 100 on this machine — and the
// AS5600 reports 4096 counts per revolution, so one step is about
// 1024/turnStep = 10 encoder counts, half the alignment tolerance the machine
// works to (kTune's "Tolerance", 20 counts).
//
// Half the tolerance, deliberately: the dial is where the operator squares a
// face BY EYE before the sweep files four marks from it, and the marks are
// only as square as that eye could get them. A step that is half the
// tolerance lets the face be centred inside the band rather than parked at
// one edge of it, which every later alignment then inherits. The price is
// that a face 45 degrees out takes 50 detents, and nobody squares a face
// from 45 degrees out.
//
// A function rather than a literal 1 so calJog() keeps its shape and the
// number has one home.
static int calStepSize() {
    return 1;
}

// The nearest of a motor's four stored marks to a raw reading, and the
// signed shortest-way error to it — the same test CubeSystem::checkAlignment()
// applies, computed with the same encError() so the two cannot disagree.
// Only meaningful when isCalibrated(); the callers check.
static int calNearestMark(int motor, int raw, int* errOut) {
    int best = 0, bestErr = 4096;
    for (int j = 0; j < 4; ++j) {
        const int e = CubeSystem::encError(raw, MotorEncoders[motor]->getCalibration(j));
        if (abs(e) < abs(bestErr)) { bestErr = e; best = j; }
    }
    if (errOut) *errOut = bestErr;
    return best;
}

// The six motors and the Save row. Seven rows, no headline and no sub-line:
// seven is exactly CubeDisplay::kOpLines and they only fit when they start at
// the top of the body, which is the Motor Sensors page's shape.
//
// Rows, not bars, even though every one of them IS selectable. Bar art is the
// menu's vocabulary for a choice and it has five slots; seven choices cannot
// be drawn with it at all. So the cursor is a marked row, exactly as on the
// jog page next door — the same interaction deserves the same look.
//
// Motor names come from kJogCaps, the jog page's strip captions, rather than a
// second hand-written U R F D L B: the two pages address the same six motors
// by the same index, and a name table that exists twice is a table waiting to
// disagree about which motor is which.
static void calListRows() {
    static char rows[kCalRows][40];
    const char* lines[kCalRows];
    CubeDisplay::RowMark marks[kCalRows];

    int done = 0;
    for (int i = 0; i < kCalMotors; ++i) {
        // scanChecked(), and the SIGN checked: a negative return is an I2C
        // fault, not an angle. Showing it as a position is exactly what
        // MotorEncoder.h is emphatic about and what the Motor Sensors page
        // already refuses to do.
        const int raw = MotorEncoders[i]->scanChecked();
        if (calAligned[i]) done++;

        // Beside the raw angle, where it stands against the STORED
        // calibration: the nearest of the four marks and the signed error to
        // it, in counts — the exact number the aligner works from. A face
        // that reads "m2 -118" is 118 counts (~10 degrees) off its mark;
        // one that reads "m2 +3" is on it. Blank until a calibration exists.
        // 16 bytes and a bounded error: the nearest of four marks is never
        // more than 512 counts away, but GCC only knows it is an int, so the
        // clamp is what buys the clean build.
        char near[16] = "";
        if (raw >= 0 && MotorEncoders[i]->isCalibrated()) {
            int e; const int k = calNearestMark(i, raw, &e);
            snprintf(near, sizeof(near), "  m%d %+d", k & 3, (int)constrain(e, -999, 999));
        }

        if (raw < 0) {
            snprintf(rows[i], sizeof(rows[i]), "%s\terr %d", kJogCaps[i], raw);
        } else if (calAligned[i]) {
            // The word as well as the mark, because on the highlighted row the
            // mark IS the cursor and cannot also report state.
            snprintf(rows[i], sizeof(rows[i]), "%s\tsquared %d%s", kJogCaps[i], raw, near);
        } else {
            snprintf(rows[i], sizeof(rows[i]), "%s\t%d%s", kJogCaps[i], raw, near);
        }
        lines[i] = rows[i];
        marks[i] = (i == calSel) ? CubeDisplay::RowMark::Busy
                 : (raw < 0)     ? CubeDisplay::RowMark::Bad
                 : calAligned[i] ? CubeDisplay::RowMark::Good
                                 : CubeDisplay::RowMark::Plain;
    }

    // Clamped so the width is PROVABLE, not merely true: done cannot leave
    // 0..kCalMotors, but GCC only knows it is an int, budgets 11 digits for
    // it, and warns the row could overflow. One comparison buys a clean build.
    const int doneShown = (done < 0) ? 0 : (done > kCalMotors ? kCalMotors : done);
    snprintf(rows[kCalSave], sizeof(rows[kCalSave]),
             "Save calibration\t%d of %d squared", doneShown, (int)kCalMotors);
    lines[kCalSave] = rows[kCalSave];
    marks[kCalSave] = (calSel == kCalSave) ? CubeDisplay::RowMark::Busy
                                           : CubeDisplay::RowMark::Plain;

    cubeDisplay.setOpLines(lines, kCalRows, marks);
    Cube.displayUpdate();
}

// The list screen whole. Rebuilt on entry and on cursor moves only — those are
// keypresses, not ticks, so the Serial-flood argument that keeps
// showOperation() out of the animated states does not apply. The per-tick
// refresh goes through calListRows() alone.
static void drawCalList() {
    char hint[40];
    if (calSel == kCalSave) {
        snprintf(hint, sizeof(hint), "SELECT runs the calibration");
    } else {
        snprintf(hint, sizeof(hint), "SELECT jogs motor %s", kJogCaps[calSel]);
    }
    // Yellow the whole way through, like every screen under Settings — the
    // frame says where you are, and this flow never leaves Calibration.
    opScreen(Op::Calibrate, "Motor Calibration", nullptr, hint);
    calListRows();
}

// The dial page's frame, painted once on entry. calDialTick() updates the
// value in place afterwards — showOperation() rebuilds the screen and reprints
// the title to Serial, and twenty times a second that is the flood the
// animated states already learned to avoid.
static void drawCalDialFrame() {
    // The title and the hint are all that change with the owner — and, since
    // full moves arrived, one gesture: the diagnostic's UP/DOWN run real
    // quarter turns (see dialTurn()), while the CalFlow's stay single detents
    // for squaring by hand. The layout, the frame color and everything else
    // are deliberately identical: this is one screen opened from two places,
    // not two screens that resemble each other. Both pages live under
    // Settings, so both frames are yellow anyway.
    const bool diag = (dialOwner == DialOwner::Diagnostic);
    static char hint[40];
    if (diag) {
        // Both moves spelled out rather than "UP/DOWN turns": the point of
        // the buttons is testing one direction against the other, and the
        // hint is where the operator checks which press is which.
        snprintf(hint, sizeof(hint), "wheel steps - UP %s  DOWN %s'",
                 kJogCaps[calSel], kJogCaps[calSel]);
    } else {
        snprintf(hint, sizeof(hint), "wheel steps - SELECT accepts");
    }
    opScreen(Op::Calibrate, diag ? "Motor Sensors" : "Motor Calibration", nullptr,
             hint);

    // "You have taken this row and the next detent goes to the machine." A
    // badge rather than a color: every screen that can arm anything lives
    // under Settings, whose frames are all yellow, so a frame color would
    // signal nothing. Re-armed after every
    // opScreen() because showOperation() clears it, which is what stops a
    // screen inheriting somebody else's badge.
    cubeDisplay.setOpArmed("ARMED");
    Cube.displayUpdate();
}

// The live half: one encoder read, then the dial.
static void calDialTick() {
    const int raw = MotorEncoders[calSel]->scanChecked();

    char caption[16], centre[12], sub[40], head[32];
    snprintf(caption, sizeof(caption), "Motor %s", kJogCaps[calSel]);

    if (raw < 0) {
        // A negative return is a sensor fault, NOT a position. Put it on the
        // arc and the needle lands somewhere plausible and confident, which is
        // worse than no dial at all — so the dial goes away and the fault
        // takes the headline. The motor's name has to move to the sub-line
        // with it: the dial's caption was carrying it, and the dial is gone.
        // scanChecked() collapses every bus failure to -1 (MotorEncoder.cpp
        // says so), so this is one digit in practice — but it is an int, and
        // caption has already taken up to 24 of these 32 bytes.
        const int errShown = (raw < -99) ? -99 : raw;
        snprintf(sub, sizeof(sub), "%s   err %d", caption, errShown);
        cubeDisplay.setStatus(sub);
        cubeDisplay.setMessage("Encoder unreadable");
        cubeDisplay.setOpDial(0, 0, 0, nullptr, nullptr);
    } else {
        // How far the wheel has moved this face since the page opened. The
        // dial shows the ABSOLUTE angle, which says nothing about how far you
        // have come — and "am I nudging it or have I been round" is the one
        // question a jog page has to answer that its own readout cannot.
        //
        // The headline carries that count and, once a calibration exists,
        // the nearest stored mark with the signed error to it — so squaring
        // a face by eye can be checked against what the machine will home
        // to. The sub-line lists the four stored marks themselves: they are
        // what the sweep at the end replaces, and seeing them is the only
        // way to know what "home" currently means for this motor.
        if (MotorEncoders[calSel]->isCalibrated()) {
            int e; const int k = calNearestMark(calSel, raw, &e);
            snprintf(head, sizeof(head), "%+d steps   m%d %+d", calDelta, k, e);
            snprintf(sub, sizeof(sub), "marks %d %d %d %d",
                     MotorEncoders[calSel]->getCalibration(0),
                     MotorEncoders[calSel]->getCalibration(1),
                     MotorEncoders[calSel]->getCalibration(2),
                     MotorEncoders[calSel]->getCalibration(3));
        } else {
            snprintf(head, sizeof(head), "%+d steps", calDelta);
            snprintf(sub, sizeof(sub), "no stored marks yet");
        }
        cubeDisplay.setStatus(sub);
        snprintf(centre, sizeof(centre), "%d", raw);
        cubeDisplay.setMessage(head);
        // 0-4095 is the AS5600's whole revolution, which is the range this
        // number genuinely lives in. A dial scaled to anything narrower would
        // be a claim about where square is, and finding that out is what the
        // sweep at the end is for.
        cubeDisplay.setOpDial(raw, 0, 4095, centre, caption);
    }
    Cube.displayUpdate();
}

// Take the wheel for motor calSel, on behalf of whichever page asked.
//
// WHAT THE OWNER DOES AND DOES NOT CHANGE. It picks the title, the row list to
// go back to, and whether the abort path releases the grippers — three facts
// about the PAGE. It does not gate a write, because there is no write here to
// gate: the dial has never recorded a calibration value (the essay above
// actCalMotors() is the argument), and the only thing it does record is
// calAligned[], a RAM flag of the calibration flow's own. The diagnostic page
// cannot reach even that, because on it SELECT is a detent rather than an
// acceptance — see calDialLoop(). So "the diagnostic writes nothing" is a
// route that does not exist rather than a check somebody has to keep making.
static void calEnterDial(DialOwner owner) {
    dialOwner = owner;
    calDelta = 0;

    // A jogged face is untracked, so a model that IS ready stops describing
    // this cube the moment a detent lands — the jog page's problem exactly,
    // and jogTurn() spells the reasoning out. It arrives here because the
    // diagnostic route asks nothing on the way in: Settings > Diagnostics >
    // Sensor Test > Motor Sensors offers a face row with no prompt, no cube
    // test and no isReady() test, and from "Cube Ready" the machine reaches it
    // still clamped. Past a net ~50 detents (45 degrees) the next alignment
    // pass snaps the face onward and completes the quarter turn, and a stale
    // model would let a later Solve replay a solution against a cube it no
    // longer matches — with SolveConfirm drawing the pre-jog net, which still
    // looks right.
    //
    // Wiped on ENTRY rather than at the first jog, and that costs something: a
    // visit that touches nothing also drops the main menu back to its pre-scan
    // form. Accepted, because there is no press to hang it on that is not
    // already a turn — SELECT is a detent on this page, and so is every wheel
    // click — and because calJog() is shared with the calibration flow, where
    // the wipe would be wrong.
    //
    // NOT for CalFlow: motor calibration runs on an empty centre by design
    // (actCalMotors() makes the operator confirm it), so its turns have no
    // cube to desync, and wiping there would be a different change.
    if (owner == DialOwner::Diagnostic && Cube.virtualCube.isReady()) {
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
    }

    // Energised for the whole visit, not per detent. A de-energised stepper is
    // held only by its detent torque, and the entire premise of the
    // calibration flow is that the face STAYS where the operator put it until
    // the sweep reads it. alignMotorsInternal() enables once for the same
    // reason.
    cubeMotors.enableMotors();
    calTick = 0;                    // read the encoder on the very next pass
    state = AppState::MotorDial;
    drawCalDialFrame();
}

// Leave the dial, accepted or not, and go back to the list that opened it.
static void calLeaveDial(bool accepted) {
    cubeMotors.disableMotors();
    if (accepted && dialOwner == DialOwner::CalFlow) {
        // Interpretation (a): this records that the FACE is square, and
        // nothing else. No setCalibration() call — see the essay above.
        //
        // The owner test is belt and braces: no diagnostic path passes true.
        // It lives here rather than at the call site so that a future one
        // cannot reintroduce the record by passing it.
        calAligned[calSel] = true;
    }
    calTick = 0;
    if (dialOwner == DialOwner::Diagnostic) {
        state = AppState::Motors;
        drawMotors();
    } else {
        state = AppState::CalMotorsPick;
        drawCalList();
    }
}

// Move the selected face by one detent's worth of steps.
//
// moveTo() takes all six targets at once because MultiStepper has no per-axis
// call, so the other five are handed back their current positions and travel
// nowhere.
static void calJog(int detents) {
    long pos[6];
    for (int i = 0; i < kCalMotors; ++i) pos[i] = cubeMotors.getPos(i);
    const int d = detents * calStepSize();
    pos[calSel] += d;
    cubeMotors.moveTo(pos);         // blocking, but pumped inside
    calDelta += d;
}

// One real quarter turn of the entered motor, from the DIAGNOSTIC dial: +1 is
// the plain move, -1 the prime — U / U' with the U motor entered. Built for
// watching one encoder across repeated full moves in EACH direction, with the
// dial's angle and nearest-mark error on screen the whole time — the shape of
// a direction-dependent fault, which single detents cannot reproduce.
//
// Diagnostic owner ONLY, and deliberately not offered on the Motor Sensors
// LIST: there UP and DOWN are the cursor, and a button that turns a motor on
// a page you are navigating fires on whatever row it lands on. Entering the
// motor first is what says "this one, on purpose". Not in the CalFlow either:
// its UP/DOWN stay single detents for squaring by hand, and an aligned move
// there would home a hand-squared face back to the OLD marks — the exact
// values the flow exists to replace.
static void dialTurn(int dir) {
    const char* mv = CubeSystem::kFaceMoves[calSel][dir > 0 ? 0 : 1];

    // calEnterDial(Diagnostic) already wiped the model on the way in, but a
    // wipe here is cheap insurance against a future entry path that forgets —
    // the desync argument is jogTurn()'s, and it does not care which page
    // turned the face.
    if (Cube.virtualCube.isReady()) {
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
    }

    // The move's name in the headline while it runs: with alignment on, a
    // turn can take over a second, and a frozen dial says nothing about the
    // machine having heard the press. calDialTick() repaints over it.
    cubeDisplay.setMessage(mv);
    Cube.displayUpdate();

    // align = true, like jogTurn(): a full move here exists to watch whether
    // the motor lands on its mark and whether the aligner corrects it — the
    // exact behaviour a solve move has — so let the pass run and report if
    // it cannot. fail() leaves the dial for the red screen; the caller must
    // notice the state change.
    const int e = Cube.executeMove(mv, false, true);
    if (e) {
        // No auto-release — direct manipulation means an operator standing
        // at the machine, and unloading would destroy the state being
        // diagnosed.
        fail("Move failed", moveErrorText(e), e, CubeFaultLog::Jog);
        return;
    }

    // The aligned move ends ON a mark (that is what align = true promises on
    // success), so the "+N steps" travel count restarts from a fresh
    // reference rather than claiming a displacement the aligner just
    // absorbed.
    calDelta = 0;
}

static void actCalMotors() {
    Cube.clearAbort();
    for (int i = 0; i < kCalMotors; ++i) calAligned[i] = false;
    calSel = 0;

    // The same shape as actCalColors' prompt, and for the same reason: this is
    // the other calibration that cannot be started blind. There it is the
    // cube's ORIENTATION that nothing downstream can check; here it is the
    // cube's ABSENCE — the grippers close on the centre and then every face
    // turns four times, and a cube left in the machine is crushed or thrown.
    opScreen(Op::Calibrate, "Motor Calibration",
             "Is the machine empty?",
             "SELECT to start, LEFT to cancel");
    const char* rows[] = {
        "The grippers will close on an",
        "empty centre and turn every face.",
        "",
        "Take the cube OUT before starting.",
    };
    cubeDisplay.setOpLines(rows, 4);
    Cube.displayUpdate();
    state = AppState::CalMotorsPrompt;
}

// Color calibration cannot be started blind.
//
// calibrateColorSensors() does not identify what it is looking at — it assumes
// the cube is loaded a particular way and files whatever the sensors return
// under the color it expects. Wrong orientation means a wrong calibration
// written to EEPROM with nothing to catch it, which then misreads every scan
// afterwards. So the machine shows the required orientation and waits.
//
// RELEASE FIRST, and that ordering is the whole point of this function.
//
// calibrateColorSensors() also releases, and that is not enough: it does so
// AFTER this prompt has been answered. The prompt says "Load a SOLVED cube
// exactly like this" — an instruction the operator cannot carry out while the
// machine is holding a cube he can neither reach nor turn, which is the normal
// rest state now that a scan and a solve both end clamped. With nothing else to
// press he presses SELECT, and calibration then files the faces of whatever is
// already in there, in whatever orientation it is in, as the six colour
// references. After a scan that is usually a SCRAMBLED cube, and the resulting
// table looks perfectly plausible — no empty-chamber degeneracy to give it
// away, and saveCalibration()'s verify only checks that EEPROM matches RAM.
//
// So the release belongs here, before the instruction is given, not there.
static void actCalColors() {
    Cube.clearAbort();
    Cube.unloadCube();
    opScreen(Op::Calibrate, "Color Calibration", nullptr,
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
// exactly enough; resist a seventh.
static void drawStats(const char* status) {
    static char rows[6][48];
    char t[16];

    // One tally: machine-paced and step solves together. The split still
    // exists in EEPROM — Step Solve is untimed and must stay out of the
    // numbers Best and Average are built from — but two numbers on the row
    // was bookkeeping shown to the operator.
    snprintf(rows[0], sizeof(rows[0]), "Solves\t%lu",
             (unsigned long)(statsVals[CubeStats::Solves]
                           + statsVals[CubeStats::UntimedSolves]));

    // Best carries the length of the solve that set it. A block whose BestMs
    // predates the BestMoves field reads 0 there — show the bare time rather
    // than inventing "(0 moves)".
    fmtSolveTime(statsVals[CubeStats::BestMs], t, sizeof(t));
    if (statsVals[CubeStats::BestMoves] > 0) {
        snprintf(rows[1], sizeof(rows[1]), "Best\t%s (%lu moves)", t,
                 (unsigned long)statsVals[CubeStats::BestMoves]);
    } else {
        snprintf(rows[1], sizeof(rows[1]), "Best\t%s", t);
    }

    // Average over the qualifying solves only — see kStatMinMoves. Division
    // guarded: zero qualifying solves passes 0 on, which comes back from
    // fmtSolveTime as the same "-" the other empty rows show.
    const uint32_t qn = statsVals[CubeStats::QualSolves];
    fmtSolveTime(qn ? statsVals[CubeStats::QualMs] / qn : 0, t, sizeof(t));
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
    opScreen(Op::Info, "Stats", nullptr,
             "LEFT back - hold SEL+RIGHT resets");
    cubeDisplay.setStatus(status);
    cubeDisplay.setOpLines(lines, 6);
    Cube.displayUpdate();
}

// The reset gesture's bookkeeping. holdStart is 0 while the chord is not
// down; swallowPress outlives the chord so letting go of it — fired or
// abandoned — cannot read as "SELECT: leave".
static const uint32_t kStatResetHoldMs = 5000;
static uint32_t statHoldStart    = 0;
static bool     statFired        = false;
static bool     statSwallowPress = false;

static void actStats() {
    statHoldStart    = 0;
    statFired        = false;
    statSwallowPress = false;
    state = AppState::Stats;
    drawStats("Since first use");
}

// The Stats page's per-pass handler, fifth of the input-owning kind. Shares
// the edge-detection state with the other pollers under the same
// one-poller-per-pass rule. What it exists for: the reset chord, SELECT+RIGHT
// held for five seconds — pollEvent() folds RIGHT into Select and fires on
// the press edge, so under it the page would exit the instant the chord began
// to form. Exits are on the RELEASE here for the same reason: only when a
// press ends without ever having been the chord is it known to be "leave".
static void statsLoop() {
    if (!Cube.encoderInitialized) return;   // no buttons; the page still shows

    const uint32_t now = millis();
    if (now - lastPoll < 25) return;
    lastPoll = now;

    const uint8_t b = menuEncoder.readButtons();
    const bool selDown   = b & RotaryEncoder::BTN_SELECT;
    const bool leftDown  = b & RotaryEncoder::BTN_LEFT;
    const bool rightDown = b & RotaryEncoder::BTN_RIGHT;

    const bool leftEdge     = leftDown   && !prevLeft;
    const bool selRelease   = !selDown   && prevSelect;
    const bool rightRelease = !rightDown && prevRight;

    prevSelect = selDown;
    prevLeft   = leftDown;
    prevRight  = rightDown;
    prevUp     = b & RotaryEncoder::BTN_UP;
    prevDown   = b & RotaryEncoder::BTN_DOWN;

    // Consumed unconditionally, exactly as pollEvent() does: the wheel means
    // nothing here, and rotation banked during the visit would replay as a
    // phantom scroll on the first menu pass after leaving.
    prevPos = menuEncoder.getPosition();

    if (leftEdge) { toMenu(); return; }

    if (selDown && rightDown) {
        // The chord. From here on this press means "reset", never "leave",
        // however the two buttons come back up.
        statSwallowPress = true;
        if (statHoldStart == 0) statHoldStart = now;
        if (!statFired) {
            const uint32_t held = now - statHoldStart;
            if (held >= kStatResetHoldMs) {
                statFired = true;
                // Everything goes — solves, times, faults, the run clock.
                // clear() drops only the block's magic, so a wipe done by
                // accident is recoverable with a programmer.
                for (uint8_t f = 0; f < CubeStats::FieldCount; ++f) {
                    statsVals[f] = 0;
                }
                runLastMs = now;    // the run clock restarts at the wipe
                cubeStats.clear();
                drawStats("Stats cleared");
            } else {
                // Count the hold down on the status line: a five-second hold
                // with no feedback is indistinguishable from a dead button.
                char s[24];
                snprintf(s, sizeof(s), "Resetting in %lu",
                         (unsigned long)((kStatResetHoldMs - held + 999) / 1000));
                cubeDisplay.setStatus(s);
                Cube.displayUpdate();
            }
        }
        return;
    }

    // The chord is no longer down. Put the status line back if it was
    // counting, and re-arm for a fresh hold.
    if (statHoldStart != 0) {
        statHoldStart = 0;
        if (!statFired) {
            cubeDisplay.setStatus("Since first use");
            Cube.displayUpdate();
        }
        statFired = false;
    }

    if (selRelease || rightRelease) {
        if (statSwallowPress) {
            // A chord released one finger at a time must not leave on the
            // second release — only once both are up is the press over.
            if (!selDown && !rightDown) statSwallowPress = false;
        } else {
            // A plain SELECT (or RIGHT, which pollEvent() treats as SELECT
            // everywhere else) tap: leave, as the Info screens do.
            toMenu();
        }
    }
}

// ---------------------------------------------------------------------------
//  Cube State — the stored model, unfolded, and a way to turn it
// ---------------------------------------------------------------------------
//
//  The screen that answers "does the machine think it is holding the cube I am
//  holding", which before it existed could only be checked by reading a
//  54-character dump over Serial. It now also answers "what would this move do
//  to it".
//
//  NOTHING PHYSICAL MOVES HERE. Not a motor, not a servo. That was a choice
//  between two, so it is worth recording which one was turned down.
//
//  The rejected one was to turn the real face and track it in the model, the
//  way executeMove(moveVirtual=true) does during a solve. It loses on what
//  this page IS: a Diagnostics readout, reachable at any time, whose job is to
//  report the model. A page that drives six steppers to do that is a different
//  page, and it already exists — Hardware Test, whose jogTurn() carries the
//  essay on why ITS turns wipe the model. The two are mirror images: the jog
//  page moves the machine and therefore cannot keep the model, and this page
//  keeps the model and therefore must not move the machine. Same desync,
//  opposite ends of it.
//
//  Which leaves the trap a model viewer has all of its own, and it is that
//  same desync wearing the other hat. A model left turned no longer describes
//  the cube in the bay either, so a later Solve would compute against a state
//  the machine is not holding and then execute it. The page therefore does not
//  leave the model turned: every move is recorded, and every exit replays the
//  trail backwards with each move inverted, so the model comes back exactly as
//  it was found. A scan survives a visit here, and nothing downstream can be
//  poisoned by one.
static const int kCsFaces = 6;    // U R F D L B — kJogCaps names them, and
                                  // CubeSystem::kFaceMoves is in that order
static int8_t    csSel    = 0;

// The undo trail: one byte per move, face * 2 plus a bit for the prime.
//
// Bounded because it has to live somewhere, and sixty quarter turns is far
// past what anyone does to a diagnostic. It is hard to reach at all because a
// turn that undoes the previous one POPS rather than pushing — which is
// exactly the forward-and-back gesture this page was asked for, so the usual
// way of using it costs no depth whatever. Full, the page refuses the turn and
// says so, rather than accepting one it could not take back.
static const int kCsHistMax = 60;
static uint8_t   csHist[kCsHistMax];
static int8_t    csHistN = 0;

// Nine of each color is the cheapest check that a stored state is a cube at
// all, and the one an operator can act on: a count that is not nine says which
// color was misread, which is more use than "invalid". Shared by both forms of
// this page so they cannot disagree about what they are counting.
static void csCountRow(const char* fac, char* out, size_t n) {
    static const char kOrder[6] = { 'W', 'Y', 'R', 'O', 'G', 'B' };
    int count[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < CubeDisplay::kNetFacelets; ++i) {
        for (int c = 0; c < 6; ++c) if (fac[i] == kOrder[c]) count[c]++;
    }
    snprintf(out, n, "W%d  Y%d  R%d  O%d  G%d  B%d",
             count[0], count[1], count[2], count[3], count[4], count[5]);
}

// Turn the selected face on the MODEL. False if the trail is full or the model
// refused the move.
static bool csTurn(int dir) {
    const int     col   = (dir > 0) ? 0 : 1;   // kFaceMoves: plain, then prime
    const uint8_t entry = (uint8_t)(csSel * 2 + col);

    // The inverse of the last move cancels it instead of extending the trail.
    // Flipping bit 0 swaps plain for prime, which is what makes that one test.
    const bool undoes = (csHistN > 0) &&
                        (csHist[csHistN - 1] == (uint8_t)(entry ^ 1));
    if (!undoes && csHistN >= kCsHistMax) return false;

    // The machine's own move engine, not a facelet permutation spelled out
    // here: a second copy of the cube's mechanics in the sketch is a copy that
    // can disagree with the one the solver runs on.
    if (Cube.virtualCube.executeMove(CubeSystem::kFaceMoves[csSel][col]) != 0)
        return false;

    if (undoes) --csHistN;
    else        csHist[csHistN++] = entry;

    // executeMove() mutates cubeArray only; the color array this screen draws
    // from is derived, and stale until this runs.
    Cube.virtualCube.rebuildFromCubeArray();
    return true;
}

// Put the model back exactly as it was found, by replaying the trail backwards
// with every move inverted. Every exit goes through it, including the one the
// operator does not think of as an exit.
static void csRestore() {
    while (csHistN > 0) {
        const uint8_t e = csHist[--csHistN];
        Cube.virtualCube.executeMove(CubeSystem::kFaceMoves[e >> 1][(e & 1) ? 0 : 1]);
    }
    Cube.virtualCube.rebuildFromCubeArray();
}

static void drawCubeState() {
    const char* fac = Cube.virtualCube.getColorArray();

    // Two lines under the net, and they are nearly all the text this screen
    // has: the net owns y 60..150 and an operation screen's headline slot at
    // y 58 draws straight through the U face. lbl_status wraps and grows
    // downward from y 156, so a newline buys the second line and still lands
    // clear of the hint box at 199.
    //
    // The counts stay, even though a built model is a permutation and can only
    // ever be nine of each: rebuildFromCubeArray() is the call its own header
    // calls unfinished, and this is the line that would show it up if it ever
    // handed back something that was not a cube.
    //
    // The second line is the one that has to be unmistakable. Angle brackets
    // are this machine's idiom for "the wheel changes this" — the jog page and
    // the value editor both — and MODEL ONLY is in capitals because the whole
    // risk of the page is an operator believing the machine just turned.
    char sub[96], counts[40];
    csCountRow(fac, counts, sizeof(counts));
    snprintf(sub, sizeof(sub), "%s\n< %s >  MODEL ONLY - no motor moves",
             counts, kJogCaps[csSel]);

    const char* hint = (csHistN >= kCsHistMax)
                     ? "undo full - LEFT puts it back"
                     : "UP/DOWN turns - SELECT/LEFT out";

    opScreen(Op::Info, "Cube State", nullptr, hint);
    cubeDisplay.setOpCubeNet(fac);
    cubeDisplay.setStatus(sub);
    Cube.displayUpdate();
}

static void actCubeState() {
    // With a real model there is something to turn and something to put back
    // afterwards, so this form of the page owns the input.
    if (Cube.virtualCube.isReady()) {
        // executeMove() mutates cubeArray only, so the color array this
        // screen reads goes stale after any mode move — an idle turn, a
        // scramble, a fold. Despite the UNFINISHED label on its header,
        // rebuildFromCubeArray() is the working "refresh colorCubeArray"
        // call, and it is all this screen needs.
        Cube.virtualCube.rebuildFromCubeArray();
        csSel   = 0;
        csHistN = 0;        // never carried across visits: the trail belongs to
                            // one sitting, and csRestore() emptied it on the
                            // way out of the last one
        state   = AppState::CubeState;
        drawCubeState();
        return;
    }

    if (Cube.scanFacesRecorded > 0) {
        // Nothing was built, but a scan was recorded — which is exactly when
        // somebody wants to see it. Reassemble the raw per-face readings into
        // net order using the same pass/sensor table the scan display uses.
        //
        // CAVEAT, and it is why this says "last scan" rather than "cube": each
        // face is laid out as its sensor saw it, and the per-face rotation is
        // only resolved later by setOrientation(). A stray sticker shows up
        // here, but WHERE it sits within its face may be turned.
        //
        // Read-only, and not because turning it would have been extra work:
        // there is no model here to turn. VirtualCube::executeMove() refuses a
        // cube that is not ready, and the unresolved per-face rotation means a
        // turn would shuffle stickers inside a frame that does not mean
        // anything yet.
        char net[CubeDisplay::kNetFacelets];
        for (int i = 0; i < CubeDisplay::kNetFacelets; ++i) net[i] = 'X';
        for (int pass = 0; pass < CubeSystem::kScanPasses; ++pass) {
            for (int sen = 0; sen < 2; ++sen) {
                const int f       = 2 * pass + sen;
                const int netFace = CubeSystem::kScanPassFaces[pass][sen];
                if (f >= Cube.scanFacesRecorded) continue;
                for (int k = 0; k < 9; ++k) net[netFace * 9 + k] = Cube.scanColor[f][k];
            }
        }

        char sub[96], counts[40];
        csCountRow(net, counts, sizeof(counts));
        snprintf(sub, sizeof(sub), "%s\nlast scan, not built", counts);

        opScreen(Op::Info, "Cube State", nullptr, "SELECT or LEFT to go back");
        cubeDisplay.setOpCubeNet(net);
        cubeDisplay.setStatus(sub);
        Cube.displayUpdate();
        state = AppState::Info;
        return;
    }

    const char* rows[] = {
        "Nothing has been scanned yet, or the",
        "last scan was discarded by a fault.",
    };
    showInfo("Cube State", rows, 2, "No cube state");
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
                // Clamp, not wrap: a position has ends, and running off one
                // should feel like a stop rather than teleport the horn from
                // Extend back to Retract on a single detent.
                if (t < 0)           t = 0;
                if (t > kJogPos - 1) t = kJogPos - 1;
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

    // "Saved" is a receipt for the press just made, not a state of the
    // section, so any further press retires it.
    if (step || in.up || in.down || in.select || in.back) parSaved = false;

    // The Apply row has no TuneParam behind it. Every branch that touches `p`
    // below is one this rules out — the row cannot be entered, gated or
    // edited — so binding row 0 in its place is a placeholder, never a value.
    const bool onApply = (parSel == parApplyRow());
    const TuneParam& p = kTune[parSec->first + (onApply ? 0 : parSel)];

    // The confirm owns the input while it is up. Nothing else is reachable
    // from here, so a gated row cannot be edited by any path that skips it.
    if (parGate) {
        if (in.select) {
            parGate = false;
            parWas  = parVal[parSel];
            parEdit = true;
            drawTune();
        } else if (in.back) {
            parGate = false;
            drawTune();
        }
        return;
    }

    if (parEdit) {
        if (in.select) {                    // keep it, still unapplied
            parEdit = false;
            drawTune();
        } else if (in.back) {               // put it back
            parVal[parSel] = parWas;
            if (p.preview) p.preview(parWas);   // and move the part back too
            parEdit = false;
            drawTune();
        } else {
            const int d = step ? step : (in.up ? 1 : in.down ? -1 : 0);
            if (d) {
                // One detent is one step, however fast the wheel is spun.
                // A live row moves the servo on every change, and honouring
                // a burst of detents at once would turn a nudge into a jump
                // the horn takes in a single instant.
                int32_t v = parVal[parSel] + (int32_t)d * p.step;
                if (v < p.lo) v = p.lo;     // clamp: a range has ends
                if (v > p.hi) v = p.hi;
                parVal[parSel] = v;
                // The edit path is the ONE place a preview runs: the operator
                // is watching, and the gate has already been shown. Note there
                // is no set() beside it any more — the wheel moves the PART and
                // the pending value, and nothing else, until Apply. See the
                // apply-gate essay above the parBase/parVal pair.
                if (p.preview) p.preview(v);
                if (p.flags & TP_LIVE) parMoved = true;
                drawTune();
            }
        }
        return;
    }

    if (in.back) {
        // Never silently. Pending edits get the red confirm; a clean section
        // simply leaves.
        if (parDirtyCount() > 0) drawParamsLeave();
        else                     parLeave();
    } else if ((in.up || in.down) && onApply) {
        // Browsing, UP and DOWN are otherwise unused — the wheel is the
        // cursor — so the Apply row lends them to its second job.
        parLoadDefaults();
    } else if (step) {
        int sel = parSel + step;
        const int last = parApplyRow();     // the wrap runs over Apply too
        if (sel < 0)    sel = last;
        if (sel > last) sel = 0;
        parSel = (int8_t)sel;
        drawTune();
    } else if (in.select) {
        if (onApply) {
            parApply();
        } else if (p.flags & TP_BOOL) {
            // A toggle has no range to scroll through, so edit mode would be a
            // press to enter, a press to flip and a press to leave. Flip it.
            parVal[parSel] = parVal[parSel] ? 0 : 1;
            drawTune();
        } else if (p.flags & TP_GATE) {
            parGate = true;
            drawTuneGate();
        } else {
            parWas  = parVal[parSel];   // what LEFT restores
            parEdit = true;
            drawTune();
        }
    }
}

// Cube State's per-pass handler, one of four of the same kind — jogLoop(),
// paramsLoop() and calDialLoop() are the others.
// The wheel points at a face and UP/DOWN turn it, which pollEvent() cannot
// express — it collapses the two into one event, which is right for a menu and
// wrong for a page where pointing and turning are different verbs.
//
// No pumpOnce() here, unlike calDialLoop(): nothing on this page blocks, but
// nothing on it can be aborted either, and SELECT alone leaves — so the chord
// never gets the chance to form, and there would be nothing for it to stop.
static void cubeStateLoop() {
    const JogInput in = pollJog();
    const int step = (in.turn > 0) ? 1 : (in.turn < 0) ? -1 : 0;

    // Both exits, because that is what this screen has always answered to and
    // the operator arriving from Diagnostics is expecting a readout. Either
    // way the model goes back the way it was found FIRST — see csRestore().
    if (in.select || in.back) {
        csRestore();
        toMenu();
        return;
    }

    if (step) {
        int sel = csSel + step;
        if (sel < 0)          sel = kCsFaces - 1;   // wrap, as the menu does
        if (sel >= kCsFaces)  sel = 0;
        csSel = (int8_t)sel;
        drawCubeState();
    } else if (in.up || in.down) {
        // UP is the plain turn and DOWN its prime, the same direction sense
        // the jog page gives the same two buttons over the same six faces.
        // A refused turn still repaints: the trail being full is said in the
        // hint bar, and a press that does nothing with no explanation is how a
        // page looks broken.
        csTurn(in.up ? +1 : -1);
        drawCubeState();
    }
}

// The dial page's per-pass handler, dispatched from loop() before pollEvent()
// for the same reason jogLoop() and paramsLoop() are: the wheel has to mean
// "step this motor" while the buttons stay buttons, and pollEvent()
// deliberately collapses the two into one.
static void calDialLoop() {
    const JogInput in = pollJog();

    // Nothing here blocks between detents, so nothing else runs pumpTick() and
    // the abort chord's hold timer would never accumulate — the Idle and
    // Sensors states' argument. pumpOnce() carries its own 25 ms input
    // throttle, so coexisting with pollJog() costs one extra seesaw read per
    // 25 ms, not one per pass.
    pumpOnce();
    if (Cube.abortPending()) {
        // The chord means stop everywhere, so honour it here too — but as an
        // exit, not a safeStop: nothing is mid-move (calJog() blocks inside
        // moveTo() and has returned by the time this runs), and what to do
        // about the cube differs by owner, which is the one judgement
        // safeStop() cannot make. The diagnostic owner is reachable straight
        // from the clamped rest state, so there may well be a cube in the
        // grip.
        cubeMotors.disableMotors();
        // The calibration flow shut the grippers on an empty centre on the way
        // in, and walking back to the menu with the machine clamped on itself
        // is the one outcome it must not produce.
        //
        // The diagnostic owner keeps its grip, and that is a decision rather
        // than an omission. This page clamps nothing, so it does not know why
        // the cube is held; the operator who walked in from "Cube Ready" to
        // check one face wants it still held when he walks out, and releasing
        // on his behalf drives three actuators he never touched. The risk
        // in leaving it clamped is never the grip but the stale model
        // behind it, and calEnterDial() invalidates that on the way in. So a
        // cube left clamped here is left clamped and UNTRUSTED, which is a
        // state the rest of the sketch already handles: Solve finds it clamped
        // and skips its own clamp, and the menu offers Scan, not Solve.
        if (dialOwner == DialOwner::CalFlow) Cube.unloadCube();
        Cube.clearAbort();
        toMenu();
        return;
    }

    if (in.back) { calLeaveDial(false); return; }

    if (in.select) {
        if (dialOwner == DialOwner::CalFlow) { calLeaveDial(true); return; }
        // Nothing to accept on the diagnostic — it records nothing — so SELECT
        // is one detent forward, exactly as it is on an armed motor row of the
        // jog page. Better a button that turns the motor than a button that
        // quietly does nothing on a screen whose whole risk is looking like the
        // calibration dial.
        calJog(+1);
        calTick = 0;                // re-read at once: the face just moved
    }

    // UP and DOWN split by owner. On the DIAGNOSTIC dial they are full
    // quarter turns — the plain move and the prime — because that page
    // exists for watching one motor across real moves in both directions
    // (see dialTurn()). On the CalFlow dial they stay single detents: the
    // flow is squaring a face by hand, and an aligned full move would home
    // it back to the old marks — the values the flow exists to replace.
    if (dialOwner == DialOwner::Diagnostic && (in.up || in.down)) {
        dialTurn(in.up ? +1 : -1);
        // A failed move left through fail() and the state is no longer this
        // page's; repainting the dial over the red screen would bury it.
        if (state != AppState::MotorDial) return;
        calTick = 0;                // re-read at once: the face just moved
        return;
    }

    // One detent is one step, however fast the wheel is spun — the tuning
    // editor's rule, for the same reason: this drives real hardware, and
    // honouring a burst of detents at once turns a nudge into a lunge.
    const int d = (in.turn > 0) ? 1 : (in.turn < 0) ? -1
                : in.up ? 1 : in.down ? -1 : 0;
    if (d) {
        calJog(d);
        calTick = 0;                // re-read at once: the face just moved
    }

    if (millis() - calTick >= 50) {
        // ~20 Hz, the Input Report page's rate. Every read is I2C traffic on
        // the encoder mux, and an unthrottled loop() would hammer it as fast
        // as it can spin for a number no eye can follow anyway.
        calTick = millis();
        calDialTick();
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

    // Sized to what this can actually hold: six single letters, "Ring" and six
    // separators is 20 bytes including the terminator. A round 64 would leave
    // the compiler unable to prove the line below fits its row, and a
    // -Wformat-truncation warning that cries wolf hides the genuine ones.
    char missing[24];
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
        // Same width as a row in `rows`, so what fits here is exactly what
        // survives addLine() — and the compiler can see that it does.
        char line[sizeof(rows[0])];
        snprintf(line, sizeof(line), "Motor encoders offline: %s", missing);
        addLine(line);
    }

    opScreen(CubeDisplay::OpKind::Error, "Startup Faults",
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
    // the proven one — and before anything below could move a part to a
    // tuned position.
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

    // The machine comes up fully RETRACTED, and stays there.
    //
    // begin() has already put every part that was not retracted back: both
    // servos (CubeServo::begin retracts out of every state but 0) and the
    // ring (initRingStepper re-homes or retracts from its stored state). That
    // is the pose the operator loads a cube into — Load & Scan expects the bay
    // open, and scanCube() opens with unloadCube() on the same assumption — so
    // nothing here moves anything.
    //
    // No eject at boot, deliberately. Sweeping a gripper up under whatever is
    // in the bay right after announcing "retracted" is wrong, and the eject
    // pose is neither the load pose nor the rest pose, so the first scan would
    // begin by lowering it again. Eject Cube on the menu presents a cube on
    // purpose; the boot does not.

    // Seed the rotary baseline from the encoder's ACTUAL count. It is a free
    // running absolute counter, so leaving prevPos at 0 makes the first poll
    // register a large phantom rotation and jump the menu selection.
    if (Cube.encoderInitialized) prevPos = menuEncoder.getPosition();

    Menu.begin(&kScreenMainPre, drawMenu);

    // Every boot actuator has now run — servo sweeps, stepper homing — and
    // the boot frame sat on the glass through all of it.
    // Re-send the whole frame before the first screen the operator is meant
    // to read. toMenu() repeats this for its own reasons; the call here is for
    // the self-test-failure branch, which does not go through toMenu() and
    // must not inherit a damaged panel either. Two requests before one render
    // cost nothing extra.
    cubeDisplay.repaintAll();

    if (Cube.selfTestPassed()) {
        // Walk the panel through menu -> operation screen -> menu before the
        // operator sees it.
        //
        // This is empirical, and it is written down as such. The boot menu
        // came up with artefacts — old boot-frame pixels, coloured snow — on
        // every bench run, and the whole-screen repaint above did not scrub
        // them; yet selecting any item that opens an operation screen, and
        // then returning, leaves the panel clean every single time. The
        // objects toMenu() touches are the same in both cases and nothing in
        // the code explains the difference, so until it is understood the
        // boot simply performs the sequence that is known to work. Three
        // renders, well under half a second.
        //
        // pumpDelay(), not displayUpdate(): LVGL refreshes at most once per
        // LV_DEF_REFR_PERIOD (33 ms), so two updates back to back would
        // collapse into one render and the intermediate screen would never
        // reach the panel. The pump calls the display every 5 ms for 50 ms,
        // which guarantees each step is drawn before the next begins.
        toMenu();
        pumpDelay(50);
        showOp(Op::Info, "Cube Solver", "Ready");
        pumpDelay(50);
        toMenu();
        pumpDelay(50);
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

    // The motor dial, third of the same kind: the wheel steps one face motor
    // there while the buttons stay buttons. Same one-poller-per-pass rule as
    // the two above, and the same handler whichever page opened it.
    if (state == AppState::MotorDial) {
        calDialLoop();
        return;
    }

    // Cube State's net turner, fourth of the same kind: the wheel points at a
    // face there while UP and DOWN turn it. Same one-poller-per-pass rule.
    if (state == AppState::CubeState) {
        cubeStateLoop();
        return;
    }

    // Stats, fifth: a static table, but its reset chord (SELECT+RIGHT held)
    // is invisible to pollEvent(), which folds RIGHT into Select and fires on
    // the press edge. Same one-poller-per-pass rule.
    if (state == AppState::Stats) {
        statsLoop();
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

        // The color everything downstream of this press will wear. Read from
        // the row under the cursor BEFORE handle() runs it, because an action
        // draws its first screen from inside handle() and the menu has moved
        // on by the time it returns.
        //
        // This one line is the whole wayfinding mechanism: a branch's color
        // reaches its operation screens because the row that opened them said
        // so, not because twenty action functions each remembered to. Set on
        // submenu entries too, harmlessly — they draw no operation screen, and
        // the next Select overwrites it.
        if (ev == MenuEvent::Select) {
            s_opTheme = CubeMenu::themeOf(Menu.current(), Menu.selectedItem());
        }

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
            // Clear first: pollEvent() fires on the SELECT rising edge, and the
            // abort chord needs LEFT as well, but clearing here also discards
            // any latch left over from a previous operation.
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
            // Clamp the cube. scanCube()'s per-pass choreography ENDS with
            // ringRetract() and botServoRetract(), so it returns with the
            // machine fully released and the cube loose in the bay — and every
            // face motor disengaged from the thing it had just finished
            // reading. Whatever runs next wants it held.
            //
            // Only on SUCCESS. A failed scan means the machine does not know
            // what it is holding, and closing three grippers on that is worse
            // than leaving them open.
            //
            // Keep the face row up: this is the one moment the operator can
            // check the machine read the cube it is actually holding. It goes
            // up BEFORE the clamp, so it is readable during the sweeps rather
            // than only after them.
            showOp(Op::Scan, "Scan", "Clamping the cube", "SELECT+LEFT to abort");
            Cube.displayFaces(Cube.scanFaceChips);
            Cube.displayUpdate();

            clampCube();

            // Honoured exactly as Loading and ModeClamp honour it — release
            // rather than press on. safeStop() suspends the latch internally
            // so the unload is a real unload, not a pumped-out no-op.
            if (Cube.abortPending()) {
                Cube.safeStop(CubeSystem::ERR_ABORTED);
                fail("Aborted", "Cube released", CubeSystem::ERR_ABORTED,
                     CubeFaultLog::Scan);
                break;
            }

            showOp(Op::Done, "Scan", "Scan complete", "Press SELECT");
            Cube.displayFaces(Cube.scanFaceChips);
            Cube.displayUpdate();
            state = AppState::Done;
        }
        break;
    }

    case AppState::Solving: {
        // Get the "Finding solution..." net onto the glass BEFORE the search
        // blocks. actSolve() painted it, and loop() called displayUpdate()
        // again on the way in — but painted is not flushed: LVGL repaints only
        // when its refresh timer comes due (LV_DEF_REFR_PERIOD, 33 ms), and two
        // back-to-back updates a millisecond apart almost never straddle that
        // tick. Without this the panel sat on the MENU for the whole compute,
        // up to ~10 s, and Solve looked like a button that had not registered.
        // One refresh period of pumping is what ModeComputing does for the
        // same reason, and it is what actually lands the frame.
        //
        // The chord is polled during these 40 ms. solveVirtual() does not
        // look at the latch, but Loading does, right after the clamp — so a
        // chord that lands here still aborts before anything moves, just
        // later than the hand that held it expected.
        pumpDelay(40);
        int e = Cube.solveVirtual();
        if (e) {
            // safeStop, not a bare fail(). fail() logs and paints; it releases
            // nothing — and the cube is CLAMPED here.
            //
            // It is clamped because a successful scan leaves it that way — see
            // Loading, two states down.
            //
            // This is not an exotic path: solveVirtual() returns 12/13/14/15
            // for an ordinary colour misread, so scan a cube with one sticker
            // read wrong, press Solve, and the machine lands on a red screen
            // with all three grippers shut and no way out but Eject.
            Cube.safeStop(e);
            fail("Solve failed", solveErrorText(e), e, CubeFaultLog::Solve);
        } else {
            // Cube.solutionLength, never a recount: it is the solver's own
            // answer and the same number Loading and the ribbon quote.
            char sub[48];
            snprintf(sub, sizeof(sub), "Solution found in %d moves",
                     Cube.solutionLength);
            drawSolveNet(sub, "SELECT to solve, LEFT to cancel");
            state = AppState::SolveConfirm;
        }
        break;
    }

    case AppState::SolveConfirm:
        // The last screen before anything moves, and the reason it exists: a
        // solve is twenty-odd moves against a model, and the net above this
        // line is the only place the operator can see the model before the
        // machine acts on it.
        //
        // Nothing is running here — no clamp driven, no solve timer started,
        // no stats touched — so backing out costs exactly nothing and leaves
        // no half-started solve behind. That is what makes LEFT safe.
        //
        // Nothing pumps either, for the same reason AwaitCube and Done do not:
        // the machine is standing still waiting on a human, and loop() already
        // refreshed the display at the top of the pass. pollEvent() swallows
        // the SELECT+LEFT chord rather than acting on it, which is right when
        // there is nothing to abort — LEFT alone is the way out.
        if (ev == MenuEvent::Select) {
            state = AppState::Loading;
        } else if (ev == MenuEvent::Back) {
            // Declined. The cube is left exactly as it stands — clamped, if it
            // was clamped — because that is where the scan and the previous
            // solve leave it, it is a safe place for the machine to sit, and
            // Eject is the way to get the cube out from there.
            //
            // The computed solution is deliberately NOT cleared. Nothing reads
            // solutionLength except the states downstream of this one, each of
            // which is reached only through a fresh solveVirtual(), and
            // clearSolution() here would buy nothing but a second failure mode.
            toMenu();
        }
        break;

    case AppState::Loading: {
        // Conditional, because by the time Solve is reachable the cube is
        // usually ALREADY clamped — the scan leaves it that way, and so does
        // the display spin at the end of the previous solve. Re-driving an
        // extended servo is at best a wasted sweep and at worst a twitch
        // against a cube three grippers are already holding.
        //
        // The screen lives INSIDE the branch for the same reason: said on the
        // way in from Solving it would claim a clamp on every solve, including
        // the common one where nothing moved. Only announce work the machine
        // is actually about to do.
        if (!cubeIsClamped()) {
            showOp(Op::Solve, "Solve", "Clamping the cube", "SELECT+LEFT to abort");
            clampCube();

            // Settle before the first move. pumpDelay, never delay: the panel
            // has to keep refreshing and the abort chord has to still be
            // noticed during the wait. It returns immediately if an abort is
            // already latched, which the check below then acts on.
            pumpDelay(kClampSettleMs);
        }

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
            // The plain solve ends on the display spin and never reaches
            // Unloading, where the modes record theirs, so its stats are
            // recorded here. Counted here rather than when the operator
            // finally presses SELECT: a solve that happened happened, whether
            // or not anyone acknowledges the screen, and Executing is reached
            // exactly once per solve so it cannot double-count.
            statsRecordTimedSolve(solveMillis, Cube.solutionLength);

            // Let go of everything except the bottom gripper. Deliberately NOT
            // unloadCube(): that retracts the bottom servo too, the cube drops
            // into the bay, and there is nothing left engaged to turn it.
            Cube.unloadCubeKeepBottom();

            // ringRetract() de-energises the drivers on its way out, so the
            // spin has to switch them back on — and they stay on for the whole
            // screen: a stepper de-energised mid-spin hands the cube's
            // momentum to the nearest detent and loses the count
            // spinToSquare() depends on.
            cubeMotors.enableMotors();

            drawSolveDisplay();
            spinStopAt = millis() + kSpinMaxMs;
            spinning   = true;
            cubeMotors.spinBegin(kSpinMotor, spinStepsPerSec());
            state = AppState::Displaying;
        }
        break;
    }

    case AppState::Displaying: {
        // Nothing here blocks for long, so nothing else pumps — the same
        // argument as Idle and DemoRest, and the machine is still holding the
        // cube.
        pumpOnce();
        if (Cube.abortPending()) {
            // Square the cube FIRST, exactly as the other two exits from this
            // state do. This is a mechanical problem, not a bookkeeping one
            // the model wipe covers: safeStop() ends in botServoRetract(),
            // which drops the cube into the bay yawed by the spin's step
            // count % turnStep — up to a quarter turn — and nothing
            // afterwards puts it back: the Error state only clears the abort,
            // and the D finger re-homing on a later move squares the MOTOR, not
            // the cube, because the finger has let go of it by then. See
            // spinToSquare() for why nothing can close on a cube standing at
            // 137 degrees.
            //
            // This has to come BEFORE disableMotors() and BEFORE safeStop():
            // it only works while the drivers are energised and the D finger is
            // still engaged. The latch cannot cut it short — spinToSquare()
            // pumps for the panel but deliberately ignores the abort result,
            // so the squaring always runs to completion. No
            // pumpAbortSuppressed dance is needed here, unlike unloadCube(),
            // whose servo sweeps DO bail on the latch.
            if (spinning) spinToSquare();

            // Kill torque before safeStop() drives the servos, exactly as
            // safeStop() itself does. The cube now goes down square, but the
            // model still goes with it: safeStop() invalidates on every fault
            // as a matter of contract, and an abort is a fault path. Squaring
            // is about what the grippers can close on next, not about saving
            // the scan — a rescan is still the honest next step.
            cubeMotors.disableMotors();
            spinning = false;
            Cube.safeStop(CubeSystem::ERR_ABORTED);
            fail("Aborted", "Cube released", CubeSystem::ERR_ABORTED,
                 CubeFaultLog::Solve);
            break;
        }

        if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            // LEFT does what SELECT does, deliberately. There is nothing to go
            // "back" to — the solve is over — and the alternative is leaving
            // the cube standing on one gripper, out of square, for whatever
            // the operator picks next.
            // Two messages, honestly ordered: the squaring is now a visible
            // stretch of slow rotation (up to kSpinSecs), not an instant.
            cubeDisplay.setMessage("Squaring the cube");
            cubeDisplay.setStatus("");
            Cube.displayUpdate();

            if (spinning) spinToSquare();
            cubeMotors.disableMotors();     // nothing may fight the grippers
            spinning = false;

            cubeDisplay.setMessage("Clamping the cube");
            Cube.displayUpdate();

            // Clamped again rather than released: the cube never left the
            // machine's grip, the model is still valid, and a Solve straight
            // after this finds it already clamped and skips its own clamp.
            clampCube();

            // The abort chord can only land here if it was already being held
            // as the screen was dismissed — the check at the top of the state
            // catches every other case — but a latch turns the sweeps above
            // into single writes, so the grip cannot be assumed closed.
            // Released and reported, exactly as the other clamp sites do it.
            if (Cube.abortPending()) {
                Cube.safeStop(CubeSystem::ERR_ABORTED);
                fail("Aborted", "Cube released", CubeSystem::ERR_ABORTED,
                     CubeFaultLog::Solve);
                break;
            }

            toMenu();
            break;
        }

        if (spinning) {
            if ((int32_t)(millis() - spinStopAt) >= 0) {
                // Long enough. Park on a whole revolution and drop torque; the
                // screen and its button are untouched, so nothing about this
                // is visible except the cube stopping.
                spinToSquare();
                cubeMotors.disableMotors();
                spinning = false;
            } else {
                // Serviced every pass, and deliberately NOT on a timer of its
                // own: runSpeed() keeps the step interval by the clock, so
                // calling it early costs nothing and calling it late delays one
                // step rather than bunching several. Never a spin loop either —
                // this screen has a live button on it and the abort chord has
                // to stay heard.
                cubeMotors.spinService();
            }
        }
        break;
    }

    case AppState::Unloading: {
        // The cube STAYS CLAMPED. This state does not unload, despite the
        // name, and that is deliberate: a successful scan leaves the cube
        // clamped and the plain Solve ends on the display spin and clamps
        // again on the way out, so a mode run that dropped the cube into the
        // bay would make the obvious next thing — run it again — cost a
        // re-clamp that achieved nothing. Leaving it held costs nothing: the
        // cube is not going anywhere, Eject is one menu item away, and the
        // grippers hold it in the position everything downstream assumes.
        // Not renamed: it is reached from two places and named in the
        // AppState block, and this note is the cheaper fix.

        // Every entry into this state is a completed solve — ModeExecuting and
        // StepReady both finish here with solveMillis final. Two paths record
        // elsewhere and must not be counted again here: Demo never unloads
        // between runs and records at its own transition, and the plain Solve
        // now ends on the display spin, which records as it enters. Step Solve
        // counts as an UNTIMED solve: it is human-paced, so its wall time would
        // poison Best and Average with however long the operator stood
        // thinking.
        if (runMode == RunMode::Step) {
            statsVals[CubeStats::UntimedSolves]++;
            statsCommit();
        } else {
            statsRecordTimedSolve(solveMillis, Cube.solutionLength);
        }

        char sub[64];
        formatSolveResult(sub, sizeof(sub));
        // s_opTitle, not "Solve": the modes share this completion state, and
        // the Done screen should name the operation that actually ran.
        showOp(Op::Done, s_opTitle, "Solved!", sub);
        state = AppState::Done;
        break;
    }

    case AppState::Ejecting:
        // Straight to the eject pose, without a detour through the bay.
        //
        // Deliberately NOT unloadCube(): that retracts the bottom servo as its
        // last step, dropping the cube down into the color-sensor box, and the
        // botServoEject() that used to follow immediately hauled it back up
        // again. unloadCubeKeepBottom() is the front of the same sequence —
        // ring, then top, in the order that is mechanically load-bearing —
        // with only that last step left out, so the cube never goes down.
        Cube.unloadCubeKeepBottom();
        Cube.botServoEject();   // at the tuned height, not the mid-scan partial

        // The virtual state goes with the cube: once it is out of the machine's
        // grip we have no idea whether the user turned a face, so anything
        // derived from the old scan is a guess. resetCube() is also what flips
        // the main menu back to its pre-scan form via syncMenuRoot(). Done
        // HERE, as it always was, and not when removal is detected — the cube
        // is reachable by hand from this moment on, so from this moment on the
        // model is a guess whether or not anyone picks it up.
        Cube.virtualCube.resetCube();
        Cube.clearSolution();

        // Open the watch on both boards and hand over to EjectWait. The first
        // sweep happens on the very next pass; presenceSweep() reports its own
        // warm-up, so there is nothing to wait for here.
        ejectWatchBegin();

        // One line, and it has to be true. The cube can simply be taken; the
        // button is the backstop for the machine not noticing, not a step.
        showOp(Op::Info, "Eject", "Take the cube out",
               "SELECT if the machine does not notice");
        state = AppState::EjectWait;
        break;

    case AppState::EjectWait:
        // Nothing in this state blocks, so nothing else runs pumpTick() — the
        // Sensor Test pages' argument exactly, and without this the abort
        // chord's hold timer would never advance.
        pumpOnce();
        if (Cube.abortPending()) {
            // An exit, not a safeStop. The machine is standing still holding a
            // cube it is trying to give away, and the abort's answer is the
            // same as the normal one: put the horn down. clearAbort() BEFORE
            // the sweep, because a latched abort turns pumpDelay() into an
            // immediate false and would reduce the retract to a single write
            // and a bail — the pumped-out release safeStop() exists to
            // prevent. Clearing also demands the chord be released before it
            // can re-latch, so the sweep cannot abort itself halfway.
            Cube.clearAbort();
            ejectFinish();
            break;
        }

        // SELECT (or LEFT, which pollEvent() folds into it) is the manual
        // finish, and it stays whatever the sensor does. If the cube cannot be
        // seen at this height the fall never comes, and this is the only way
        // out that does not involve waiting two minutes.
        if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            ejectFinish();
            break;
        }

        if (ejectWatch() || (int32_t)(millis() - ejectGiveUpAt) >= 0) {
            // Detected or given up on — the same ending either way, and
            // deliberately so. Both mean "there is nothing more to wait for",
            // and a screen distinguishing them would be reporting on the
            // sensor rather than on the machine. Straight back to the menu
            // rather than through a "Cube ejected" acknowledgement: the whole
            // point of watching was to stop asking for a press that tells the
            // machine nothing it does not already know.
            ejectFinish();
        }
        break;

    case AppState::CalMotorsPrompt:
        if (ev == MenuEvent::Select) {
            showOp(Op::Calibrate, "Motor Calibration", "Closing the grippers",
                   "SELECT+LEFT to abort");
            state = AppState::CalMotorsClamp;
        } else if (ev == MenuEvent::Back) {
            toMenu();
        }
        break;

    case AppState::CalMotorsClamp:
        // The shared clamp again. There is no cube in the grip this time; the
        // ordering is about the mechanism, not its contents, which is exactly
        // why the same call serves both.
        clampCube();

        // Honoured exactly as ModeClamp honours it — release rather than press
        // on. safeStop() suspends the latch internally so the unload is a real
        // unload and not a pumped-out no-op.
        if (Cube.abortPending()) {
            Cube.safeStop(CubeSystem::ERR_ABORTED);
            fail("Aborted", "Grippers released", CubeSystem::ERR_ABORTED,
                 CubeFaultLog::Cal);
            break;
        }

        calSel  = 0;
        calTick = 0;
        state = AppState::CalMotorsPick;
        drawCalList();
        break;

    case AppState::CalMotorsPick:
        // Nothing in this state blocks, so nothing else runs pumpTick() and
        // the abort check below could never fire — the Sensors state's
        // argument, with the same kind of exit.
        pumpOnce();
        if (Cube.abortPending()) {
            // Release on the way out. The grippers are shut because this flow
            // shut them, and the menu has no idea that happened.
            Cube.unloadCube();
            Cube.clearAbort();
            toMenu();
            break;
        }

        if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
            // Wraps, as the jog page and the menu wrap: rolling off the Save
            // row back to U is a shorter trip than winding all the way up.
            calSel = (int8_t)((calSel + (ev == MenuEvent::Down ? 1 : kCalRows - 1))
                              % kCalRows);
            drawCalList();
        } else if (ev == MenuEvent::Select) {
            if (calSel == kCalSave) {
                // The original one-shot calibration, unchanged. It is the last
                // STEP of the flow now rather than the whole of it — every
                // value it writes is derived from where the six faces are
                // standing, which is what the rows above this one were for.
                showOp(Op::Calibrate, "Motor Calibration", "Finding home positions",
                       "Do not touch the machine");
                state = AppState::CalMotors;
            } else {
                calEnterDial(DialOwner::CalFlow);
            }
        } else if (ev == MenuEvent::Back) {
            // Leaving the flow releases the machine, for the same reason as
            // the abort exit above.
            Cube.unloadCube();
            toMenu();
        } else if (millis() - calTick >= 250) {
            // Six encoder reads a tick is real traffic on the mux the wheel
            // shares, and four times a second is already faster than the
            // numbers can be read. The Motor Sensors page's argument, at the
            // rate six rows rather than seven deserve.
            calTick = millis();
            calListRows();
        }
        break;

    case AppState::CalMotors: {
        int e = Cube.calibrateMotorRotations();
        // Release before either screen goes up. This state is only reachable
        // from the flow's Save row now, so the grippers are shut on an empty
        // centre and nothing downstream would open them — least of all the
        // failure path, which parks the machine waiting for a human.
        Cube.unloadCube();
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
        // Release before either screen goes up, exactly as the motor case
        // above does and for the same reason: the failure path parks the
        // machine waiting for a human, so nothing downstream will open the
        // grippers. calibrationBail() restores the colour tables and kills
        // torque but never releases — it knows nothing about what is held —
        // and the sensor-board return (90) leaves the routine before it moves
        // anything at all, so entered from the clamped rest state it used to
        // put a red screen on a machine still gripping the cube.
        //
        // Free on every other return, which is why it is unconditional rather
        // than a failure-only branch: the abort and reorientation returns (9
        // and 82) have already been through safeStop(), and the success and
        // save-failure returns (0 and 8) leave STEP 3's last botServoRetract()
        // in force with the ring already back — so ringMove() early-returns
        // and each servo is written the angle it is already at.
        Cube.unloadCube();
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
    case AppState::MotorDial:
    case AppState::CubeState:
    case AppState::Stats:
        break;      // handled before pollEvent() ever ran; nothing here
                    // consumes a MenuEvent

    case AppState::ParamsLeave:
        // The answer to drawParamsLeave()'s red confirm. SELECT is the
        // destructive answer, as it is on Reset Defaults; LEFT goes back to
        // the list with every pending value — and every previewed part — still
        // exactly where it was left.
        if (ev == MenuEvent::Select) {
            parDiscard();
        } else if (ev == MenuEvent::Back) {
            state = AppState::Params;
            drawTune();
        }
        break;

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
        // Nothing in this state blocks for long, so nothing else runs
        // pumpTick() — and without this the abort check below could never
        // fire: the chord's hold timer resets whenever the pump goes quiet
        // for 200 ms, and a parked cursor's reads take nothing like that
        // long. A read that does settle pumps from inside its wait.
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
            // an end stop. The move repaints and reads NOTHING this pass, so
            // a wheel spun across the grid costs no integration windows at
            // all — only the one the next tick spends on wherever it stopped.
            senSel = (int8_t)((senSel + (ev == MenuEvent::Down ? 1 : 17)) % 18);
            drawSensors();
        } else if (ev == MenuEvent::Select) {
            // Drill into the one selected.
            diagLastTick = 0;       // read the chosen sensor on the next pass
            state = AppState::SensorRaw;
            drawSensorRaw();
        } else if (ev == MenuEvent::Back) {
            sensorsLeave();
            toMenu();
        } else if (millis() - diagLastTick >= 60) {
            // The gap between reads, not the read rate. A parked cursor costs
            // ~1 ms of bus, so this is ~16 Hz and pollEvent() has the rest of
            // the time — no button is seen while inside scanSingle(). The
            // opening sweep still blocks one 160 ms window per cell for its
            // ~3 s, and that is the one stretch where a tap can be missed.
            diagLastTick = millis();
            sensorsTick();
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

        if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
            // The wheel walks the sensors from in HERE, instead of making the
            // operator back out to move and come in again. Same numbering and
            // same wrap as the grid, so backing out lands the cursor on the
            // sensor that was being read.
            senSel = (int8_t)((senSel + (ev == MenuEvent::Down ? 1 : 17)) % 18);
            diagLastTick = 0;       // read the new one at once
            drawSensorRaw();
        } else if (ev == MenuEvent::Back || ev == MenuEvent::Select) {
            // BOTH go back to the grid, one level up: there is nothing deeper
            // than this screen to enter, so the only thing SELECT can honestly
            // mean is what LEFT means.
            diagLastTick = 0;
            state = AppState::Sensors;
            drawSensors();
        } else if (millis() - diagLastTick >= 200) {
            // Slower than the grid on purpose: four numbers changing sixteen
            // times a second cannot be read. The reads themselves are free —
            // the channel is already held on the sensor being watched.
            diagLastTick = millis();
            sensorRawTick();
        }
        break;
    }

    case AppState::Motors:
        // Pumped and abortable like the color pages, and for a reason they do
        // not have: this one can start a homing run, and the chord has to mean
        // stop on the page that offers it as well as inside it.
        pumpOnce();
        if (Cube.abortPending()) {
            Cube.clearAbort();
            toMenu();
            break;
        }

        if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
            // Wraps, like the calibration list next door: rolling off Home
            // back to U is a shorter trip than winding all the way up.
            motSel = (int8_t)((motSel + (ev == MenuEvent::Down ? 1 : kMotRows - 1))
                              % kMotRows);
            drawMotors();
        } else if (ev == MenuEvent::Select) {
            if (motSel == kMotHome) {
                motorsHome();
            } else if (motSel < kMotFaces) {
                calSel = motSel;
                calEnterDial(DialOwner::Diagnostic);
            }
            // The ring row falls through deliberately — cubeMotors drives the
            // ring to named states, not by steps, so there is nothing for a
            // wheel to turn. Its hint bar says so, which is the difference
            // between a row that cannot be entered and a button that looks
            // broken.
        } else if (ev == MenuEvent::Back) {
            toMenu();
        } else if (millis() - diagLastTick >= 100) {
            // Seven reads per tick is cheap — a couple of I2C transactions
            // each — but unthrottled they would saturate the encoder mux bus
            // for a screen no faster than the eye can read anyway.
            diagLastTick = millis();
            motorsRows();
        }
        break;

    case AppState::InputReport:
        // 'ev' is deliberately ignored here, unlike every other screen. This
        // page exists to show what each button does, and exiting on SELECT or
        // Back meant three of the five buttons — SELECT, LEFT, and RIGHT, which
        // pollEvent() folds into Select — left the screen the moment you tested
        // them. Only the SELECT+LEFT chord gets out, and inputReportTick() has
        // to find it: pollEvent() swallows the chord on purpose.
        if (millis() - diagLastTick >= 50) {
            // ~20 Hz. The report reads the seesaw over I2C on every tick, and
            // an unthrottled loop() would hammer the same bus pollEvent() is
            // trying to use.
            diagLastTick = millis();
            if (inputReportTick()) {
                // Holding the chord is also how an abort is raised, and this is
                // now the one screen where an operator holds it with nothing
                // running. Clear the latch on the way out so the next operation
                // does not fail with code 25 before it starts.
                Cube.clearAbort();
                toMenu();
            }
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
        // The shared clamp, gated the way Solve gates its own: the modes are
        // only reachable with a scanned cube already in the grip, so clamping
        // — and saying "Clamping the cube" — on every entry announced work the
        // machine was not doing. When the grip really is open the clamp still
        // runs, and says so; the entry screens themselves no longer claim it.
        if (!cubeIsClamped()) {
            cubeDisplay.setMessage("Clamping the cube");
            Cube.displayUpdate();
            clampCube();
        }

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
                // remember to set.
                toComputing();
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
            // (dial, count, decorative frame) over the entry message.
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
                toComputing();
                break;
            case RunMode::Pattern:
                // The fold IS the whole operation — nothing to compute and
                // nothing to run, so this is a completion, not a handover. It
                // finishes the way a solve finishes: show the cube off by
                // turning it, then SELECT clamps and goes back to the menu.
                //
                // Through Displaying, never letting go: the cube stays on the
                // bottom gripper the whole time, so the model stays sound and
                // "Solve to undo" is true from the menu afterwards. Presenting
                // the cube instead (release and lift to the eject pose) would
                // put it within reach, and once a hand can turn a face the
                // machine has no idea what it is holding — every other path
                // that presents the cube resets the model for that reason.
                // Offering both at once meant Solve could clamp an empty bay
                // and run twenty moves against a pattern that left the building.
                // Anyone who wants it in their hand presses Eject, which
                // resets the model.
                //
                // executeMove() mutates cubeArray only; the color array the
                // net is drawn from is stale until this refresh. Despite the
                // UNFINISHED label on its header, rebuildFromCubeArray() is
                // the working "refresh colorCubeArray" call.
                Cube.virtualCube.rebuildFromCubeArray();

                // Same handover as the end of a solve — see AppState::Executing,
                // which this deliberately mirrors rather than reimplements.
                // NOT unloadCube(): that retracts the bottom servo too and
                // leaves nothing engaged to turn the cube with.
                Cube.unloadCubeKeepBottom();
                cubeMotors.enableMotors();

                // No stat. A pattern fold is not a solve, and Displaying sits
                // downstream of where a solve gets counted — the counting is
                // done in Executing, which this path never enters, so there is
                // nothing here to suppress. Said out loud because "it finishes
                // like a solve" is exactly the sentence that would talk someone
                // into adding one.
                drawSolveDisplay();
                spinStopAt = millis() + kSpinMaxMs;
                spinning   = true;
                cubeMotors.spinBegin(kSpinMotor, spinStepsPerSec());
                state = AppState::Displaying;
                break;
            default:
                // Scramble Solve and Demo roll straight into the solve.
                toComputing();
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
            // The cube is CLAMPED here, so release it before the error screen
            // starts waiting on a human.
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
                statsRecordTimedSolve(solveMillis, Cube.solutionLength);

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
                // nothing. The cube stays clamped, as actSolve's copy of this
                // guard leaves it: nothing ran, so nothing changed.
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
            // leaves: cube CLAMPED, model still valid, menu still post-scan.
            // Every idle turn was tracked, so nothing is stale, and nothing
            // is mid-move — idleTurn() runs each turn to completion before
            // this can be reached. Only a fault or Eject lets go.
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
            showOp(Op::Solve, "Demo", "Scrambling", "SELECT or LEFT ends the demo");
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
            // the post-scan menu can re-Solve — and the cube stays clamped
            // for exactly that: Solve from the menu finds it held and picks
            // up where this left off. The half-consumed solution is wiped:
            // every solve entry recomputes anyway, but a stale one left
            // lying around is a replay waiting for the one path that forgets
            // to.
            Cube.clearSolution();
            toMenu();
        }
        break;
    }
    }
}
