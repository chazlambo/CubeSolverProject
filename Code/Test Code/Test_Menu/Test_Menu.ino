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
//  No scan, no solve, no calibration. What it does do is drive the actuators
//  directly, one at a time, from the Actuators menu — every servo position,
//  every ring position, every face motor in both directions, and both whole-
//  cube rotations. That is the panel version of Test Code/Actuator_Test, and it
//  is the reason this sketch is worth flashing to a real machine.
//
//  THESE MOVE REAL HARDWARE: the Actuators menu, Load/Eject, and Servo
//  Positions under Diagnostics > Tuning, which drives a horn to each value as
//  you dial it — that is the point of it. Everything else under Screens and
//  Diagnostics is drawing and navigation only.
//
//  Tuning previews on the wheel and COMMITS on the Apply row, which is the
//  last row of every section and the only thing here that writes an owner or
//  EEPROM. Walking away with edits pending asks, in red. Defaults live in the
//  shared kTune table (CubeTuneTable — one copy for this sketch and the
//  firmware); Diagnostics > Tuning > Reset Defaults puts every one of them
//  back.
//
//  THE TREE
//  --------
//    Load Cube / Eject Cube      real servos
//    Actuators                   real everything, one part at a time
//    Screens > Operations        scan, color capture, motor squaring, and
//                                the two endings — solve result, eject prompt
//            > Modes             the firmware's five Modes, same order
//            > Cube Views        the unfolded net, including the solve confirm
//                                and Cube State, where the wheel points at a
//                                face and UP/DOWN turn the MODEL — no motors
//            > Messages          info, error, stats
//            > Navigation        menu rendering at every item count
//    Diagnostics > Tuning        six sections, committed on Apply
//                > Input Report, Color Sensors, Motor Sensors, Fault Log
//                  Color Sensors is a two-board grid over a canned sweep;
//                  Motor Sensors lists eight rows and opens the same dial the
//                  calibration flow does — see DialOwner. Both are canned.
//
//  Screens > Modes deliberately mirrors the firmware's Modes menu item for item
//  and order for order. A rehearsal that groups the screens differently from
//  the machine stops being a rehearsal of the machine.
//
//  THE FRAME COLOR
//  ---------------
//  The band says WHERE YOU ARE, not what the machine is doing — see §2 of
//  docs/theme/UI_DESIGN.md. This sketch works the same way the firmware does:
//  s_opTheme is read off the selected row in loop(), and every operation screen
//  goes through opScreen(), which stamps it. Nothing calls showOperation()
//  directly.
//
//  Two places here are deliberately NOT wayfinding, and both earn it:
//  Screens > Navigation, whose five child screens are five different colors
//  because exercising all six IS the point of that subtree, and Screens'
//  children generally, which each wear the color of the FIRMWARE branch they
//  rehearse rather than their parent's — a screen photographed in the wrong
//  color is not a rehearsal of the screen the machine draws.
//
//  WHY THE SCREEN DEMOS ARE HERE
//  -----------------------------
//  The operation screens — scan step rows, calibration color chips, the solve
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
//    SELECT + LEFT         swallowed as the abort chord, never a menu action.
//                          Input Report is the one page where it is also the
//                          way OUT, because every other button is one the
//                          operator came there to press.
//
//  See docs/theme/UI_DESIGN.md before changing anything the panel draws.
// =============================================================================

#include <CubeSystem.h>
#include <CubeHardwareConfig.h>
#include <CubeMenu.h>
#include <CubeTuneTable.h>   // the shared tuning table; the editor UI is here

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
    Jog,        // direct actuator control; see the note above pollJog()
    Params,     // a settings list; the wheel changes a value, not a cursor
    ParamsLeave,// the red confirm before walking away from unapplied edits
    MotorDial,  // one face motor on the wheel; see calDialLoop(). Named for the
                // SCREEN rather than for the calibration flow, because two
                // pages open it now — see DialOwner.
    CubeState,  // the stored model, with the wheel on a face; see csTurn()
    Loading,    // clamp the cube, once
    Ejecting    // release and present it, once
};

static TState state = TState::Menu;

// A screen that redraws itself every pass (the live input report), or animates
// from canned data (the operation-screen demos).
enum class Live : uint8_t { None, Input, Steps, Chips, Scramble, Fold, Step, Demo,
                            Sensors, SensorRaw, Motors, MotorsHome, Faults,
                            Idle, IdleSolve, StepScram, SolveNet, CalPrompt,
                            CalClamp, CalPick, CalSave };
static Live     live      = Live::None;
static uint32_t liveStart = 0;
static uint32_t lastLive  = 0;

// The color every operation screen this sketch draws will wear.
//
// The same one-line mechanism the firmware uses, and for the same reason: the
// frame band says WHERE YOU ARE, so the color is a fact about the menu row that
// was selected. loop() reads it with CubeMenu::themeOf() one assignment before
// Menu.handle(), and opScreen() stamps it on every screen. Nothing per-action
// to remember, and nothing that drifts when a table is recolored.
//
// Two deliberate exceptions, both mirrored from the machine: an Error screen is
// always red (enforced inside CubeDisplay::setOpTheme(), not here), and Idle
// Mode cycles all six colors to look alive — see kIdleCycle.
static MenuTheme s_opTheme = MenuTheme::Blue;

// Input edge-detection state, lifted from the firmware unchanged. This is the
// code under test, so it is copied rather than approximated.
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

// Which page opened the motor dial. Up here beside JogInput for the same
// reason: it is a parameter type of calEnterDial(), so Arduino's generated
// prototypes have to see it, and they are inserted just below.
//
// The dial is ONE screen with two doors — the calibration flow's Save list and
// the Motor Sensors diagnostic. The owner picks the title, the hint and the
// list to go back to, and nothing else: an operator who has seen it once has
// seen it both times. What it does NOT pick is whether anything is written,
// because the dial writes nothing either way — see calLeaveDial().
enum class DialOwner : uint8_t { CalFlow, Diagnostic };

// What a color sensor would have answered — the shape of ColorReading, minus
// the parts no screen here shows, plus the four raw channels the drill-down
// prints. `rc` is liveRead()'s return: 0, or a negative I2C error code.
//
// Up here for the same reason as the two above: senCanned() returns one, so
// Arduino's generated prototypes have to have seen the type. Anything that
// becomes a parameter or a return type in this sketch belongs in this block.
struct SenCanned {
    int  rc;
    char color;     // 'W','Y','R','O','G','B', 'E' empty chamber, 'U' unusable
    char alt;       // the runner-up, which is what makes a confidence readable
    bool ok;        // classify() vouches for it
    int  conf;      // percent, already scaled the way the firmware scales it
    int  rgbw[4];
};

// ---------------------------------------------------------------------------
//  Forward declarations
// ---------------------------------------------------------------------------
extern const MenuScreen kScreenNav;
extern const MenuScreen kScreenScreens;
extern const MenuScreen kScreenOps;
extern const MenuScreen kScreenCube;
extern const MenuScreen kScreenMsg;
extern const MenuScreen kScreenPatterns;
extern const MenuScreen kScreenModes;

extern const MenuScreen kScreenDiag;
extern const MenuScreen kScreenTuning;
extern const MenuScreen kScreenServoTune;
extern const MenuScreen kScreenThree;
extern const MenuScreen kScreenFour;
extern const MenuScreen kScreenFive;
extern const MenuScreen kScreenLong;
extern const MenuScreen kScreenDeep;

static void actLoad();
static void actEject();
static void actReport();
static void actInputReport();
static void actColorSensors();
static void actMotorSensors();
static void actFaultLog();
static void actCubeState();
static void actDemoInfo();
static void actDemoSteps();
static void actDemoChips();
static void actCalMotors();
static void actDemoError();
static void actDemoSolveDone();
static void actDemoEject();
static void actDemoScramble();
static void actStepSolve();
static void actStats();
static void actIdleMode();
static void actDemoMode();
static void actDemoNetSolved();
static void actDemoNetScrambled();
static void actDemoNetLoad();
static void actDemoSolveConfirm();
static void actFoldPattern();
static void actJog();
static void actTopServo();
static void actBotServo();
static void actRingPos();
static void actFaceMot();
static void actAlignPar();
static void actColorPar();
static void actResetTune();

// ---------------------------------------------------------------------------
//  Menu tables
// ---------------------------------------------------------------------------
//  Between them these cover every visual state the theme has: all six frame
//  colors, screens of three, four and five items, captions of realistic
//  length, preview lists and the placeholder graphic, labels long enough to
//  ellipsise, and a stack deep enough to hit CubeMenu's depth limit.
//
//  COLOR
//  -----
//  A branch is one color from the row that opens it down through everything it
//  leads to, so a row omits `theme` unless it genuinely LEAVES its branch —
//  which is what makes a branch one color by construction instead of by every
//  row agreeing. Five root rows, five colors:
//
//      Load Cube      BLUE     the main line of work, as in the firmware
//      Eject Cube     GREEN    as in the firmware
//      Actuators      VIOLET   this sketch's own branch; the firmware files
//                              the same jog page under Settings' yellow
//      Screens        PURPLE   drawing only — but see below
//      Diagnostics    YELLOW   all of it is the firmware's Settings subtree
//
//  Under Screens the rows DO name a theme, nearly all of them, and that is the
//  one place in this file where saying it is right: each of those rows opens a
//  rehearsal of a firmware screen, so the color it has to wear is the color the
//  MACHINE gives it — Modes red, Cube Views yellow, Stats purple. Wearing the
//  parent's color instead would photograph the right screen in the wrong frame,
//  which is the one thing a rehearsal must not do.

static const char* const kPrevNav[]     = { "3 items", "4 items", "5 items", "Long" };
static const char* const kPrevScreens[] = { "Operations", "Modes", "Cube views",
                                            "Messages", "Navigation" };

static const char* const kPrevOps[]   = { "Faces", "Chips", "Motor cal",
                                          "Solved!", "Eject" };
static const char* const kPrevModes[] = { "Scramble", "Idle", "Demo", "Step",
                                          "Patterns" };
static const char* const kPrevMsg[]   = { "Info", "Error", "Stats" };

// A canned solution for the Step Solve screen. Real notation, real length —
// twenty-one moves is what the solver typically returns — so the ribbon is
// exercised at the size it will actually see.
static const char* const kStepMoves[21] = {
    "R", "U2", "F'", "L", "D", "B2", "R'", "U", "F2", "L'", "D2",
    "B", "R2", "U'", "F", "L2", "D'", "B'", "R", "U2", "F'",
};

// The demo solution's OWN length, rather than a literal written out at each of
// the screens that quote it: the solve confirm, the solve result and the two
// "Solved!" frames all report the same number because they all read this one.
// It lives up here with the table it counts, so it is available to every demo
// below rather than only to the ones that happen to follow it.
static const int kDoneMoves = (int)(sizeof(kStepMoves) / sizeof(kStepMoves[0]));

// Thirty moves, and no two in a row on the same face — which is what a real
// scramble looks like, and the reason it reads differently from a solution.
// A separate list so Demo Mode's two halves do not look like the same thing
// twice.
//
// Sized by the firmware's own constant, not a local one, so this rehearsal
// cannot quietly run a different length from the scramble the machine
// generates — one entry too many here is a compile error.
static const char* const kScrambleMoves[CubeSystem::kScrambleLen] = {
    "D2", "L",  "B'", "R",  "U'", "F2", "D",  "L2", "B",  "R'",
    "U2", "F",  "D'", "L'", "B2", "R2", "U",  "F'", "D",  "L",
    "B",  "R",  "U2", "F2", "D'", "L2", "B'", "R2", "U'", "F",
};
static const char* const kPrevDiag[] = { "Tuning", "Input", "Color", "Motor",
                                         "Faults" };
static const char* const kPrevTuning[]  = { "Servos", "Face motors", "Alignment",
                                            "Color" };
static const char* const kPrevServoT[]  = { "Top", "Bottom", "Ring" };

// Canned fault history, in the FIRMWARE's row format: `when` is uptime at the
// fault (the machine has no RTC, so seconds-since-boot is the only clock it
// can honestly report — newest first, so the times run downwards), `what` is
// the SOURCE — which code space the fault's number belongs to, the thing that
// keeps a bare 25 decodable — and the codes are real ones from the error
// tables and the README. The source names come from CubeFaultLog itself, so
// this rehearsal cannot drift from the rows the machine draws.
struct FaultEntry { const char* when; const char* what; int code; };
static const FaultEntry kFaults[] = {
    { "3h12m",  CubeFaultLog::kSourceNames[CubeFaultLog::Solve], 122 },
    { "3h05m",  CubeFaultLog::kSourceNames[CubeFaultLog::Scan],   60 },
    { "2h58m",  CubeFaultLog::kSourceNames[CubeFaultLog::Mode],   25 },
    { "2h44m",  CubeFaultLog::kSourceNames[CubeFaultLog::Scan],   42 },
    { "2h30m",  CubeFaultLog::kSourceNames[CubeFaultLog::Scan],   81 },
    { "2h12m",  CubeFaultLog::kSourceNames[CubeFaultLog::Solve], 125 },
    { "1h58m",  CubeFaultLog::kSourceNames[CubeFaultLog::Scan],   13 },
    { "1h41m",  CubeFaultLog::kSourceNames[CubeFaultLog::Cal],     8 },
    { "1h29m",  CubeFaultLog::kSourceNames[CubeFaultLog::Scan],   70 },
    { "1h02m",  CubeFaultLog::kSourceNames[CubeFaultLog::Mode],   22 },
    { "47m10s", CubeFaultLog::kSourceNames[CubeFaultLog::Jog],    24 },
    { "31m05s", CubeFaultLog::kSourceNames[CubeFaultLog::Cal],    90 },
    { "15m44s", CubeFaultLog::kSourceNames[CubeFaultLog::Solve], 121 },
    { "0m52s",  CubeFaultLog::kSourceNames[CubeFaultLog::Scan],   41 },
};
static const int kFaultCount = (int)(sizeof(kFaults) / sizeof(kFaults[0]));

static const char* const kPrevCube[]    = { "Solved", "Scrambled", "Load",
                                            "Confirm", "State" };
static const char* const kPrevDeep[]    = { "Deeper", "and", "deeper" };

// ---- root ----
//
// Blue on the screen itself for the same reason the firmware's two main menus
// are blue: it is the color of the main line of work, and it is what the frame
// falls back to if a row ever stops naming one.
static const MenuItem kMainItems[] = {
    { "Load Cube",   nullptr,         actLoad,  "Clamp the cube. Moves servos.",
      nullptr, 0, MenuTheme::Blue },
    { "Eject Cube",  nullptr,         actEject, "Release and present it.",
      nullptr, 0, MenuTheme::Green },
    { "Actuators",   nullptr,         actJog,   "Drive every part by hand.",
      nullptr, 0, MenuTheme::Violet },
    { "Screens",     &kScreenScreens, nullptr,  "Draw the panel, no hardware.",
      kPrevScreens, 5, MenuTheme::Purple },
    // previewCount is the length of kPrevDiag, not a number picked by eye: a
    // stale one silently shows fewer entries than the pane was written for.
    { "Diagnostics", &kScreenDiag,    nullptr,  "Tuning, sensors and faults.",
      kPrevDiag, 5, MenuTheme::Yellow },
};
static const MenuScreen kScreenMain = { "Menu Test", kMainItems, 5, MenuTheme::Blue };

// ---------------------------------------------------------------------------
//  Diagnostics
// ---------------------------------------------------------------------------
// No row here names a theme. The whole branch is the firmware's Settings
// yellow — Sensor Test, Fault Log and Parameters all live under it there — and
// inheriting is what makes that true by construction rather than by five rows
// agreeing.
static const MenuItem kDiagItems[] = {
    { "Tuning",         &kScreenTuning, nullptr,    "Values you can change.",
      kPrevTuning, 4 },
    { "Input Report", nullptr,     actInputReport, "Live wheel and buttons." },
    { "Color Sensors", nullptr,   actColorSensors, "Live, per board." },
    { "Motor Sensors",  nullptr,   actMotorSensors,  "Raw encoder angles." },
    { "Fault Log",      nullptr,   actFaultLog,      "What went wrong, recently." },
};
const MenuScreen kScreenDiag = { "Diagnostics", kDiagItems, 5, MenuTheme::Yellow };

// Navigation lives under Screens rather than here: it exercises the menu
// RENDERER at every item count and tells you nothing about the machine, which
// is what everything under Diagnostics is for.
//
// Reset Defaults inherits yellow with its siblings, as in the firmware: the
// warning it needs is on its confirm screen, which is an Op::Error and
// therefore red however this row is themed — and one red row inside a yellow
// branch would read as "you are somewhere else", which is the only thing a
// color change is allowed to say.
static const MenuItem kTuningItems[] = {
    { "Servos",       &kScreenServoTune, nullptr, "Positions set by eye.",
      kPrevServoT, 3 },
    { "Face Motors",  nullptr, actFaceMot,  "Speed and settling time." },
    { "Alignment",    nullptr, actAlignPar, "How square is square enough." },
    { "Color",        nullptr, actColorPar, "How a sticker is judged." },
    { "Reset Defaults", nullptr, actResetTune, "Throw away every change." },
};
const MenuScreen kScreenTuning = { "Tuning", kTuningItems, 5, MenuTheme::Yellow };

// The three that move something you can watch, kept together and away from the
// numbers that only take effect on the next move. Still yellow: driving a horn
// is not a different PLACE from reading the number that drives it.
static const MenuItem kServoTuneItems[] = {
    { "Top Servo",    nullptr, actTopServo, "Grips from above." },
    { "Bottom Servo", nullptr, actBotServo, "Grips, centres and ejects." },
    { "Ring",         nullptr, actRingPos,  "Stepper, not a servo." },
};
const MenuScreen kScreenServoTune = { "Servos", kServoTuneItems, 3, MenuTheme::Yellow };

// ---- navigation: one screen per item count ----
//
// Five items pack tighter than four or fewer (different start and pitch), so
// both layouts need looking at on the real panel.
//
// The one subtree in this sketch that is deliberately NOT wayfinding: its five
// child screens are five different colors, and with this screen's own red that
// is all six on one walk. Somewhere has to draw every color for comparison, and
// a subtree that renders the menu at every item count and says nothing about
// the machine is the honest place to do it — there is no branch here to be lost
// inside of.
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
//
// Split three ways rather than crammed into one screen: five items is the
// design limit, and this was already at it. A screen that wants a sixth wants
// splitting, which is the same rule the real menu follows.
//
// Every row names a theme, and every one of them is the color the FIRMWARE
// gives the screens behind it: Operations blue (the main line of work), Modes
// red, Cube Views yellow (Cube State and the calibration prompt both live under
// Settings there). Navigation is the color sampler and keeps its red. Messages
// alone inherits, because its three demos come from three different branches
// and no one color would be honest about all of them — the rows below carry
// them instead.
static const MenuItem kScreensItems[] = {
    { "Operations",  &kScreenOps,  nullptr, "Progress while working.",
      kPrevOps,  5, MenuTheme::Blue },
    { "Modes",       &kScreenModes, nullptr, "The five ways to run it.",
      kPrevModes, 5, MenuTheme::Red },
    { "Cube Views",  &kScreenCube, nullptr, "The unfolded cube net.",
      kPrevCube, 5, MenuTheme::Yellow },
    { "Messages",    &kScreenMsg,  nullptr, "Status, faults and records.",
      kPrevMsg,  3 },
    { "Navigation",  &kScreenNav,  nullptr, "Screens of every size.",
      kPrevNav,  4, MenuTheme::Red },
};
const MenuScreen kScreenScreens = { "Screens", kScreensItems, 5, MenuTheme::Purple };

// Deliberately the SAME five items, in the same order and now the same five
// colors, as the firmware's Modes menu. This sketch is the rehearsal for that
// tree, and a demo that groups the screens differently from the machine stops
// being a rehearsal of anything.
//
// Modes is the branch that fans out: the screen is red, but each mode owns a
// color and keeps it through every screen that mode draws, because a running
// mode is somewhere you can be for minutes and the five are five different
// things that happen to be filed together. Red stays the screen's own and
// Scramble Solve's, as the mode this menu was named after.
static const MenuItem kModesItems[] = {
    { "Scramble Solve", nullptr, actDemoScramble, "Scramble, then solve it.",
      nullptr, 0, MenuTheme::Red },
    { "Idle Mode",      nullptr, actIdleMode,     "Awake, waiting, worth a glance.",
      nullptr, 0, MenuTheme::Yellow },
    { "Demo Mode",      nullptr, actDemoMode,     "Scramble and solve, looping.",
      nullptr, 0, MenuTheme::Blue },
    { "Step Solve",     nullptr, actStepSolve,    "One move per press.",
      nullptr, 0, MenuTheme::Purple },
    { "Patterns",       &kScreenPatterns, nullptr, "Previews in the side pane.",
      nullptr, 0, MenuTheme::Green },
};
const MenuScreen kScreenModes = { "Modes", kModesItems, 5, MenuTheme::Red };

// Pattern previews come from CubeSystem::kPatternNets, the tables the firmware
// folds from: a second copy here is a copy that can be computed in a different
// frame from the one the model builds, and once was.
//
// The point of this screen: the preview pane shows what each pattern PRODUCES.
// A list of names would say nothing about what you are choosing between.
//
// Item order IS table order: each row's previewNet indexes the shared
// kPattern* tables by position, and actFoldPattern() reuses selectedIndex()
// the same way — the same contract as the firmware's Patterns screen, which
// this one rehearses row for row.
//
// The per-item themes here are NOT wayfinding — they are four different colors
// so four rows of preview net do not all sit in the same frame. The branch
// color is the screen's green, and actFoldPattern() sets that explicitly so a
// fold's operation screens do not inherit whichever row was picked.
static const MenuItem kPatternItems[] = {
    { "Checkerboard", nullptr, actFoldPattern, "U2 D2 R2 L2 F2 B2",
      nullptr, 0, MenuTheme::Blue,   CubeSystem::kPatternNets[0] },
    { "Cube in Cube", nullptr, actFoldPattern, "Fifteen moves.",
      nullptr, 0, MenuTheme::Green,  CubeSystem::kPatternNets[1] },
    { "Six Spot",     nullptr, actFoldPattern, "U D' R L' F B' U D'",
      nullptr, 0, MenuTheme::Yellow, CubeSystem::kPatternNets[2] },
    { "Superflip",    nullptr, actFoldPattern, "Every edge flipped.",
      nullptr, 0, MenuTheme::Purple, CubeSystem::kPatternNets[3] },
};
const MenuScreen kScreenPatterns = { "Patterns", kPatternItems, 4, MenuTheme::Green };

// The ones that are not modes: what the machine draws while it is scanning,
// while it is learning colors, while it is being squared up before it can learn
// either, and the two screens a plain Solve and an Eject now END on. The modes
// moved to Modes; what is left here is every top-level operation.
//
// Full at five, which is the design limit — a sixth wants a split, the same
// rule the real menu follows. That limit is why the solve CONFIRM screen is
// filed under Cube Views rather than here beside Solve Result: it is a cube net
// with a line under it, which is what every screen on that page is.
//
// Blue is the branch, and the two rows that leave it say so: color capture and
// motor squaring are Calibration screens on the machine, which is yellow, and
// the eject prompt is the green Eject row's.
static const MenuItem kOpsItems[] = {
    { "Scan Faces",   nullptr, actDemoSteps,     "Faces fill as they are read." },
    { "Color Chips",  nullptr, actDemoChips,     "Two boards, six colors.",
      nullptr, 0, MenuTheme::Yellow },
    { "Motor Cal",    nullptr, actCalMotors,     "Square each face, then save.",
      nullptr, 0, MenuTheme::Yellow },
    { "Solve Result", nullptr, actDemoSolveDone, "How a plain solve ends." },
    { "Eject Prompt", nullptr, actDemoEject,     "Take it, no press needed.",
      nullptr, 0, MenuTheme::Green },
};
const MenuScreen kScreenOps = { "Operations", kOpsItems, 5, MenuTheme::Blue };

// Every screen built on the cube net, together — which is how the shared piece
// is filed in SCREEN_PLAN, and the only page where four nets can be compared
// against each other for facelet order and label placement.
//
// Yellow is the branch: the stored-state views are Diagnostics > Cube State on
// the machine and the orientation prompt is Calibration's, both under Settings.
// Solve Confirm is the one that leaves — it belongs to the blue Solve, and it
// is worth seeing that the same net reads differently inside a blue frame.
static const MenuItem kCubeItems[] = {
    { "Solved Cube",     nullptr, actDemoNetSolved,    "Every face one color." },
    { "Scrambled Cube",  nullptr, actDemoNetScrambled, "Checkerboard, 9 of each." },
    { "Load Orientation",nullptr, actDemoNetLoad,      "The calibration prompt." },
    { "Solve Confirm",   nullptr, actDemoSolveConfirm, "The net before it runs.",
      nullptr, 0, MenuTheme::Blue },
    { "Cube State",      nullptr, actCubeState,        "Turn the model, not the cube." },
};
const MenuScreen kScreenCube = { "Cube Views", kCubeItems, 5, MenuTheme::Yellow };

// Three demos from three different branches, so all three rows say which:
// an info panel is a Settings screen, the error look is red wherever it
// happened, and Stats is its own top-level row on the machine. The screen
// inherits Screens' purple, which is Stats' color and the closest thing to a
// majority here.
static const MenuItem kMsgItems[] = {
    { "Info Panel",   nullptr, actDemoInfo,  "Aligned label/value rows.",
      nullptr, 0, MenuTheme::Yellow },
    { "Error Screen", nullptr, actDemoError, "The red stopped look.",
      nullptr, 0, MenuTheme::Red },
    { "Stats",        nullptr, actStats,     "Solve records.",
      nullptr, 0, MenuTheme::Purple },
};
const MenuScreen kScreenMsg = { "Messages", kMsgItems, 3, MenuTheme::Purple };

// ---------------------------------------------------------------------------
//  Display helpers
// ---------------------------------------------------------------------------
// Every screen this sketch draws while it is "working" goes through here, so
// all of them get the same frame, the same title position and the same hint bar
// as the menu — and, since the frame band became wayfinding, the color of the
// branch that was walked down to reach it.
//
// That second job is why nothing else in this file calls showOperation(). One
// funnel, one setOpTheme(), and a demo added later cannot forget to wear its
// branch's color. `kind` still names what sort of screen this is; on an Error
// it is also what keeps the frame red, because setOpTheme() refuses to repaint
// an Error screen.
//
// No displayUpdate() here: the decorated screens set rows, chips, nets or a
// ribbon before they flush, and flushing twice costs a full repaint.
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

    // Re-send the whole frame, as the firmware's toMenu() does: Load and Eject
    // move real servos under an operation screen, and the differential panel
    // driver cannot see what that did to the glass. See CubeDisplay::repaintAll.
    cubeDisplay.repaintAll();
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
//  Jog pages — direct actuator control
// ---------------------------------------------------------------------------
//  A menu is the wrong shape for this. Driving a servo through Extend, Partial
//  and Retract is nine menu entries across three screens once the ring is in
//  too, and the face motors are twelve more — all of it clicked through one
//  item at a time, when what you actually want is to pick a thing and nudge it
//  while you watch it move.
//
//  So this page is direct manipulation instead, and it can be because the
//  wheel and the UP/DOWN BUTTONS are separate inputs on this encoder. The
//  menu collapses them into one meaning; here they get two:
//
//      wheel        choose which part
//      UP / DOWN    move that part
//      LEFT         back, exactly as everywhere else
//
//  One selector over the whole machine: three grippers, the load/eject row,
//  six face motors, two whole-cube rotations — twelve rows, no submenus, and
//  the wheel wraps, so nothing is more than six detents away. One page is not
//  only tidiness: a face motor cannot turn until the grippers are clear, so
//  seeing where the grippers are WHILE jogging a face is the difference
//  between a considered press and a jam.
//
//  Two levels either way, but a gripper POSITION is a choice and a face turn
//  is not. Scroll to a servo or the ring, SELECT to enter it, wheel to pick
//  the position, SELECT again to send. Scroll to a face or a rotation, SELECT
//  to take the wheel, and from then on every detent (or UP/DOWN) IS a turn —
//  a turn you want to repeat should not cost three presses.
//
//  The band is wayfinding and stays this branch's violet the whole time the
//  page is up, so "I am about to move something" cannot be a frame color. It
//  is CubeDisplay::setOpArmed() instead — a breathing ARMED badge on the title
//  line, backed by the hint bar and the row's own "< Extend >" marks: a shape
//  that appears and a motion that continues survive a frame colour that does
//  not change.
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
// it. Eject was the one missing: the bottom servo parks there after every
// unload, and this page could neither send it nor name it. Mirrored from the
// firmware's jog page, table for table — change both.
//
// The order is DECLARATION order and deliberately NOT travel order, which looks
// wrong on the bottom servo: its Eject is tuned to 120 deg against Partial's
// 195, so Eject sits physically BELOW Partial and the wheel does not walk the
// horn monotonically down the list.
//
// Travel order was rejected because it is not a constant. It is a property of
// four editable numbers, so it would re-sort itself the moment somebody changed
// one under Tuning > Servos — the same row would mean a different pose between
// two visits, on the one page whose job is saying where a part IS. This order
// is fixed, it is the order the ring's own tuning section already lists, and
// every row is labelled with the pose it sends.
//
// The top servo has no Partial and no Eject tuning row, so ejectTarget() falls
// back to partialTarget() and its two middle rows send the horn to the SAME
// ANGLE. Left visible rather than collapsed into one row: the coarse state each
// leaves behind IS distinct, so the row reads back whichever was actually sent.
static const int kJogPos = 4;                          // stops per gripper
static const char* const kGripPos[3][kJogPos] = {
    { "Retract", "Partial", "Eject",  "Extend" },
    { "Retract", "Partial", "Eject",  "Extend" },
    { "Retract", "Partial", "Middle", "Extend" },
};

// Where each gripper is, as an index into the row above. -1 renders as "?".
//
// Seeded from the machine itself on every entry to this page —
// CubeServo::coarseState() and CubeMotors::getRingState(), both restored from
// EEPROM at boot — so the page opens telling the truth instead of three
// question marks. RE-READ on entry, never cached across visits: a tuning
// preview under Diagnostics moves a horn without telling this page, so a value
// latched last time would assert a position that stopped being true.
static int8_t gripAt[3] = { -1, -1, -1 };

// Translate a gripper's own state numbering into this page's four stops.
//
// The servo does not number them the way the rows do: it calls extended 1,
// partial 2 and ejected 3, while the rows read Retract / Partial / Eject /
// Extend. Mapping is cheaper than renumbering, because the servo's value is
// persisted in EEPROM and a renumber would misread every machine in the field.
// Mirrored from the firmware's gripStateOf() — change both.
static int8_t gripStateOf(const CubeServo& s) {
    switch (s.coarseState()) {
        case 0:  return 0;      // retracted
        case 1:  return 3;      // extended
        case 2:  return 1;      // partial
        case 3:  return 2;      // ejected
        default: return -1;     // mid-sweep, aborted, or moved by a preview
    }
}

// Face moves come from CubeSystem::kFaceMoves — the firmware's own table,
// not a copy, so the notation this page sends cannot drift from the grammar
// the machine parses. Columns 0-1 are plain and prime; the jog wheel has no
// gesture for a double turn.

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

    // One kind throughout: the frame carries the BRANCH now, so flipping
    // Calibrate to Info while a gripper is entered would recolor nothing and
    // only misname the screen.
    opScreen(Op::Calibrate, "Actuators", nullptr, hint);

    // The badge, and it has to come AFTER opScreen(): showOperation() clears
    // it, so a page that repaints on every interaction must re-assert it or it
    // flickers out. It stays up while a move runs, because then the machine is
    // not merely armed, it is going, and dropping the badge mid-move would read
    // as safe.
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

    // Mirrored from the firmware's jog page — change both. A jogged turn is
    // untracked (moveVirtual is off, this page cannot promise a ready model),
    // so a model that IS ready stops describing the cube the moment the motor
    // moves and must be wiped before it. Never true in this sketch — nothing
    // here scans — but the two pages are the same code and must not drift.
    if (Cube.virtualCube.isReady()) {
        Cube.virtualCube.resetCube();
        Cube.clearSolution();
    }

    drawJog(mv);
    // align = true: this is the screen for checking a motor lands on its
    // detent, so let the alignment pass run and report if it cannot.
    const int e = Cube.executeMove(mv, false, true);
    if (e) {
        char sub[48];
        snprintf(sub, sizeof(sub), "%s  -  code %d", mv, e);
        showScreen(Op::Error, "Actuators", "Move failed");
        cubeDisplay.setStatus(sub);
        Cube.displayUpdate();
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
        gripAt[0] = gripAt[1] = gripAt[2] = 3;      // all extended
    } else {
        drawJog("releasing");
        Cube.unloadCube();      // ring, then top, then bottom - all retracted
        Cube.botServoEject();   // then present the cube, at the tuned height
        // Mirrored from the firmware's jog page — change both. Out of the
        // machine's grip the stored state is a guess, so it goes with the
        // cube. Never true here — this sketch does not scan — but the two
        // pages must not drift.
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
    jogSel   = 0;
    jogArmed = false;

    // Ask the machine where it is, ON ENTRY — the firmware's actJog() does the
    // same and for the same reason. All three parts persist their state to
    // EEPROM and Cube.begin() has already acted on it, so after a clean boot
    // this opens on Retract/Retract/Retract rather than "?".
    gripAt[0] = gripStateOf(topServo);
    gripAt[1] = gripStateOf(botServo);
    switch (cubeMotors.getRingState()) {
        // The ring's numbering is NOT this page's row order, which is why every
        // case is written out. CubeMotors numbers its stops in the order they
        // were ADDED — 0 retracted, 1 halfway, 2 extended, 3 partial, partial
        // having arrived last — while the rows run Retract / Partial / Middle /
        // Extend. That lone 3 in the middle of the switch looks like a typo and
        // is not: arithmetic or a tidying renumber here is a silent bug that
        // reports the ring one stop away from where it is standing.
        case 0:  gripAt[2] = 0; break;      // retracted -> Retract
        case 3:  gripAt[2] = 1; break;      // partial   -> Partial
        case 1:  gripAt[2] = 2; break;      // halfway   -> Middle
        case 2:  gripAt[2] = 3; break;      // extended  -> Extend
        // -1 is the in-motion sentinel, only observable after a move that was
        // aborted or lost power. Honestly unknown, so it stays "?".
        default: gripAt[2] = -1; break;
    }

    state    = TState::Jog;
    drawJog(nullptr);
}

// ---------------------------------------------------------------------------
//  Tuning — the value editor
// ---------------------------------------------------------------------------
//  The one interaction the menu cannot express: the wheel has to change a
//  NUMBER, not move a cursor. Two levels, the same shape the grippers on the
//  Actuators page use — scroll to a row, SELECT to enter it, wheel to change,
//  SELECT to keep or LEFT to put it back.
//
//  Kept out of CubeMenu deliberately. CubeMenu is navigation-only and testable
//  on a host without a screen; editing values is a different job.
//
//  The parameter table itself lives in the library — CubeTuneTable — because
//  the index is the EEPROM slot and the firmware reads the same block: two
//  per-sketch copies could drift by a row and silently hand values to the
//  wrong owners, which is why the table is shared and only this editor UI is
//  duplicated. (CubeTuneTable.h explains the accessors and why the order is
//  frozen.)
static const TuneSection* parSec  = nullptr;
static int8_t             parSel  = 0;
static bool               parEdit = false;
static int32_t            parWas  = 0;   // value on entering edit, for LEFT
static bool               parGate = false;  // showing the confirm for a gated row
static bool               parMoved = false; // a live row moved a servo

//  THE APPLY GATE, and the one thing it must not break
//  ---------------------------------------------------
//  A parameter is easy to change by accident here — one press and one detent —
//  so nothing is written until the Apply row: a machine that quietly kept the
//  accident would offer no way back to the number that worked.
//
//  What is gated is the COMMIT, never the preview. Six rows are TP_LIVE and a
//  servo endpoint is set BY EYE: you turn the wheel and watch the horn follow.
//  Gating that would make servo tuning impossible, so preview() still runs on
//  every single detent, ungated — which is what TuneParam::preview was split
//  away from set() for. (A change too large to have come from a wheel is SWEPT
//  rather than written straight out, so the first detent in a row moves the
//  horn instead of snapping it there.) While a section is open the owners and
//  EEPROM still hold parBase[], the wheel moves parVal[], and set() plus
//  tuneSaveAll() are reached from exactly ONE place: the Apply row.
//
//  The consequence worth naming out loud: a previewed servo is PHYSICALLY
//  standing at a value the machine does not otherwise know about. So discarding
//  is not a matter of forgetting numbers — parDiscard() drives every previewed
//  part back to its base first. And LEFT, with edits pending, ASKS in red
//  rather than choosing silently between saving and throwing away.
static const int kParMaxRows = CubeDisplay::kOpLines;  // six rows plus Apply
static int32_t   parBase[kParMaxRows];  // what the machine and EEPROM hold
static int32_t   parVal[kParMaxRows];   // what the wheel has been moving
static bool      parSaved = false;      // "Saved" on the hint until the next
                                        // press, so an Apply whose rows all
                                        // look the same afterwards still says
                                        // that it did something

// The Apply row sits AFTER the section's own rows. Last, because it is the end
// of the job, and because row 0 is where the cursor opens and a write to EEPROM
// should not be the thing one press away.
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
            // Angle brackets in plain ASCII: the baked fonts carry 0x20-0x7F
            // and nothing else, and a missing glyph draws as an empty box
            // without a word of complaint.
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

    // The band is wayfinding and stays Diagnostics' yellow while editing, so
    // "you are changing something" is carried by the hint bar and the row's
    // own "< 1450 >" marks rather than by a frame color.
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
    // Neither line promises more than the code does. The non-live half says
    // Apply, because nothing this editor does reaches the machine before it.
    // The live half says the part MOVES rather than that it follows the wheel
    // "at once": previewRaw() writes a wheel-sized change straight out but
    // SWEEPS anything larger — and the first detent in a row is always larger,
    // because parVal is seeded from the stored endpoint rather than from where
    // the horn is standing, so the opening move can be a full pumped travel of
    // a couple of seconds. Promising "at once" would make that read as a hang.
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
    state    = TState::Params;
    drawTune();
}

static void actTopServo() { tuneEnter(&kSecTopServo); }
static void actBotServo() { tuneEnter(&kSecBotServo); }
static void actRingPos()  { tuneEnter(&kSecRing);     }
static void actFaceMot()  { tuneEnter(&kSecFaces);    }
static void actAlignPar() { tuneEnter(&kSecAlign);    }
static void actColorPar() { tuneEnter(&kSecColor);    }

// Reset is gated like the hardware rows are, and for the same reason: it is the
// one action here that cannot be undone by turning the wheel back. The servos
// are NOT driven to their default positions afterwards - the values are what
// reset, and moving three parts at once because a menu item was picked would be
// a much bigger surprise than a stale horn.
static bool resetConfirm = false;

static void actResetTune() {
    resetConfirm = true;
    state = TState::Screen;
    live  = Live::None;
    opScreen(Op::Error, "Reset Defaults",
             "Discard all tuning?",
             "SELECT to reset - LEFT to keep");
    cubeDisplay.setStatus("Every value goes back to compiled");
    Cube.displayUpdate();
}

// The horn is wherever the last preview left it, and CubeServo::begin() trusts
// the stored POSITION to decide how far its first sweep travels — so a stale one
// is what arms a full-travel slam on the next power-up. Written once, at the
// end, rather than once per detent. It stores where the servo is STANDING, not
// what its endpoints are, so it is right on the way out of an Apply and equally
// right on the way out of a discard.
static void parPersistHorns() {
    if (parMoved) {
        topServo.persist();
        botServo.persist();
        parMoved = false;
    }
}

// Leaving a section, with nothing left pending.
//
// No tuneSaveAll() here: leaving is not a decision to keep anything — the Apply
// row is, see the apply-gate essay above — and every path that reaches here has
// either applied, discarded, or had nothing to apply.
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
        // setters clamp, and a base left disagreeing with its owner by one
        // degree would leave the row starred for ever and Apply for ever
        // offering to save it again.
        parVal[i] = parBase[i] = p.get();
    }

    parPersistHorns();

    // One block for all the rows, this section's or not — EEPROM.update() means
    // the rows nothing touched cost no write at all.
    tuneSaveAll();
    parSaved = true;
    drawTune();
}

// The Apply row's second job: put every row of THIS page at its compiled
// default — as PENDING values, never as a write.
//
// Reset Defaults under Settings > Parameters clears the whole EEPROM block
// and re-applies all 28 defaults at once, so there was no way to put one
// section back without losing the tuning of the other five. This is that
// way. It goes through the Apply gate like every other change, for the reason
// every other change does: the stars show exactly which rows a press of Apply
// would alter, and LEFT still asks before the proposal is thrown away.
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
// its part to the previewed angle — that is the entire point of those rows — so
// the horn is physically standing at a value that is about to stop existing
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
// one SELECT gives — the same way round as every other confirm here — and LEFT
// puts you back on the list, one row away from Apply.
//
// A TState of its own rather than another flag beside parGate: this one is
// answered with MenuEvents from pollEvent(), as the firmware's AppState::
// ParamsLeave is, because nothing on it needs the wheel separated from the
// buttons.
static void drawParamsLeave() {
    const int n = parDirtyCount();
    char head[64];
    snprintf(head, sizeof(head), "Discard %d change%s?", n, (n == 1) ? "" : "s");

    opScreen(Op::Error, parSec->title, head, "SELECT discards - LEFT goes back");
    cubeDisplay.setStatus("Apply, on the last row, keeps them");
    Cube.displayUpdate();
    state = TState::ParamsLeave;
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
//
// Not showScreen(): that stamps its own "SELECT or LEFT to go back", and on
// this page SELECT, LEFT and RIGHT are three of the five buttons the operator
// came here to press. The chord is the only way out — see updateInputReport().
static void actInputReport() {
    lastLive  = 0;
    live      = Live::Input;
    liveStart = millis();
    state     = TState::Screen;
    opScreen(Op::Info, "Input Report", nullptr, "Hold SELECT+LEFT to leave");
    Cube.displayUpdate();
}

// Returns true while SELECT+LEFT is held, which is this page's ONLY way out.
// The detection has to happen here rather than from a MenuEvent because
// pollEvent() deliberately swallows the chord (it is not a menu gesture), and
// because every single-button event this page could exit on is a button the
// operator came here to press. This function already samples all five levels in
// one transaction, so the chord costs nothing extra.
static bool updateInputReport() {
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
    // that lights while it is held. Not a word, and not two buttons to a line:
    // a stuck button is a light that never goes out, seen at a glance, where a
    // word that never changes has to be read.
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
    // takes the cursor yellow this theme uses for "live". The chord is named in
    // the hint bar, where every other screen says what the buttons do.
    cubeDisplay.setOpLines(lines, 6, nullptr, values);
    Cube.displayUpdate();

    // Level, not edge: pressing two buttons on the same 25 ms tick is a
    // coincidence, not a gesture. Both down on any one tick is the chord.
    return (b & RotaryEncoder::BTN_SELECT) && (b & RotaryEncoder::BTN_LEFT);
}

// ---------------------------------------------------------------------------
//  Sensor screens
// ---------------------------------------------------------------------------
//  Split in two because they answer different questions. The color boards want
//  "is any sensor disagreeing with its neighbours", which is a picture. The
//  motor encoders want "what angle is each one reading", which is a list of
//  numbers — and, because a diagnostic that can only watch is half a
//  diagnostic, a wheel to turn one of them with.
//
//  NOTHING HERE READS A SENSOR, TOUCHES THE MUX OR MOVES A MOTOR. Every value
//  below is canned. What the firmware does in their place, so the two can be
//  compared line by line:
//
//      colorSensorN.liveRead(i, rgbw, &r)   holds the mux channel and the lamp
//                                           between calls, so re-reading the
//                                           sensor already selected is nearly
//                                           free and only a change of channel
//                                           costs an integration window
//      MotorEncoders[i]->scan()             a raw 12-bit angle, or a negative
//                                           I2C error code
//      CubeSystem::homeMotors()             drives all six faces to their
//                                           nearest detent
//
//  The shape rehearsed here is liveRead()'s: a grid that fills itself in about
//  three seconds and then costs nothing at all while the cursor sits still. A
//  scanSingle() read is ~900 ms of warm-up and integration spent INSIDE the
//  call, where pollEvent() does not run and the seesaw's instantaneous level
//  check misses any press that begins and ends in the window — a page built on
//  it feels broken as well as slow.

// ---- Color Sensors: two boards, eighteen cells ----------------------------
//
// Cursor, 0..17 across BOTH boards — the numbering CubeDisplay::setOpGrid()
// takes for its own cursor, so nothing has to take it apart except the code
// that decides what a sensor would have answered.
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
// sweep would never finish on a machine with no cube in it. Sensor 9 of board 1
// is canned as exactly that case, because it is the one overlap the two
// meanings of a hollow cell have and it is worth seeing.
static uint32_t       senSeen     = 0;
static const uint32_t kSenAllSeen = (1UL << 18) - 1;

// The two board captions, built once on entry. The firmware's come from
// boardHealthRow(), which is nine separations and nine health checks per board
// — not arithmetic to repeat sixteen times a second for an answer that cannot
// move while the page is up. Same reason to build them once from canned
// figures here: the rehearsal should cost what the screen costs.
static char senCapA[44], senCapB[44];

// The two ways a cell turns into kCellFault, one of each, so both halves of the
// drill-down are reachable without a broken machine to produce them:
//
//   board 2 sensor 2   answers cleanly and matches nothing it can vouch for.
//                      That is this machine's real fault — a dead green
//                      channel, see the README — and it is the screen doing
//                      its job rather than a bug in it.
//   board 2 sensor 9   does not answer at all: a negative return, which is a
//                      FAULT and never data. Its four channels print as dashes,
//                      because a failed transaction hands back zeros and four
//                      zeros classify as whichever reference is darkest.
static const int kSenDeadGreen = 10;   // board 2, sensor 2
static const int kSenNoAnswer  = 17;   // board 2, sensor 9
static const int kSenEmptySlot = 8;    // board 1, sensor 9 — 'E', read and hollow

// Deliberately deterministic rather than drifting: the firmware's grid stops
// changing the moment the sweep finishes and the cursor parks, and a rehearsal
// that kept shimmering would be showing a screen the machine does not draw.
static SenCanned senCanned(int idx) {
    SenCanned r = { 0, 'U', 'U', false, 0, { 0, 0, 0, 0 } };

    if (idx == kSenNoAnswer) {
        r.rc = -2;              // liveRead()'s "the transaction failed" code
        return r;
    }

    static const char kLetters[6] = { 'W', 'Y', 'R', 'O', 'G', 'B' };
    // A dominant channel per color, and a white channel that tracks the sum —
    // the shape a real reading has, which is what makes the four numbers worth
    // putting on a screen at all.
    static const int kBoost[6][3] = {
        { 700, 700, 700 }, { 800, 750, 200 }, { 900, 250, 220 },
        { 880, 480, 200 }, { 250, 780, 300 }, { 230, 300, 820 },
    };

    const int c = (idx * 5 + 1) % 6;        // no run of neighbours the same
    for (int k = 0; k < 3; ++k) r.rgbw[k] = kBoost[c][k];

    if (idx == kSenDeadGreen) {
        r.rgbw[1] = 0;          // the dead channel, and the whole reason this
        r.color   = 'U';        // sensor never resolves to a color
        r.alt     = kLetters[c];
        r.ok      = false;
    } else if (idx == kSenEmptySlot) {
        r.color = 'E';          // the calibrated empty-chamber reference: a
        r.alt   = 'W';          // real answer, not a fault
        r.ok    = true;
        r.conf  = 88;
        for (int k = 0; k < 3; ++k) r.rgbw[k] = 120 + k * 10;
    } else {
        r.color = kLetters[c];
        r.alt   = kLetters[(c + 1) % 6];
        r.ok    = true;
        // Spread across the range on purpose, low enough on some sensors to
        // show what a lucky right answer looks like beside a confident one.
        r.conf  = 38 + (idx * 7) % 58;
    }
    r.rgbw[3] = r.rgbw[0] + r.rgbw[1] + r.rgbw[2];
    return r;
}

// One reading as a grid cell. The two sentinels exist because "we have not
// asked this sensor yet" and "it answered nonsense" are opposite conclusions
// that used to share one hollow box. Mirrored from the firmware's senCellFor().
static int8_t senCellFor(const SenCanned& r) {
    if (r.rc != 0) return CubeDisplay::kCellFault;   // negative is never data
    if (!r.ok)     return CubeDisplay::kCellFault;
    // 'E' has no chip color, so chipIndexForColor() returns -1 — which IS
    // kCellUnread. An empty machine therefore reads as a grid of empty cells,
    // which is what an empty machine looks like.
    return CubeSystem::chipIndexForColor(r.color);
}

// Color letter -> name for the "Reads as" row. 'E' is a real answer worth
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

// The grid, from the classification cache. Two captioned 3x3s — the shape of a
// color board — rather than eighteen boxes in a line: "that one, bottom right
// of board 2" is a place, and a strip of eighteen has to be counted along.
//
// The board health lives in the captions rather than in status rows: setOpGrid()
// owns the whole body, so rows drawn on the same screen would run straight
// through it — and a caption sits directly above the nine cells it describes.
static void sensorsPaintGrid() {
    cubeDisplay.setOpGrid(senCapA, senFill[0], senCapB, senFill[1], senSel);
}

// The grid screen, rebuilt whole. Entry and cursor moves only — those are
// keypresses, not ticks, so the Serial-flood argument that keeps
// showOperation() out of the animated demos does not apply.
static void drawSensors() {
    char hint[40];
    snprintf(hint, sizeof(hint), "B%d S%d - SELECT for the numbers",
             (senSel < 9) ? 1 : 2, (senSel % 9) + 1);
    opScreen(Op::Scan, "Color Sensors", nullptr, hint);
    sensorsPaintGrid();
    Cube.displayUpdate();
}

// Which sensor this tick fills. The cursor's — unless some sensor has never
// been asked, in which case that one, so the grid fills itself instead of
// opening on seventeen empty cells. Mirrored from the firmware's
// senReadTarget(); the cursor still comes first while IT is the unasked one, so
// a move always shows its own answer before the sweep gets another turn.
static int senReadTarget() {
    if (!(senSeen & (1UL << senSel))) return senSel;
    if (senSeen == kSenAllSeen)       return senSel;
    for (int k = 1; k <= 18; ++k) {
        const int i = (senNext + k) % 18;
        if (!(senSeen & (1UL << i))) { senNext = (uint8_t)i; return i; }
    }
    return senSel;      // unreachable: senSeen is not full and all 18 were tried
}

// The gap between fills. On the machine this is 60 ms of open input plus one
// ~160 ms integration window per cell the cursor has not visited, which is what
// makes the opening sweep take about three seconds. There is no window behind a
// canned answer, so the whole of it is spent here — a sweep that filled
// eighteen cells in a fifth of a second would be rehearsing a screen nobody
// will ever see.
static const uint32_t kSenSweepMs = 170;

// One canned read into the grid. A fault is a CELL, not an early return: "this
// sensor cannot be read" is one of the answers the page exists to give.
static void sensorsTick() {
    const int idx = senReadTarget();
    senFill[idx / 9][idx % 9] = senCellFor(senCanned(idx));
    senSeen |= (1UL << idx);
    sensorsPaintGrid();
    Cube.displayUpdate();
}

static void actColorSensors() {
    senSel  = 0;
    senNext = 0;
    senSeen = 0;
    lastLive = 0;
    for (int b = 0; b < 2; ++b)
        for (int i = 0; i < 9; ++i) senFill[b][i] = CubeDisplay::kCellUnread;

    // Built once, in the firmware's own format. The figures are canned: nine of
    // nine on board 1, and on board 2 the eight-of-nine and the near-zero
    // separation the dead green channel actually produces, which is the number
    // this screen exists to make visible.
    snprintf(senCapA, sizeof(senCapA), "BOARD %d  %s", 1, "9/9 healthy, sep 165");
    snprintf(senCapB, sizeof(senCapB), "BOARD %d  %s", 2, "8/9 healthy, sep 3");

    live      = Live::Sensors;
    liveStart = millis();
    state     = TState::Screen;
    drawSensors();
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
    const SenCanned r = senCanned(senSel);

    static char rows[6][40];
    const char* lines[6];
    CubeDisplay::RowMark marks[6];
    static const char* const kChan[4] = { "Red", "Green", "Blue", "White" };

    for (int k = 0; k < 4; ++k) {
        // A failed transaction hands back zeros, and four zeros are a reading —
        // one that classifies as whichever reference is darkest. So a fault
        // prints as a dash and never as a number.
        if (r.rc == 0) snprintf(rows[k], sizeof(rows[k]), "%s\t%d", kChan[k], r.rgbw[k]);
        else           snprintf(rows[k], sizeof(rows[k]), "%s\t-", kChan[k]);
        lines[k] = rows[k];
        marks[k] = (r.rc == 0) ? CubeDisplay::RowMark::Plain
                               : CubeDisplay::RowMark::Bad;
    }

    if (r.rc == 0) {
        // The letter is the nearest match even when the classifier would not
        // act on it — this is the screen for seeing why. The MARK carries the
        // verdict: Good only when classify() vouches for the reading.
        snprintf(rows[4], sizeof(rows[4]), "Reads as\t%s", sensorColorName(r.color));
        marks[4] = r.ok ? CubeDisplay::RowMark::Good : CubeDisplay::RowMark::Bad;
        // How decisively the winner beat the runner-up, scaled by this sensor's
        // own separation so the number means the same thing on every sensor. It
        // is what says whether a right answer was a confident one or a lucky
        // one, which four raw channels cannot.
        snprintf(rows[5], sizeof(rows[5]), "Confidence\t%d%%  vs %c", r.conf, r.alt);
        marks[5] = r.ok ? CubeDisplay::RowMark::Plain : CubeDisplay::RowMark::Bad;
    } else {
        snprintf(rows[4], sizeof(rows[4]), "Reads as\tI2C error %d", r.rc);
        snprintf(rows[5], sizeof(rows[5]), "Confidence\t-");
        marks[4] = marks[5] = CubeDisplay::RowMark::Bad;
    }
    lines[4] = rows[4];
    lines[5] = rows[5];

    cubeDisplay.setOpLines(lines, 6, marks);
    Cube.displayUpdate();

    // The grid repaints from senFill when LEFT backs out; this sensor was just
    // read, so keep its cell current too.
    senFill[senSel / 9][senSel % 9] = senCellFor(r);
    senSeen |= (1UL << senSel);
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
// on the wheel, so the number can be watched moving under your own hand. Both
// are canned here — see motorsHome() and DialOwner.
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

// ---- the canned encoders, shared by every page that shows an angle -------
//
// Three pages read the same seven encoders on the machine — this one, the Motor
// Calibration list, and the dial both of them open — so behind them there is
// ONE canned model here rather than three. A face jogged on the dial reads back
// MOVED on this list, which is what the real pages do and what three
// independent stand-ins could never show.
//
// Deliberately not round and not evenly spaced: seven shafts last turned by
// hand do not line up, and numbers that did would invite reading a meaning into
// them that the real ones do not carry. Index 6 is the ring.
static const int kEncBase[7] = { 2013, 774, 3388, 1590, 251, 3120, 908 };

// Steps jogged, per face motor. With no encoder to ask, this IS the position.
// The ring has none, because nothing on any page jogs it — see the Ring row's
// hint bar for why the machine has no gesture for it either.
static int encSteps[6];

// One encoder canned as dead, so the "err" row and its Bad mark are on screen
// without a broken machine to produce them. That is board 2 sensor 2's argument
// on the other kind of sensor: a screen that only ever shows healthy hardware
// never shows the half of itself worth checking.
static const int kEncBadMotor = 5;             // B

// What one jogged step is worth in counts. The firmware needs no such figure —
// a real encoder reports where the face ended up — but a canned angle has to be
// moved by hand, and moving it by the machine's own numbers is what keeps the
// dial travelling at the rate the real one would.
static int encCountsPerStep() {
    const int perRev = cubeMotors.getTurnStep() * 4;
    return (perRev > 0) ? (4096 / perRev) : 10;
}

// A parked AS5600 does not sit perfectly still; it twitches by a count or two.
// Enough to show the screen is live, and small enough that it cannot be
// mistaken for the face having moved.
static int encJitter(int i) {
    return (int)((millis() / 250 + (uint32_t)i) % 5) - 2;
}

// The stand-in for MotorEncoders[i]->scan(): a raw 12-bit angle, or the RAW
// negative code, which says WHICH transaction failed.
static int encScan(int i) {
    if (i == kEncBadMotor) return -3;
    const int steps = (i < kMotFaces) ? encSteps[i] : 0;
    int v = (kEncBase[i] + steps * encCountsPerStep() + encJitter(i)) % 4096;
    if (v < 0) v += 4096;
    return v;
}

// And for scanChecked(), which retries and collapses every bus failure to -1.
// Same broken motor, a different honest number — the difference between a page
// that reports a value and a page that acts on one.
static int encScanChecked(int i) {
    const int v = encScan(i);
    return (v < 0) ? -1 : v;
}

// The canned calibration: four marks per face motor, exactly a quarter turn
// (1024 counts) apart from its base angle, which is what a clean sweep on the
// real machine produces. The firmware reads these from MotorEncoder's EEPROM
// copy; this page shows them the same way (calListRows, calDialTick).
static int calFakeMark(int motor, int j) {
    return (kEncBase[motor] + j * 1024) % 4096;
}

// Nearest canned mark and the signed error to it — the firmware's helper of
// the same name, over calFakeMark() instead of getCalibration().
static int calNearestMark(int motor, int raw, int* errOut) {
    int best = 0, bestErr = 4096;
    for (int j = 0; j < 4; ++j) {
        const int e = CubeSystem::encError(raw, calFakeMark(motor, j));
        if (abs(e) < abs(bestErr)) { bestErr = e; best = j; }
    }
    if (errOut) *errOut = bestErr;
    return best;
}

// Face names come from kJogCaps, the Actuators page's strip captions, for the
// reason calListRows() gives: three pages now address the same six motors by
// the same index, and a name table that exists three times is a table waiting
// to disagree about which motor is which.
static const char* motRowName(int row) {
    if (row < kMotFaces) return kJogCaps[row];
    return (row == kMotRing) ? "Ring" : "Home motors";
}

// The live half: seven angles, and seven of the eight rows shown.
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
            // scan(), not scanChecked(): a diagnostic wants the raw code, and
            // retrying is what flattens it to a bare -1.
            raw = encScan(row);
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
        // with bar art at all (it has five slots), and the Actuators page and
        // the calibration list next door already mark the row.
        marks[i] = (row == motSel)                    ? CubeDisplay::RowMark::Busy
                 : (row <= kMotRing && raw < 0)       ? CubeDisplay::RowMark::Bad
                                                      : CubeDisplay::RowMark::Plain;
    }

    cubeDisplay.setOpLines(lines, CubeDisplay::kOpLines, marks);
    Cube.displayUpdate();
}

// The list screen whole. Rebuilt on entry and on cursor moves only — the hint
// changes with the row, and those are keypresses rather than ticks.
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

static void actMotorSensors() {
    motSel   = 0;
    lastLive = 0;
    live      = Live::Motors;
    liveStart = millis();
    state     = TState::Screen;
    drawMotors();
}

// Send the machine home, from the diagnostic page.
//
// On the machine this is CubeSystem::homeMotors() — six steppers driven to
// their nearest detent, every wait inside it a pumpDelay() so the panel stays
// alive and SELECT+LEFT unwinds it. Nothing moves here; the screen is held for
// about as long as the machine holds it, because a rehearsal that flashed past
// the screen the machine paints would be rehearsing a flow the machine does not
// have. That is the clamp screen's argument, and Screens draws rather than
// drives.
static const uint32_t kMotHomeMs = 2600;

static void motorsHome() {
    showOp(Op::Solve, "Motor Sensors", "Homing motors", "SELECT+LEFT to abort");
    live      = Live::MotorsHome;
    liveStart = millis();
    state     = TState::Screen;
}

// A list nobody selects from, so status rows rather than bars — the same rule
// the scan screen learned the hard way.
//
// More entries than fit, so it scrolls. The position goes in the HINT BAR
// rather than beside a scrollbar: the theme has no scrollbar art, inventing
// some would be a new shape for a one-off, and "7-13 of 14" says more than a
// thumb on a track does anyway.
static int8_t faultTop = 0;

static void drawFaultLog() {
    const int rows = (kFaultCount < CubeDisplay::kOpLines)
                   ? kFaultCount : CubeDisplay::kOpLines;

    static char text[CubeDisplay::kOpLines][40];
    const char* lines[CubeDisplay::kOpLines];
    CubeDisplay::RowMark marks[CubeDisplay::kOpLines];

    for (int i = 0; i < rows; ++i) {
        const FaultEntry& f = kFaults[faultTop + i];
        snprintf(text[i], sizeof(text[i]), "%s  %s\t%d", f.when, f.what, f.code);
        lines[i] = text[i];
        // An abort is the user stopping the machine, not the machine failing.
        // Colouring it like a fault would teach the wrong thing. The code set
        // is the firmware's isAbortCode() — change both.
        marks[i] = (f.code == 5 || f.code == 9 || f.code == 25 ||
                    f.code == 70 || f.code == 105 || f.code == 125)
                 ? CubeDisplay::RowMark::Plain
                 : CubeDisplay::RowMark::Bad;
    }

    char hint[40];
    snprintf(hint, sizeof(hint), "%d-%d of %d   wheel scrolls",
             faultTop + 1, faultTop + rows, kFaultCount);

    opScreen(Op::Info, "Fault Log", nullptr, hint);
    cubeDisplay.setOpLines(lines, rows, marks);
    Cube.displayUpdate();
}

static void actFaultLog() {
    faultTop = 0;
    showScreen(Op::Info, "Fault Log", nullptr, Live::Faults);
    drawFaultLog();
}

// ---------------------------------------------------------------------------
//  Actions — screen demos, all from canned data
// ---------------------------------------------------------------------------
static void actDemoInfo() {
    const char* lines[] = {
        "Motors\tCALIBRATED",
        "Color\tNOT CALIBRATED",
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
    showScreen(Op::Scan, "Scan Faces", "Reading the cube", Live::Steps);
}

static void actDemoChips() {
    // Headline left empty: the demo sets it per stage below, and a fixed one
    // here would sit contradicting the stage line underneath it.
    showScreen(Op::Calibrate, "Color Chips", "", Live::Chips);
}


// A scramble followed by a solve.
//
// The frame carries the BRANCH, and Scramble Solve's branch is red for the
// whole run: the machine has not moved anywhere just because the scrambling
// stopped. What announces the handover is the headline, the ribbon and the
// bar, all three of which change together at the boundary — and that is what
// this demo is for.
static void actDemoScramble() {
    showScreen(Op::Solve, "Scramble Solve", "Scrambling", Live::Scramble);
}

// The six cube colors, in the order CubeDisplay's chips use them.
static const char kNetOrder[6] = { 'W', 'Y', 'R', 'O', 'G', 'B' };

// A solved cube's face colors in net order (U R F D L B). Same scheme the
// simulator scans.
static const char kSolvedFace[6] = { 'W', 'R', 'G', 'Y', 'O', 'B' };

// A solved cube in net order (U R F D L B).
static void buildSolvedNet(char* net) {
    for (int f = 0; f < 6; ++f)
        for (int k = 0; k < 9; ++k) net[f * 9 + k] = kSolvedFace[f];
}

static void actDemoNetSolved() {
    char net[CubeDisplay::kNetFacelets];
    buildSolvedNet(net);
    showScreen(Op::Info, "Solved Cube", nullptr);
    cubeDisplay.setOpCubeNet(net);
    cubeDisplay.setStatus("Every face one color");
    Cube.displayUpdate();
}

// The classic checkerboard: each sticker is either its own face color or the
// opposite one. Worth having as the scrambled case because it is a REAL state —
// five of a face's own color and four of its opposite, so each opposite pair
// still totals nine of each. A random splash of color would not be a cube, and
// would hide exactly the bugs this screen is for.
//
// Its own function because the solve confirm needs the same state: that screen
// is asking "is this the cube you loaded", and it has to be asking it about a
// cube that could exist. Two hand-built nets would eventually disagree.
static void buildScrambledNet(char* net) {
    static const char kOpp[6] = { 'Y', 'W', 'O', 'R', 'B', 'G' };   // vs kNetOrder
    buildSolvedNet(net);

    for (int f = 0; f < 6; ++f) {
        char own = net[f * 9];
        char opp = own;
        for (int c = 0; c < 6; ++c) if (kNetOrder[c] == own) opp = kOpp[c];
        for (int k = 0; k < 9; ++k) {
            const bool even = (((k / 3) + (k % 3)) % 2) == 0;
            net[f * 9 + k] = even ? own : opp;
        }
    }
}

static void actDemoNetScrambled() {
    char net[CubeDisplay::kNetFacelets];
    buildScrambledNet(net);
    showScreen(Op::Info, "Scrambled Cube", nullptr);
    cubeDisplay.setOpCubeNet(net);
    cubeDisplay.setStatus("Checkerboard - nine of each");
    Cube.displayUpdate();
}

// The orientation color calibration requires. Drawn from the firmware's own
// constant, not a copy, so this cannot drift from what the machine expects.
static void actDemoNetLoad() {
    showScreen(Op::Calibrate, "Load Orientation", nullptr);
    cubeDisplay.setOpCubeNet(CubeSystem::kCalStartFacelets);
    cubeDisplay.setStatus(CubeSystem::kCalStartText);
    Cube.displayUpdate();
}

// ---------------------------------------------------------------------------
//  Cube State — the stored model, unfolded, and a way to turn it
// ---------------------------------------------------------------------------
//  The firmware's Diagnostics > Cube State, rehearsed against a model this
//  sketch builds itself. It answers "does the machine think it is holding the
//  cube I am holding", and now also "what would this move do to it".
//
//  NOTHING PHYSICAL MOVES HERE. Not a motor, not a servo — which on the machine
//  is a choice rather than a limitation, and the essay above the firmware's
//  csTurn() is where the argument lives: Hardware Test's jogTurn() and this page
//  are mirror images of one desync. That page moves the machine and therefore
//  cannot keep the model; this page keeps the model and therefore must not move
//  the machine.
//
//  Which leaves the trap a model viewer has all of its own. A model left turned
//  no longer describes the cube in the bay either, so a later Solve would
//  compute against a state the machine is not holding and then execute it. The
//  page therefore does not leave the model turned: every move is recorded, and
//  every exit replays the trail backwards with each move inverted. Nothing here
//  scans or solves, so nothing downstream could be poisoned in this sketch —
//  but the restore is half of what the screen IS, and a rehearsal without it
//  would be rehearsing a different page.
//
//  The model is seeded from VirtualCube's own solved state in the default frame
//  (green left, orange back — the same frame CubeSystem::kPatternNets were
//  computed in), so the demo needs no scan, no solver and no cube. Turning goes
//  through VirtualCube::executeMove(), the machine's own move engine, rather
//  than a facelet permutation spelled out here: a second copy of the cube's
//  mechanics in the sketch is a copy that can disagree with the one the solver
//  runs on.
static const int kCsFaces = 6;    // U R F D L B — kJogCaps names them, and
                                  // CubeSystem::kFaceMoves is in that order
static int8_t    csSel    = 0;

// The undo trail: one byte per move, face * 2 plus a bit for the prime.
//
// Bounded because it has to live somewhere, and sixty quarter turns is far past
// what anyone does to a diagnostic. It is hard to reach at all because a turn
// that undoes the previous one POPS rather than pushing — which is exactly the
// forward-and-back gesture this page was asked for, so the usual way of using it
// costs no depth whatever. Full, the page refuses the turn and says so, rather
// than accepting one it could not take back.
static const int kCsHistMax = 60;
static uint8_t   csHist[kCsHistMax];
static int8_t    csHistN = 0;

// Nine of each color is the cheapest check that a stored state is a cube at
// all, and the one an operator can act on: a count that is not nine says which
// color was misread, which is more use than "invalid". kNetOrder is the shared
// color order this file already draws chips in.
static void csCountRow(const char* fac, char* out, size_t n) {
    int count[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < CubeDisplay::kNetFacelets; ++i) {
        for (int c = 0; c < 6; ++c) if (fac[i] == kNetOrder[c]) count[c]++;
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
// with every move inverted. Every exit goes through it.
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
    // are this machine's idiom for "the wheel changes this" — the Actuators
    // page and the value editor both — and MODEL ONLY is in capitals because
    // the whole risk of the page is an operator believing the machine turned.
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

// Build the model this page turns, in VirtualCube's default frame. No scan and
// no solver: setSolved() paints the six faces, setOrientation() names which way
// round they sit, and buildCubeArray() is what makes isReady() true — the same
// three calls, in the same order, that CubeSystem makes after a real scan.
static bool csSeedModel() {
    Cube.virtualCube.resetCube();
    Cube.virtualCube.setSolved();
    if (Cube.virtualCube.setOrientation('G', 'O') != 0) return false;
    if (Cube.virtualCube.buildCubeArray() != 0)         return false;
    Cube.virtualCube.rebuildFromCubeArray();
    return Cube.virtualCube.isReady();
}

static void actCubeState() {
    // Seeded only when there is nothing to show, so a second visit opens on the
    // model the last one left — which is what proves csRestore() put it back.
    // A model left ready behind us is the firmware's behaviour too, and the
    // Actuators page already knows what to do about one: jogTurn() wipes it
    // before it moves a motor.
    if (!Cube.virtualCube.isReady() && !csSeedModel()) {
        showScreen(Op::Error, "Cube State", "Model unavailable");
        const char* lines[] = { "VirtualCube would not build a solved",
                                "cube - nothing to turn." };
        cubeDisplay.setOpLines(lines, 2);
        Cube.displayUpdate();
        return;
    }

    // A move made elsewhere leaves the derived color array stale — see csTurn().
    // Despite the UNFINISHED label on its header, rebuildFromCubeArray() is the
    // working refresh call, and it is all this screen needs.
    Cube.virtualCube.rebuildFromCubeArray();
    csSel   = 0;
    csHistN = 0;        // never carried across visits: the trail belongs to one
                        // sitting, and csRestore() emptied it on the way out of
                        // the last one
    live    = Live::None;
    state   = TState::CubeState;
    drawCubeState();
}

// ---------------------------------------------------------------------------
//  The thinking half of a solve — both frames of it
// ---------------------------------------------------------------------------
//  The firmware draws these two from one function (drawSolveNet), and so does
//  this: they are the same picture asking the same question — "is this the cube
//  you loaded" — and only the line under the net and the hint change. Kept as
//  one drawer here for the same reason, so the rehearsal cannot show two
//  screens the machine draws as one.
//
//  Why the net and not a headline. solveVirtual() blocks unpumped for up to
//  ~10 s on the machine, so the first of these stands long enough to be read,
//  and what is worth reading in that window is the cube the solution is being
//  computed FOR. It is the one chance to catch "the machine is not holding what
//  I think it is" before twenty moves run against a bad model, and the confirm
//  after it is where that catch is acted on.
//
//  There is no headline, and that is structural rather than taste: the net
//  occupies y=60..150 and showOperation() puts the headline at 58, so a screen
//  carrying both draws one through the other. setOpCubeNet() moves the sub-line
//  below the net for exactly this reason, which is where the text goes instead.
//  Cube State and the pattern result are built the same way.
//
//  Title "Solve", not the menu label: the firmware puts the operation's own
//  name up there (s_opTitle), and it is the machine's screen being judged.
static void drawSolveNet(const char* sub, const char* hint) {
    char net[CubeDisplay::kNetFacelets];
    buildScrambledNet(net);
    opScreen(Op::Solve, "Solve", nullptr, hint);
    cubeDisplay.setOpCubeNet(net);
    cubeDisplay.setStatus(sub);
    Cube.displayUpdate();
}

// How long the first frame stands before the count arrives. Shorter than the
// machine's worst case on purpose — this is a screen being looked at, not a
// search being waited on — but long enough that the two frames read as a
// sequence rather than as one flicker.
static const uint32_t kSolveThinkMs = 1600;

static void actDemoSolveConfirm() {
    // Not showScreen(): that stamps "SELECT or LEFT to go back", and on the
    // machine SELECT here starts twenty moves. The hint IS the design on this
    // screen, so swapping in the generic line would rehearse a different one.
    // The ordinary TState::Screen handler still dismisses it on either key.
    live      = Live::SolveNet;
    liveStart = millis();
    state     = TState::Screen;
    drawSolveNet("Finding solution...", nullptr);
}

// Folding a pattern, animated from the firmware's own tables: the fold moves
// march the ribbon at the machine's pace, then the screen becomes the
// firmware's completion shape — the pattern's net, its name on the line
// beneath. Moves, counts and nets are all CubeSystem's copy, so this rehearsal
// cannot drift from what the machine folds.
static int8_t      g_foldIdx  = 0;
static const char* g_foldName = nullptr;

static void actFoldPattern() {
    // Index FIRST, before the screen changes: the tick reads the shared
    // tables by row, and selectedIndex() only means this row while the menu
    // still shows it. Item order matches the kPattern* tables — see the
    // table's comment.
    g_foldIdx = (int8_t)Menu.selectedIndex();
    const MenuItem* it = Menu.selectedItem();
    g_foldName = (it && it->label) ? it->label : "Pattern";

    // The one override in this sketch, and the firmware's actPattern() makes
    // the same one. The four Patterns rows are colored so four rows of preview
    // net do not share a frame; those colors are a preview device, not four
    // places. The BRANCH is the screen's green, so put it back before any
    // operation screen is drawn.
    s_opTheme = MenuTheme::Green;

    showScreen(Op::Solve, "Patterns", "Folding", Live::Fold);
    cubeDisplay.setStatus(g_foldName);
    Cube.displayUpdate();
}

// ~140 ms per move — the same pace the scramble demos use, because it is the
// machine's. Pieces only, per the 20 Hz rule.
static void updateFold(uint32_t t) {
    const char* const* moves  = CubeSystem::kPatternMoves[g_foldIdx];
    const int          count  = CubeSystem::kPatternMoveCounts[g_foldIdx];
    const uint32_t     foldMs = (uint32_t)count * 140;

    if (t < foldMs) {
        const int m = (int)((t * (uint32_t)count) / foldMs);
        char sub[48];
        snprintf(sub, sizeof(sub), "Move %d of %d   %s", m + 1, count, moves[m]);
        cubeDisplay.setMessage("Folding");
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(moves, count, m);
        cubeDisplay.setOpProgress(m, count);
        return;
    }

    // Done — the firmware's completion shape. The headline goes because the
    // net owns the middle of the screen, and the fold's ribbon and bar go
    // with it. Dropping live to None makes this a one-shot: the finished
    // screen just sits, dismissed by the generic SELECT/LEFT handler, and
    // the net is not rewritten twenty times a second for nothing.
    //
    // No recolor to Op::Done. Finishing a fold is not going anywhere, and the
    // frame stays the Patterns green actFoldPattern() pinned.
    cubeDisplay.setMessage("");
    cubeDisplay.setOpRibbon(nullptr, 0, -1);
    cubeDisplay.setOpProgress(0, 0);
    cubeDisplay.setOpCubeNet(CubeSystem::kPatternNets[g_foldIdx]);
    cubeDisplay.setStatus(g_foldName);
    live = Live::None;
}

// One move per press. The ribbon is the whole screen: where you are in the
// solution, what just happened, and what is coming — which a "Move 7/21"
// counter alone cannot show.
static int8_t stepAt = 0;

static void drawStepSolve() {
    char head[32];
    snprintf(head, sizeof(head), "Move %d of 21", stepAt + 1);
    opScreen(Op::Solve, "Step Solve", head,
             (stepAt < 20) ? "SELECT for the next move"
                           : "SELECT to finish");
    cubeDisplay.setOpRibbon(kStepMoves, 21, stepAt);
    cubeDisplay.setOpProgress(stepAt, 20);
    Cube.displayUpdate();
}

// Whether the cube in the machine is scrambled.
//
// Tracked rather than assumed because Step Solve has to know whether it needs
// to scramble first, and because Idle Mode scrambles it as a side effect of
// turning to look alive. Set by anything that disorders the cube, cleared by
// anything that solves it.
static bool cubeScrambled = false;

// Thirty moves at ~140 ms is what the machine takes, and the pause after them
// is not padding: a solution has to be computed before it can be run, and
// jumping from the last scramble move straight to the first solve move would
// show something the machine never does.
static const uint32_t kStepScramMs    = 4200;
static const uint32_t kStepComputeMs  = 1400;

// Ticks at 20 Hz, so it updates PIECES rather than calling showOperation()
// again. That call rebuilds the screen and prints the title and headline to
// Serial every time — the same flood that made the demo screens unreadable over
// the serial monitor. setMessage() prints only when the text actually changes.
static void drawStepScramble(uint32_t t) {
    const bool computing = (t >= kStepScramMs);
    const int  m = computing ? CubeSystem::kScrambleLen
                             : (int)((t * CubeSystem::kScrambleLen) / kStepScramMs);

    // The wording is the firmware's, verbatim — headline "Scrambling" with the
    // move counter on the status line, then "Computing the solution" — so the
    // rehearsal shows the screens the machine actually paints (toComputing()
    // and ModeScrambling in CubeSolver.ino), not a paraphrase of them.
    //
    // Neither half recolors. The frame reports WHERE YOU ARE, and both halves
    // are Step Solve's purple, exactly as toComputing() reasserts s_opTheme
    // rather than naming a kind. The words, the ribbon and the bar carry the
    // phase.
    if (computing) {
        cubeDisplay.setMessage("Computing the solution");
        cubeDisplay.setStatus("");
        // Clear the scramble's furniture. Without this the finished ribbon and
        // a full progress bar sit under "Computing", which reads as a solve
        // that is already complete before it has started. Both hide on a
        // null/zero argument.
        cubeDisplay.setOpRibbon(nullptr, 0, -1);
        cubeDisplay.setOpProgress(0, 0);
    } else {
        const int shown = (m < CubeSystem::kScrambleLen) ? m : CubeSystem::kScrambleLen - 1;
        char sub[40];
        snprintf(sub, sizeof(sub), "Move %d of %d   %s",
                 shown + 1, CubeSystem::kScrambleLen, kScrambleMoves[shown]);
        cubeDisplay.setMessage("Scrambling");
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(kScrambleMoves, CubeSystem::kScrambleLen, shown);
        cubeDisplay.setOpProgress(m, CubeSystem::kScrambleLen);
    }
    Cube.displayUpdate();
}

static void actStepSolve() {
    stepAt = 0;
    if (cubeScrambled) {
        // Already disordered - by Idle Mode, or by the last time through here.
        // Scrambling an already scrambled cube would be a lie about what the
        // machine does, and thirty moves of one.
        showScreen(Op::Solve, "Step Solve", nullptr, Live::Step);
        drawStepSolve();
        return;
    }
    // Op::Solve, not Op::Error: this is a scramble, not a fault, and the frame's
    // color comes from the Step Solve row (purple), not from the kind.
    showScreen(Op::Solve, "Step Solve", "Scrambling", Live::StepScram);
    drawStepScramble(0);
}

// Scramble, solve, repeat, unattended. Both halves already existed — the phase
// shape from Scramble Solve, the ribbon from Step Solve — so this is a loop
// around them plus a run counter.
//
// It shows the MOVES rather than only a counter, because the whole point of
// leaving this running is that it should be worth watching.
// Four phases, because the machine has four. The pause between scrambling and
// solving is not padding: the real one has to work out a solution before it can
// run one, and a demo that jumped straight from the last scramble move to the
// first solve move would be showing something the machine never does.
static const uint32_t kDemoScrambleMs = 4200;   // 30 moves at ~140 ms
static const uint32_t kDemoComputeMs  = 1400;
static const uint32_t kDemoSolveMs    = 3500;
static const uint32_t kDemoRestMs     = 1500;
static const uint32_t kDemoCycleMs    = kDemoScrambleMs + kDemoComputeMs +
                                        kDemoSolveMs + kDemoRestMs;

static void actDemoMode() {
    showScreen(Op::Solve, "Demo Mode", "Scrambling", Live::Demo);
}

// Four phases and ONE color, blue, all the way through — Demo Mode's own row.
// No phase may recolor the frame, because a mode you leave running for
// minutes is somewhere you ARE, and the band is what says so. The headline,
// the ribbon and the bar all change at each handover, which is what the demo
// is watched for.
static void updateDemoMode(uint32_t t) {
    const uint32_t cycle = t % kDemoCycleMs;
    const int      run   = (int)(t / kDemoCycleMs) + 1;

    char sub[52];
    if (cycle < kDemoScrambleMs) {
        const int m = (int)((cycle * CubeSystem::kScrambleLen) / kDemoScrambleMs);
        cubeDisplay.setMessage("Scrambling");
        snprintf(sub, sizeof(sub), "Run %d   -   Move %d of %d",
                 run, m + 1, CubeSystem::kScrambleLen);
        cubeDisplay.setOpRibbon(kScrambleMoves, CubeSystem::kScrambleLen, m);
        Cube.displayProgress(m, CubeSystem::kScrambleLen - 1);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs) {
        // The handover. It happens here rather than at the first solve move,
        // because this is the moment it stops scrambling — and the ribbon and
        // the bar go, because neither has anything true to say about a search
        // that has not finished.
        cubeDisplay.setMessage("Computing the solution");
        snprintf(sub, sizeof(sub), "Run %d", run);
        cubeDisplay.setOpRibbon(nullptr, 0, 0);
        Cube.displayProgress(0, 0);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs + kDemoSolveMs) {
        const int m = (int)(((cycle - kDemoScrambleMs - kDemoComputeMs) * 21)
                            / kDemoSolveMs);
        cubeDisplay.setMessage("Solving");
        snprintf(sub, sizeof(sub), "Run %d   -   Move %d of 21", run, m + 1);
        cubeDisplay.setOpRibbon(kStepMoves, 21, m);
        Cube.displayProgress(m, 20);
    } else {
        cubeDisplay.setMessage("Solved!");
        snprintf(sub, sizeof(sub), "Run %d   -   %d moves in 4.62 s",
                 run, kDoneMoves);
        cubeDisplay.setOpRibbon(kStepMoves, 21, 20);
        Cube.displayProgress(20, 20);
    }
    cubeDisplay.setStatus(sub);
}

// The firmware's Scramble Solve, rehearsed: one run of Demo Mode's phases
// without the run counter, painting the status lines the firmware paints —
// the scramble with ribbon and bar, "Computing" with the furniture cleared,
// the solve, then "Solved!". It loops like every other demo. The machine holds
// at Solved! until SELECT, but the handovers are what this rehearsal is for,
// and looping lets them be watched without re-picking the item.
//
// Red throughout, and only because Scramble Solve's ROW is red — not because
// scrambling is. No phase repaints the band; what marks a handover is the
// headline changing and the furniture clearing, and whether that is enough to
// read across a room is the question this screen exists to answer.
static void updateScrambleSolve(uint32_t t) {
    const uint32_t cycle = t % kDemoCycleMs;

    char sub[48];
    if (cycle < kDemoScrambleMs) {
        const int m = (int)((cycle * CubeSystem::kScrambleLen) / kDemoScrambleMs);
        cubeDisplay.setMessage("Scrambling");
        snprintf(sub, sizeof(sub), "Move %d of %d   %s",
                 m + 1, CubeSystem::kScrambleLen, kScrambleMoves[m]);
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(kScrambleMoves, CubeSystem::kScrambleLen, m);
        cubeDisplay.setOpProgress(m, CubeSystem::kScrambleLen);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs) {
        // The handover, at the moment it stops scrambling: the scramble's
        // furniture goes, because a finished ribbon and a full bar under
        // "Computing" read as a solve that finished before it started.
        cubeDisplay.setMessage("Computing the solution");
        cubeDisplay.setStatus("");
        cubeDisplay.setOpRibbon(nullptr, 0, -1);
        cubeDisplay.setOpProgress(0, 0);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs + kDemoSolveMs) {
        const int m = (int)(((cycle - kDemoScrambleMs - kDemoComputeMs)
                             * kDoneMoves) / kDemoSolveMs);
        cubeDisplay.setMessage("Solving");
        snprintf(sub, sizeof(sub), "Move %d/%d   %s", m + 1, kDoneMoves,
                 kStepMoves[m]);
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(kStepMoves, kDoneMoves, m);
        cubeDisplay.setOpProgress(m, kDoneMoves);
    } else {
        snprintf(sub, sizeof(sub), "%d moves in 4.62 s", kDoneMoves);
        cubeDisplay.setMessage("Solved!");
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(kStepMoves, kDoneMoves, kDoneMoves - 1);
        cubeDisplay.setOpProgress(kDoneMoves, kDoneMoves);
    }
}

// ---------------------------------------------------------------------------
//  The two endings
// ---------------------------------------------------------------------------
//  A plain Solve and an Eject each finish on a screen of their own, and
//  neither is a mode. Most of what the machine does there is behind the
//  screen — turning the cube on the bottom gripper so the finished faces can
//  be seen, or watching a color sensor for the cube to leave — and neither of
//  those is anything a sketch with no cube, no solver and no reason to touch
//  the mux can show.
//
//  The screen is the half that CAN be judged on the bench, so the screen is
//  what is rehearsed: frame color, wording, and above all the hint. On both of
//  these the hint is the design. Neither says "SELECT or LEFT to go back",
//  because on the machine SELECT does something to the machine, and a demo
//  that quietly swapped in the generic line would be rehearsing the one part
//  that was actually rewritten.
//
//  Both are therefore drawn with opScreen() and their own hint rather than
//  through showScreen(), which stamps the generic one — the same reason
//  actCalMotors does not use it either. They are still dismissed by the
//  ordinary TState::Screen handler, so SELECT and LEFT do return to the menu
//  here.

// 4.62 s is the time Scramble Solve and Demo Mode already quote on their
// "Solved!" frames, so all three endings report the same solve. The move count
// is kDoneMoves, which lives up with the table it counts.
static const uint32_t kDoneMillis = 4620;

// How a plain Solve ends. The machine releases the ring and the top servo,
// keeps the cube up on the bottom gripper and turns it slowly on the spot —
// about eight seconds a revolution — with this standing on the screen until
// SELECT squares the cube, clamps it again and goes back to the menu.
//
// The spin is the part no bench sketch can show, and nothing here energises a
// motor to fake it. Everything else is the firmware's drawSolveDisplay():
// "Solved!", the result on the status line, and no ribbon or bar — the solve is
// over, and a move list still sitting under it would be offering something
// nobody can act on. The result line is built by the same format string the
// firmware's formatSolveResult() uses — change both.
//
// Op::Done is a LABEL here, not a color. The frame is blue, because Solve is,
// and it stays blue from the confirm through the run to this screen: finishing
// is not arriving somewhere new.
static void actDemoSolveDone() {
    live      = Live::None;
    liveStart = millis();
    state     = TState::Screen;

    char sub[48];
    snprintf(sub, sizeof(sub), "%d moves in %lu.%02lu s", kDoneMoves,
             (unsigned long)(kDoneMillis / 1000),
             (unsigned long)((kDoneMillis % 1000) / 10));

    // Title "Solve", not the menu label — see drawSolveNet().
    opScreen(Op::Done, "Solve", "Solved!",
             "SELECT clamps the cube and finishes");
    cubeDisplay.setStatus(sub);
    Cube.displayUpdate();
}

// Eject. The machine watches a color sensor for the cube to go, so the headline
// is a plain statement of the only thing left to do, and the button is named as
// the BACKSTOP for the machine not noticing rather than as a step — a press to
// say "I took it out" carries nothing the operator is not already holding in
// their hand. That distinction lives in two lines of text, which is exactly
// what this sketch exists to let someone read on the real panel. The watch
// itself needs the real color boards and a real cube, so nothing here opens
// the mux or lights an LED.
static void actDemoEject() {
    live      = Live::None;
    liveStart = millis();
    state     = TState::Screen;

    // Info, not Done: on the machine this frame is up while it is still
    // waiting, and Done would be claiming the eject had finished.
    opScreen(Op::Info, "Eject", "Take the cube out",
             "SELECT if the machine does not notice");
    Cube.displayUpdate();
}

// ---------------------------------------------------------------------------
//  Stats
// ---------------------------------------------------------------------------
//  Six rows, which is exactly enough — resist a seventh. The numbers here are
//  canned: the EEPROM block behind the real screen is CubeStats (its header
//  carries the design brief), and the only question this demo answers on the
//  bench is whether the label/value columns line up at realistic widths.
//
//  Row format mirrors the firmware's actStats — change both. That includes
//  the "(+N step)" tally Step Solve adds to the Solves row (counted but
//  untimed, so it must not inflate the count the average divides by) and the
//  "-" a time shows before anything has been recorded.
static void actStats() {
    static const char* const rows[6] = {
        "Solves\t128 (+6 step)",
        "Best\t12.4 s",
        "Average\t18.9 s",
        "Last\t15.2 s",
        "Run time\t9h 41m",
        "Faults\t3",
    };
    showScreen(Op::Info, "Stats", nullptr);
    cubeDisplay.setStatus("Since first use");
    cubeDisplay.setOpLines(rows, 6, nullptr);
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
//  That is the ONE INTENTIONAL EXCEPTION to the rule that the band says where
//  you are — everything else in this sketch wears its branch's color and holds
//  it — and tying the cycle to the moves at least makes it honest: a color
//  change means something happened, so the machine is visibly alive from
//  further away than the move counter can be read.
//
//  Idle's OTHER screens do not cycle. The solve it hands off to wears Idle's
//  own yellow, because that is somewhere you went rather than something that
//  happened.
static const MenuTheme kIdleCycle[6] = {
    MenuTheme::Green,  MenuTheme::Blue,   MenuTheme::Violet,
    MenuTheme::Purple, MenuTheme::Yellow, MenuTheme::Red,
};

// Seconds, not milliseconds, because that is the unit the wheel steps in and
// storing what is displayed avoids a rounding disagreement between the two.
static const int kIdleGapMin =  1;
static const int kIdleGapMax = 60;
static int      idleGapS   = 10;
static uint8_t  idleStep   = 0;      // where in the color cycle
static uint16_t idleMoves  = 0;
static uint32_t idleNextAt = 0;
static const char* idleLastMove = nullptr;

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

    cubeDisplay.setMessage(idleLastMove ? idleLastMove : "Ready");

    // The move count goes in the sub-line rather than a row of its own. One
    // number is not a table, and putting it there leaves the middle of the
    // screen for the dial - which is the only thing here worth looking at
    // from any distance.
    cubeDisplay.setStatus(moves);
    cubeDisplay.setOpDial(idleGapS, kIdleGapMin, kIdleGapMax, gap, "between moves");
    // Overrides s_opTheme on purpose, and this is the only screen allowed to.
    // See the cycle's comment above.
    cubeDisplay.setOpTheme(kIdleCycle[idleStep]);
    Cube.displayUpdate();
}

// One random quarter turn. Reuses the firmware's move table rather than
// carrying a copy — a demo that drifted from the moves the machine sends
// would be showing notation the machine does not use.
static void idleTurn() {
    idleLastMove = CubeSystem::kFaceMoves[random(6)][random(2)];
    idleMoves++;
    idleStep = (uint8_t)((idleStep + 1) % 6);

    // Idling disorders the cube, so Step Solve must not scramble it again.
    cubeScrambled = true;

    idleNextAt = millis() + (uint32_t)idleGapS * 1000UL;
    drawIdle();
}

static void actIdleMode() {
    idleStep     = 0;
    idleMoves    = 0;
    idleLastMove = nullptr;
    idleNextAt   = millis() + (uint32_t)idleGapS * 1000UL;

    // Seeded from the clock so two runs do not turn the same way. Entry time
    // depends on how long someone spent in the menu, which is enough.
    randomSeed(millis());

    // Not showScreen(): that stamps its own "SELECT or LEFT to go back" hint,
    // and here SELECT does something quite different from going back.
    live      = Live::Idle;
    liveStart = millis();
    state     = TState::Screen;

    // The one full build. Everything after this updates pieces.
    opScreen(Op::Solve, "Idle", "Ready",
             "wheel sets the gap - SELECT solves");
    drawIdle();
}

// SELECT means "stop idling and solve it". The solve itself is canned, like
// every other operation in this sketch, but the state it leaves behind is not:
// the cube ends up solved, so Step Solve will scramble before its next run.
static void actIdleSolve() {
    showScreen(Op::Solve, "Idle", "Solving", Live::IdleSolve);
    // showScreen() rebuilds, which already clears the extras - but say so,
    // because a dial left up beside a progress bar would be two different
    // claims about how far along the same operation is.
    cubeDisplay.setOpDial(0, 0, 0, nullptr, nullptr);
}

static const uint32_t kIdleSolveMs = 3500;

static void updateIdleSolve(uint32_t t) {
    const int total = 21;
    if (t >= kIdleSolveMs) {
        cubeScrambled = false;
        // No sub-line: showScreen() already puts "SELECT or LEFT to go back" in
        // the hint bar, and saying it twice on one screen reads as two
        // different instructions that happen to match.
        showScreen(Op::Done, "Idle", "Solved");
        Cube.displayUpdate();
        return;
    }
    const int m = (int)((t * total) / kIdleSolveMs);
    cubeDisplay.setOpRibbon(kStepMoves, total, m);
    cubeDisplay.setOpProgress(m, total);
}

// ---------------------------------------------------------------------------
//  Motor Calibration — the firmware's flow, from canned data
// ---------------------------------------------------------------------------
//  Four screens in the machine's order: confirm the chamber is empty, clamp,
//  square each face on the dial, save. Rehearsed as a FLOW rather than as four
//  separate items, because a flow is what it is on the machine — the same
//  argument that keeps Screens > Modes item for item with the firmware's Modes
//  menu. Every key means here what it means there.
//
//  Why the machine needs the flow at all, in one line:
//  calibrateMotorRotations() derives all four of a motor's marks from wherever
//  that motor is STANDING when the sweep begins, so a face left out of square
//  produces four marks that are out of square by the same amount and nothing
//  downstream can tell. The dial is where that gets fixed, by eye, before the
//  sweep runs.
//
//  NOTHING here reads an encoder, moves a motor or touches the mux. The angles
//  are a canned base per motor plus whatever the wheel has jogged, which is
//  what lets the dial answer the wheel on a bench with no machine behind it.
//  The one thing this cannot rehearse is the clamp — the real flow closes the
//  grippers on an empty centre, and Actuators is where that gets tried with
//  hardware present.
//
//  The row and dial code below is the firmware's calListRows() and
//  calDialTick(), line for line apart from where the angle comes from —
//  change both.
//
//  THE DIAL IS ONE SCREEN WITH TWO DOORS. This flow's Save list opens it, and
//  so does Diagnostics > Motor Sensors, which has no Save row and no way to
//  reach one. DialOwner picks the title, the hint and the list to go back to,
//  and nothing else — an operator who has seen it once has seen it both times.
//  What the owner does NOT pick is whether anything is written, because the
//  dial writes nothing either way: the only thing it records is calAligned[],
//  a flag about the operator's judgement, and the diagnostic cannot reach even
//  that, because there SELECT is a detent rather than an acceptance. "The
//  diagnostic writes nothing" is a route that does not exist rather than a
//  check somebody has to keep making.

static const int kCalMotors = 6;              // faces only. The ring has an
                                              // encoder but is not rotation-
                                              // calibrated, and CubeSystem
                                              // clamps numMotors to 6 for the
                                              // same reason.
static const int kCalSave   = kCalMotors;     // the last row: confirm and save
static const int kCalRows   = kCalMotors + 1; // 7 — exactly CubeDisplay::kOpLines

static int8_t   calSel = 0;                   // cursor, 0..kCalSave
static bool     calAligned[kCalMotors];       // SELECT accepted this face's pose
static int      calDelta = 0;                 // steps jogged on the open dial
static uint32_t calTick  = 0;                 // throttle for the angle refresh
                                              // on both pages

// Which page opened the dial. Set by calEnterDial() and read by everything the
// dial does differently for the two of them — three things, all of them about
// the PAGE rather than about what the dial writes.
static DialOwner dialOwner = DialOwner::CalFlow;

// The two phases nobody drives. The clamp is three actuators moving; the save
// is calibrateMotorRotations(), which turns every face through four quarter
// turns and scans between them. Both are held for about as long as the machine
// holds them, because skipping a screen the machine paints is rehearsing a
// flow the machine does not have — the same reason the scramble demos pause on
// "Computing the solution".
static const uint32_t kCalClampMs = 1400;
static const uint32_t kCalSaveMs  = 5000;

// The angles come from the shared canned encoders — kEncBase and encSteps, up
// with the Motor Sensors page — through encScanChecked(), which is this flow's
// read: scanChecked() retries and collapses every bus failure to -1, so -1 is
// the only negative a row here can honestly carry. Back is the canned dead one,
// and its row is the reason to look.
//
// Mirrored from the firmware's calStepSize() — change both.
//
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

// The six motors and the Save row. Seven rows, no headline and no sub-line:
// seven is exactly CubeDisplay::kOpLines and they only fit when they start at
// the top of the body, which is the Motor Sensors page's shape.
//
// Rows, not bars, even though every one of them IS selectable. Bar art is the
// menu's vocabulary for a choice and it has five slots; seven choices cannot
// be drawn with it at all. So the cursor is a marked row, exactly as on the
// Actuators page — the same interaction deserves the same look.
//
// Motor names come from kJogCaps, the jog page's strip captions, rather than a
// second hand-written U R F D L B: both pages address the same six motors by
// the same index, and a name table that exists twice is a table waiting to
// disagree about which motor is which.
static void calListRows() {
    // 48 where the firmware writes 40: the Save row's two counts are plain
    // ints as far as the compiler can see, and at 40 it warns that a wide
    // enough one would truncate the row. Eight more bytes a row cost nothing
    // anyone can measure, and a warning nobody can act on is how a build
    // learns to be ignored.
    static char rows[kCalRows][48];
    const char* lines[kCalRows];
    CubeDisplay::RowMark marks[kCalRows];

    int done = 0;
    for (int i = 0; i < kCalMotors; ++i) {
        // The SIGN is checked, as it is on every screen here that shows an
        // encoder: a negative is an I2C fault, not an angle, and drawing it as
        // a position is what MotorEncoder.h is emphatic about.
        const int raw = encScanChecked(i);
        if (calAligned[i]) done++;

        // The firmware appends the nearest STORED mark and the signed error
        // to it ("m2 -118"); the canned marks here sit exactly a quarter turn
        // apart from each motor's base angle, so the number shown is the
        // jitter plus whatever the dial has jogged.
        // 16 bytes and a bounded error: the nearest of four marks is never
        // more than 512 counts away, but GCC only knows it is an int, so the
        // clamp is what buys the clean build.
        char near[16] = "";
        if (raw >= 0) {
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

    snprintf(rows[kCalSave], sizeof(rows[kCalSave]),
             "Save calibration\t%d of %d squared", done, kCalMotors);
    lines[kCalSave] = rows[kCalSave];
    marks[kCalSave] = (calSel == kCalSave) ? CubeDisplay::RowMark::Busy
                                           : CubeDisplay::RowMark::Plain;

    cubeDisplay.setOpLines(lines, kCalRows, marks);
    Cube.displayUpdate();
}

// The list screen whole. Rebuilt on entry and on cursor moves only — those are
// keypresses, not ticks, so the Serial-flood argument that keeps
// showOperation() out of the animated demos does not apply. The per-tick
// refresh goes through calListRows() alone.
static void drawCalList() {
    char hint[40];
    if (calSel == kCalSave) {
        snprintf(hint, sizeof(hint), "SELECT runs the calibration");
    } else {
        snprintf(hint, sizeof(hint), "SELECT jogs motor %s", kJogCaps[calSel]);
    }
    // One color the whole way through — Motor Cal's yellow, which is the
    // firmware's Calibration branch: every step of this flow is the same PLACE.
    opScreen(Op::Calibrate, "Motor Calibration", nullptr, hint);
    calListRows();
}

// The dial page's frame, painted once on entry. calDialTick() updates the
// value in place afterwards — showOperation() rebuilds the screen and reprints
// the title to Serial, and twenty times a second that is the flood the
// animated demos already learned to avoid.
static void drawCalDialFrame() {
    // The title and the hint are all that change with the owner. The layout,
    // the frame color and the interaction are deliberately identical: this is
    // one screen opened from two places, not two screens that resemble each
    // other. Both pages are the firmware's Settings yellow anyway.
    const bool diag = (dialOwner == DialOwner::Diagnostic);
    opScreen(Op::Calibrate, diag ? "Motor Sensors" : "Motor Calibration", nullptr,
             diag ? "wheel steps - LEFT goes back" : "wheel steps - SELECT accepts");

    // "You have taken this row and the next detent goes to the machine." A
    // badge rather than a color: the whole Settings subtree — where every
    // screen that can arm anything lives — is yellow, so a yellow frame would
    // move nothing. Re-armed after every opScreen() because showOperation()
    // clears it, which is what stops a screen inheriting somebody else's badge.
    cubeDisplay.setOpArmed("ARMED");
    Cube.displayUpdate();
}

// The live half: one angle, then the dial.
static void calDialTick() {
    const int raw = encScanChecked(calSel);

    // 40 where the firmware writes 32, for the reason calListRows() gives for its
    // rows: encScanChecked() returns through a lookup the compiler cannot bound,
    // so it would otherwise warn that "err %d" could truncate the caption.
    char caption[16], centre[12], sub[40];
    snprintf(caption, sizeof(caption), "Motor %s", kJogCaps[calSel]);

    if (raw < 0) {
        // A negative is a sensor fault, NOT a position. Put it on the arc and
        // the needle lands somewhere plausible and confident, which is worse
        // than no dial at all — so the dial goes away and the fault takes the
        // headline. The motor's name has to move to the sub-line with it: the
        // dial's caption was carrying it, and the dial is gone.
        snprintf(sub, sizeof(sub), "%s   err %d", caption, raw);
        cubeDisplay.setStatus(sub);
        cubeDisplay.setMessage("Encoder unreadable");
        cubeDisplay.setOpDial(0, 0, 0, nullptr, nullptr);
    } else {
        // How far the wheel has moved this face since the page opened. The
        // dial shows the ABSOLUTE angle, which says nothing about how far you
        // have come — and "am I nudging it or have I been round" is the one
        // question a jog page has to answer that its own readout cannot.
        //
        // As in the firmware: the headline carries the count and the nearest
        // stored mark with the error to it; the sub-line lists the four
        // stored marks for this motor.
        int e; const int k = calNearestMark(calSel, raw, &e);
        char head[32];
        snprintf(head, sizeof(head), "%+d steps   m%d %+d", calDelta, k, e);
        snprintf(sub, sizeof(sub), "marks %d %d %d %d",
                 calFakeMark(calSel, 0), calFakeMark(calSel, 1),
                 calFakeMark(calSel, 2), calFakeMark(calSel, 3));
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
static void calEnterDial(DialOwner owner) {
    dialOwner = owner;
    calDelta = 0;
    // The firmware energises the steppers here and disables them on the way
    // out, so the face holds where the operator put it until the sweep reads
    // it. There is nothing to energise behind a canned angle, and Screens
    // draws rather than drives — so the call is deliberately absent here, not
    // forgotten.
    calTick = 0;                    // redraw on the very next pass
    state = TState::MotorDial;
    drawCalDialFrame();
}

// Leave the dial, accepted or not, and go back to the list that opened it.
static void calLeaveDial(bool accepted) {
    // The firmware's interpretation (a): SELECT records that the FACE is
    // square and writes nothing into the encoder's calibration — the Save
    // sweep is what turns six squared faces into twenty-four values. If the
    // bench decides otherwise there, this is the function that follows it.
    //
    // The owner test is belt and braces: no diagnostic path passes true, since
    // SELECT is a detent there. It lives here rather than at the call site so
    // that a future caller cannot reintroduce the record by passing it.
    if (accepted && dialOwner == DialOwner::CalFlow) calAligned[calSel] = true;
    calTick = 0;
    if (dialOwner == DialOwner::Diagnostic) {
        live  = Live::Motors;
        state = TState::Screen;
        drawMotors();
    } else {
        live  = Live::CalPick;
        state = TState::Screen;
        drawCalList();
    }
}

// Move the selected face by one detent's worth of steps. The firmware hands
// all six targets to moveTo() and lets the encoder report the result; with no
// motor and no encoder the step count IS the result, and encScan() adds it to
// the shared base.
static void calJog(int detents) {
    const int d = detents * calStepSize();
    encSteps[calSel] += d;
    calDelta += d;
}

// The clamp screen. The words are the machine's, including an abort chord with
// nothing to abort here — this screen is the thing being judged, and rewording
// it would judge a different one.
static void calToClamp() {
    showOp(Op::Calibrate, "Motor Calibration", "Closing the grippers",
           "SELECT+LEFT to abort");
    live      = Live::CalClamp;
    liveStart = millis();
    state     = TState::Screen;
}

// The Save row: the only step of the flow that writes anything, and the only
// one that is not interactive.
static void calToSave() {
    showOp(Op::Calibrate, "Motor Calibration", "Finding home positions",
           "Do not touch the machine");
    live      = Live::CalSave;
    liveStart = millis();
    state     = TState::Screen;
}

static void actCalMotors() {
    // The jogged positions are NOT reset with the flags. They are where the
    // canned faces are standing, and nothing on the machine puts a face back
    // because a menu item was picked — the Motor Sensors list would disagree
    // with this one about the same encoder if it did.
    for (int i = 0; i < kCalMotors; ++i) calAligned[i] = false;

    // Two faces already accepted before a detent has been turned. The firmware
    // starts with all six false, which is right there and useless here: the
    // list has four looks — cursor, squared, plain, error — and the two that
    // only appear once you have worked the page are the two nobody would ever
    // check. Up is squared AND under the cursor, which is the case the word
    // "squared" exists for; Front is the one that shows the mark.
    calAligned[0] = true;   // Up
    calAligned[2] = true;   // Front
    calSel = 0;

    // The same shape as the color prompt, and for the same reason: this is the
    // other calibration that cannot be started blind. There it is the cube's
    // ORIENTATION that nothing downstream can check; here it is the cube's
    // ABSENCE — the grippers close on the centre and then every face turns
    // four times, and a cube left in the machine is crushed or thrown.
    //
    // Not showScreen(): that stamps its own "SELECT or LEFT to go back", and
    // here SELECT starts a flow rather than dismissing a picture.
    live      = Live::CalPrompt;
    liveStart = millis();
    state     = TState::Screen;

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
        // Three passes, ~1.8 s each, with a rotation between them — the shape
        // of the real scan, played back against a solved cube's colors.
        const uint32_t cycle    = t % 7200;
        const int      pass     = (int)(cycle / 1800);
        const bool     rotating = (cycle % 1800) > 1200;

        int8_t faces[6] = { -1, -1, -1, -1, -1, -1 };
        const int done = rotating ? pass + 1 : pass;
        for (int p = 0; p < done && p < CubeSystem::kScanPasses; ++p) {
            for (int k = 0; k < 2; ++k) {
                const int f = CubeSystem::kScanPassFaces[p][k];
                faces[f] = CubeSystem::chipIndexForColor(kSolvedFace[f]);
            }
        }

        const bool reading = !rotating && pass < CubeSystem::kScanPasses;
        Cube.displayFaces(faces,
                          reading ? CubeSystem::kScanPassFaces[pass][0] : -1,
                          reading ? CubeSystem::kScanPassFaces[pass][1] : -1);
        cubeDisplay.setStatus(reading ? CubeSystem::kScanPassLabels[pass]
                                      : "Rotating cube");
        break;
    }

    case Live::Chips: {
        // Fill both rows out of step, the way the real calibration does: each
        // rotation feeds a different color to each board.
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

    case Live::Demo:
        updateDemoMode(t);
        break;

    case Live::Scramble:
        updateScrambleSolve(t);
        break;

    case Live::Fold:
        updateFold(t);
        break;

    case Live::IdleSolve:
        updateIdleSolve(t);
        break;

    case Live::StepScram:
        // Scramble, then compute, then hand over to the ribbon. The handover
        // is what makes this a phase of Step Solve rather than a screen of its
        // own: nothing is dismissed and nothing is picked, it just becomes the
        // next thing.
        if (t >= kStepScramMs + kStepComputeMs) {
            cubeScrambled = true;
            stepAt = 0;
            live   = Live::Step;
            drawStepSolve();
        } else {
            drawStepScramble(t);
        }
        break;

    case Live::SolveNet: {
        // The second frame, once the search has "finished". A full redraw
        // rather than a setStatus(), because the HINT changes too — that is
        // the whole difference between the two screens, and it is the half
        // worth judging: one is a machine thinking, the other is a machine
        // asking.
        //
        // One-shot, not a loop. live drops to None so the confirm just stands
        // there, exactly as it stands on the machine until somebody answers
        // it, and the net is not rebuilt twenty times a second for nothing.
        if (t >= kSolveThinkMs) {
            char sub[48];
            snprintf(sub, sizeof(sub), "Solution found in %d moves", kDoneMoves);
            drawSolveNet(sub, "SELECT to solve, LEFT to cancel");
            live = Live::None;
        }
        break;
    }

    case Live::CalClamp:
        // Three actuators moving, on the machine. Held rather than skipped
        // because the machine paints this screen, and a rehearsal that jumped
        // from the prompt straight to the list would be showing a flow the
        // machine does not have.
        if (t >= kCalClampMs) {
            calSel  = 0;
            calTick = 0;
            live    = Live::CalPick;
            drawCalList();
        }
        break;

    case Live::CalSave:
        if (t >= kCalSaveMs) {
            // The firmware's completion screen. Nothing in this phase carries
            // a progress bar, because calibrateMotorRotations() is a blocking
            // library call that reports nothing while it runs — a bar here
            // would promise progress the machine cannot actually show.
            showScreen(Op::Done, "Motor Calibration", "Motors calibrated");
        }
        break;

    // Live::Sensors, SensorRaw, Motors, MotorsHome and Input are NOT here.
    // Each of them answers the wheel and the buttons as well as a clock, so
    // each owns a branch of loop()'s Screen case instead — the same split the
    // firmware makes between an animated demo and an interactive page.

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

// Input for a jog page.
//
// Separate from pollEvent() because it needs the wheel and the UP/DOWN buttons
// to mean DIFFERENT things, and pollEvent() deliberately collapses them into
// one event — which is right for a menu and wrong here. Only one of the two
// runs per pass, so they can share the edge-detection state below.
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

// The dial page's per-pass handler. It needs the same split pollJog() gives
// the other two: the wheel steps one motor while the buttons stay buttons,
// and pollEvent() deliberately collapses those into one event.
//
// Kept down here beside pollJog() rather than up with the rest of the flow.
// Arduino generates prototypes, but the simulator compiles this .ino as plain
// C++ and generates none, so a call to pollJog() from above its definition
// would build on the bench and break in the sim. The firmware's calDialLoop()
// sits below its own pollJog() for exactly that reason.
static void calDialLoop() {
    const JogInput in = pollJog();

    if (in.back) { calLeaveDial(false); return; }

    if (in.select) {
        if (dialOwner == DialOwner::CalFlow) { calLeaveDial(true); return; }
        // Nothing to accept on the diagnostic — it records nothing — so SELECT
        // is one detent forward, exactly as it is on an armed motor row of the
        // Actuators page. Better a button that turns the motor than a button
        // that quietly does nothing on a screen whose whole risk is looking
        // like the calibration dial.
        calJog(+1);
        calTick = 0;                // redraw at once: the face just moved
    }

    // One detent is one step, however fast the wheel is spun — the tuning
    // editor's rule and the firmware dial's. There it is because a burst of
    // detents honoured at once turns a nudge into a lunge of real hardware;
    // here it is so the number climbs at the rate the real one would.
    const int d = (in.turn > 0) ? 1 : (in.turn < 0) ? -1
                : in.up ? 1 : in.down ? -1 : 0;
    if (d) {
        calJog(d);
        calTick = 0;                // redraw at once: the face just moved
    }

    if (millis() - calTick >= 50) {
        // ~20 Hz, the rate every live page here runs at. The firmware throttles
        // this because each pass is I2C traffic on the mux the wheel shares;
        // there is none behind a canned angle, but a number no eye can follow
        // is not worth redrawing faster either.
        calTick = millis();
        calDialTick();
    }
}

// Cube State's per-pass handler, third of the same kind. The wheel points at a
// face and UP/DOWN turn it, which pollEvent() cannot express — it collapses the
// two into one event, which is right for a menu and wrong for a page where
// pointing and turning are different verbs.
//
// Kept down here beside pollJog() for the reason calDialLoop() gives.
static void cubeStateLoop() {
    const JogInput in = pollJog();
    const int step = (in.turn > 0) ? 1 : (in.turn < 0) ? -1 : 0;

    // Both exits, because that is what this screen has always answered to and
    // the operator arriving from Cube Views is expecting a readout. Either way
    // the model goes back the way it was found FIRST — see csRestore().
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
        // UP is the plain turn and DOWN its prime, the same direction sense the
        // Actuators page gives the same two buttons over the same six faces. A
        // refused turn still repaints: the trail being full is said in the hint
        // bar, and a press that does nothing with no explanation is how a page
        // looks broken.
        csTurn(in.up ? +1 : -1);
        drawCubeState();
    }
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

    // Before anything can be shown or moved: the values a screen would display
    // and the positions a servo would sweep to both come from here.
    tuneLoadAll();

    Menu.begin(&kScreenMain, drawMenu);
    if (Cube.encoderInitialized) prevPos = menuEncoder.getPosition();
    state = TState::Menu;

    // The boot frame sat on the glass through both servo sweeps. Re-send every
    // pixel before the first menu, exactly as the firmware's setup() does.
    cubeDisplay.repaintAll();
}

void loop() {
    // Always service the display, whatever state we are in.
    Cube.displayUpdate();

    // A jog page owns the input while it is up: it needs the wheel and the
    // buttons separated, which pollEvent() cannot give it.
    if (state == TState::Jog) {
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
        return;
    }

    // The value editor needs the same split pollJog() gives the actuator page:
    // the wheel changes a number while the buttons stay buttons.
    if (state == TState::Params) {
        const JogInput in = pollJog();
        const int step = (in.turn > 0) ? 1 : (in.turn < 0) ? -1 : 0;

        // "Saved" is a receipt for the press just made, not a state of the
        // section, so any further press retires it.
        if (step || in.up || in.down || in.select || in.back) parSaved = false;

        // The Apply row has no TuneParam behind it. Every branch that touches
        // `p` below is one this rules out — the row cannot be entered, gated or
        // edited — so binding row 0 in its place is a placeholder, never a
        // value.
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
                    // The edit path is the ONE place a preview runs: the
                    // operator is watching, and the gate has already been
                    // shown. No set() here — the wheel moves the PART and the
                    // pending value, nothing else, until Apply. See the
                    // apply-gate essay above.
                    if (p.preview) p.preview(v);
                    if (p.flags & TP_LIVE) parMoved = true;
                    drawTune();
                }
            }
            return;
        }

        if (in.back) {
            // Never silently. Pending edits get the red confirm; a clean
            // section simply leaves.
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
                // A toggle has no range to scroll through, so edit mode would
                // be a press to enter, a press to flip and a press to leave.
                // Flip it.
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
        return;
    }

    // The motor dial, third of the same kind: it steps one face motor with the
    // wheel, so it owns the input while it is up. Two pages open it — see
    // DialOwner — and both hand it the input the same way.
    if (state == TState::MotorDial) {
        calDialLoop();
        return;
    }

    // Cube State, fourth of the same kind: the wheel points at a face while
    // UP/DOWN turn it, so it owns the input while it is up.
    if (state == TState::CubeState) {
        cubeStateLoop();
        return;
    }

    const MenuEvent ev = pollEvent();

    switch (state) {

    case TState::Menu:
        // The color everything downstream of this press will wear. Read from
        // the row under the cursor BEFORE handle() runs it, because an action
        // draws its first screen from inside handle() and the menu has moved on
        // by the time it returns.
        //
        // This one line is the whole wayfinding mechanism, lifted from the
        // firmware unchanged: a branch's color reaches its screens because the
        // row that opened them said so, not because two dozen demo functions
        // each remembered to. Set on submenu entries too, harmlessly — they
        // draw no operation screen, and the next Select overwrites it.
        if (ev == MenuEvent::Select) {
            s_opTheme = CubeMenu::themeOf(Menu.current(), Menu.selectedItem());
        }

        Menu.handle(ev);
        // Only redraw while the menu still has the panel: an action may have
        // moved us to a screen, and repainting the list over it would leave the
        // panel lying about what is on it.
        if (state == TState::Menu) Menu.render();
        break;

    case TState::Jog:
    case TState::Params:
    case TState::MotorDial:
    case TState::CubeState:
        break;      // handled above; nothing here consumes a MenuEvent

    case TState::ParamsLeave:
        // The answer to drawParamsLeave()'s red confirm. SELECT is the
        // destructive answer, as it is on Reset Defaults; LEFT goes back to the
        // list with every pending value — and every previewed part — still
        // exactly where it was left.
        if (ev == MenuEvent::Select) {
            parDiscard();
        } else if (ev == MenuEvent::Back) {
            state = TState::Params;
            drawTune();
        }
        break;

    case TState::Screen:
        if (resetConfirm) {
            if (ev == MenuEvent::Select) {
                resetConfirm = false;
                tuneResetAll();
                showScreen(Op::Done, "Reset Defaults", "Tuning reset");
            } else if (ev == MenuEvent::Back) {
                resetConfirm = false;
                toMenu();
            }
        } else if (live == Live::Faults && (ev == MenuEvent::Up || ev == MenuEvent::Down)) {
            // Clamped, not wrapped: a log has a top and a bottom, and wrapping
            // from the oldest entry back to the newest would misread as more
            // history than there is.
            const int span = kFaultCount - CubeDisplay::kOpLines;
            int top = faultTop + (ev == MenuEvent::Down ? 1 : -1);
            if (top < 0)    top = 0;
            if (top > span) top = span;
            faultTop = (int8_t)top;
            drawFaultLog();
        } else if (live == Live::Idle && (ev == MenuEvent::Up || ev == MenuEvent::Down)) {
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
                // out. Turning the wheel down to 1 s and then waiting 30 s for
                // the next move would read as the setting not working.
                idleNextAt = millis() + (uint32_t)idleGapS * 1000UL;
            }
            drawIdle();
        } else if (live == Live::Idle && ev == MenuEvent::Select) {
            actIdleSolve();
        } else if (live == Live::Sensors) {
            if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
                // Wrapped, as the menu wraps: 18 stops is too many to bump
                // along an end stop. On the machine the move repaints and reads
                // NOTHING that pass, so a wheel spun across the grid costs no
                // integration windows at all.
                senSel = (int8_t)((senSel + (ev == MenuEvent::Down ? 1 : 17)) % 18);
                drawSensors();
            } else if (ev == MenuEvent::Select) {
                lastLive = 0;                       // read the chosen one at once
                live = Live::SensorRaw;             // drill into the one selected
                drawSensorRaw();
            } else if (ev == MenuEvent::Back) {
                toMenu();
            } else if (senSeen != kSenAllSeen && millis() - lastLive >= kSenSweepMs) {
                // The opening sweep, and only the opening sweep. Once every
                // cell has answered the page stops ticking for good — the
                // firmware's does the same, because a background sweep would
                // steal the held mux channel from the cursor every tick.
                lastLive = millis();
                sensorsTick();
            }
        } else if (live == Live::SensorRaw) {
            if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
                // The wheel walks the sensors from in HERE, instead of making
                // the operator back out to move and come in again. Same
                // numbering and same wrap as the grid, so backing out lands the
                // cursor on the sensor that was being read.
                senSel = (int8_t)((senSel + (ev == MenuEvent::Down ? 1 : 17)) % 18);
                lastLive = 0;
                drawSensorRaw();
            } else if (ev == MenuEvent::Back || ev == MenuEvent::Select) {
                // BOTH go back to the grid, one level up. There is nothing
                // deeper than this screen to enter, so the only thing SELECT
                // can honestly mean is what LEFT means.
                lastLive = 0;
                live = Live::Sensors;
                drawSensors();
            } else if (millis() - lastLive >= 200) {
                // Slower than the grid on purpose: four numbers changing
                // sixteen times a second cannot be read.
                lastLive = millis();
                sensorRawTick();
            }
        } else if (live == Live::Motors) {
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
                // The ring row falls through deliberately — cubeMotors drives
                // the ring to named states, not by steps, so there is nothing
                // for a wheel to turn. Its hint bar says so, which is the
                // difference between a row that cannot be entered and a button
                // that looks broken.
            } else if (ev == MenuEvent::Back) {
                toMenu();
            } else if (millis() - lastLive >= 100) {
                // Seven reads per tick is cheap on the machine, but unthrottled
                // they would saturate the encoder mux bus for a screen no
                // faster than the eye can read anyway.
                lastLive = millis();
                motorsRows();
            }
        } else if (live == Live::MotorsHome) {
            // A canned dwell, not a run: see motorsHome(). The chord in the
            // hint bar has nothing to abort here, and is left saying what the
            // machine says because this screen is the thing being judged.
            if (millis() - liveStart >= kMotHomeMs) {
                lastLive = 0;
                live = Live::Motors;
                drawMotors();
            }
        } else if (live == Live::Input) {
            // 'ev' is deliberately ignored, unlike every other screen here.
            // This page exists to show what each button does, and exiting on
            // SELECT or Back meant three of the five buttons — SELECT, LEFT and
            // RIGHT, which pollEvent() folds into Select — left the screen the
            // moment you tested them. Only the chord gets out, and
            // updateInputReport() has to find it: pollEvent() swallows it.
            if (millis() - lastLive >= 50) {
                lastLive = millis();
                if (updateInputReport()) toMenu();
            }
        } else if (live == Live::Step && ev == MenuEvent::Select) {
            // SELECT means "next move" here, not "done looking". Only LEFT
            // leaves, which is the one meaning it has everywhere.
            if (stepAt < 20) { stepAt++; drawStepSolve(); }
            else             { cubeScrambled = false; toMenu(); }
        } else if (live == Live::CalPrompt) {
            // The confirm owns the input while it is up: SELECT starts the
            // flow, LEFT is the only way out. Nothing else is reachable from
            // here, which is the point of asking.
            if (ev == MenuEvent::Select)    calToClamp();
            else if (ev == MenuEvent::Back) toMenu();
        } else if (live == Live::CalPick) {
            // The firmware's CalMotorsPick, less the abort exit that releases
            // the grippers on the way out — there are none shut here.
            if (ev == MenuEvent::Up || ev == MenuEvent::Down) {
                // Wraps, as the Actuators page and the menu wrap: rolling off
                // the Save row back to U is a shorter trip than winding up.
                const int step = (ev == MenuEvent::Down) ? 1 : kCalRows - 1;
                calSel = (int8_t)((calSel + step) % kCalRows);
                drawCalList();
            } else if (ev == MenuEvent::Select) {
                if (calSel == kCalSave) calToSave();
                else                    calEnterDial(DialOwner::CalFlow);
            } else if (ev == MenuEvent::Back) {
                toMenu();
            } else if (millis() - calTick >= 250) {
                // Four times a second, the firmware's rate: six encoder reads
                // a tick is real traffic on the mux the wheel shares, and it
                // is already faster than seven numbers can be read.
                calTick = millis();
                calListRows();
            }
        } else if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
            toMenu();
        } else if (live == Live::Idle) {
            // Compared as a difference rather than millis() >= idleNextAt, so
            // the 49-day rollover is a non-event instead of a machine that
            // stops turning until someone reboots it.
            if ((int32_t)(millis() - idleNextAt) >= 0) idleTurn();
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
