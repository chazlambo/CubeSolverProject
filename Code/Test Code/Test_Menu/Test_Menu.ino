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
//  Tuning edits the real values and SAVES them to EEPROM on leaving a section.
//  Defaults live in the shared kTune table (CubeTuneTable — one copy for this
//  sketch and the firmware); Diagnostics > Tuning > Reset Defaults puts every
//  one of them back.
//
//  THE TREE
//  --------
//    Load Cube / Eject Cube      real servos
//    Actuators                   real everything, one part at a time
//    Screens > Operations        scan, color capture
//            > Modes             the firmware's five Modes, same order
//            > Cube Views        the unfolded net
//            > Messages          info, error, stats
//            > Navigation        menu rendering at every item count
//    Diagnostics > Tuning        six sections, saved to EEPROM
//                > Input Report, Color Sensors, Motor Sensors, Fault Log
//
//  Screens > Modes deliberately mirrors the firmware's Modes menu item for item
//  and order for order. A rehearsal that groups the screens differently from
//  the machine stops being a rehearsal of the machine.
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
//    SELECT + LEFT         swallowed as the abort chord, never a menu action
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
    Loading,    // clamp the cube, once
    Ejecting    // release and present it, once
};

static TState state = TState::Menu;

// A screen that redraws itself every pass (the live input report), or animates
// from canned data (the operation-screen demos).
enum class Live : uint8_t { None, Input, Steps, Chips, Scramble, Fold, Step, Demo,
                            Sensors, SensorRaw, Motors, Faults, Idle, IdleSolve,
                            StepScram };
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
static void actDemoInfo();
static void actDemoSteps();
static void actDemoChips();
static void actDemoError();
static void actDemoScramble();
static void actStepSolve();
static void actStats();
static void actIdleMode();
static void actDemoMode();
static void actDemoNetSolved();
static void actDemoNetScrambled();
static void actDemoNetLoad();
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

static const char* const kPrevNav[]     = { "3 items", "4 items", "5 items", "Long" };
static const char* const kPrevScreens[] = { "Operations", "Modes", "Cube views",
                                            "Messages", "Navigation" };

// The pattern previews come from CubeSystem::kPatternNets — the same tables
// the firmware folds from, computed by running each sequence through
// VirtualCube rather than drawn by hand, and checked for nine of each color.
// This sketch used to carry its own copies; they turned out to have been
// computed in a different frame from the one the model actually builds,
// which is exactly the drift owning a second copy invites.
static const char* const kPrevOps[]   = { "Faces", "Chips" };
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

// One caption per sticker on a color board. Nine of them, in the order the
// sensors are read.
static const char* const kSensorCaps[9] = { "1","2","3","4","5","6","7","8","9" };
static const char* const kPrevCube[]    = { "Solved", "Scrambled", "Load" };
static const char* const kPrevDeep[]    = { "Deeper", "and", "deeper" };

// ---- root ----
static const MenuItem kMainItems[] = {
    { "Load Cube",   nullptr,         actLoad,  "Clamp the cube. Moves servos.",
      nullptr, 0, MenuTheme::Blue },
    { "Eject Cube",  nullptr,         actEject, "Release and present it.",
      nullptr, 0, MenuTheme::Violet },
    { "Actuators",   nullptr,         actJog,   "Drive every part by hand.",
      nullptr, 0, MenuTheme::Red },
    { "Screens",     &kScreenScreens, nullptr,  "Draw the panel, no hardware.",
      kPrevScreens, 5, MenuTheme::Yellow },
    { "Diagnostics", &kScreenDiag,    nullptr,  "Navigation and input.",
      kPrevDiag, 2, MenuTheme::Purple },
};
static const MenuScreen kScreenMain = { "Menu Test", kMainItems, 5, MenuTheme::Green };

// ---------------------------------------------------------------------------
//  Actuators — the panel version of Test Code/Actuator_Test
// ---------------------------------------------------------------------------
//  Direct manual control of every moving part, one position or one turn at a
//  time. This is the bench tool for "does that servo actually reach retract"
//  and for showing the machine off a piece at a time, and it replaces squinting
//  at a numbered list over Serial.
//
//  THESE MOVE REAL HARDWARE. Everything else in this sketch is drawing.
//
//  The tree is grouped by part rather than flattened, because five items is the
//  screen limit and there are six face motors. Opposite faces share a screen —
//  they are the same axis, so it is a grouping that means something rather than
//  an arbitrary split.
static const MenuItem kDiagItems[] = {
    { "Tuning",         &kScreenTuning, nullptr,    "Values you can change.",
      kPrevTuning, 2, MenuTheme::Yellow },
    { "Input Report", nullptr,     actInputReport, "Live wheel and buttons.",
      nullptr, 0, MenuTheme::Purple },
    { "Color Sensors", nullptr,   actColorSensors, "Live, per board.",
      nullptr, 0, MenuTheme::Blue },
    { "Motor Sensors",  nullptr,   actMotorSensors,  "Raw encoder angles.",
      nullptr, 0, MenuTheme::Green },
    { "Fault Log",      nullptr,   actFaultLog,      "What went wrong, recently.",
      nullptr, 0, MenuTheme::Red },
};
const MenuScreen kScreenDiag = { "Diagnostics", kDiagItems, 5, MenuTheme::Purple };

// Tuning needed a slot and Diagnostics was full at the five-item limit, so
// Navigation moved to Screens. It belongs there anyway: it exercises the menu
// RENDERER at every item count and tells you nothing about the machine, which
// is what everything else under Diagnostics is for.
static const MenuItem kTuningItems[] = {
    { "Servos",       &kScreenServoTune, nullptr, "Positions set by eye.",
      kPrevServoT, 3, MenuTheme::Violet },
    { "Face Motors",  nullptr, actFaceMot,  "Speed and settling time.",
      nullptr, 0, MenuTheme::Blue },
    { "Alignment",    nullptr, actAlignPar, "How square is square enough.",
      nullptr, 0, MenuTheme::Green },
    { "Color",        nullptr, actColorPar, "How a sticker is judged.",
      nullptr, 0, MenuTheme::Yellow },
    { "Reset Defaults", nullptr, actResetTune, "Throw away every change.",
      nullptr, 0, MenuTheme::Red },
};
const MenuScreen kScreenTuning = { "Tuning", kTuningItems, 5, MenuTheme::Yellow };

// The three that move something you can watch, kept together and away from the
// numbers that only take effect on the next move.
static const MenuItem kServoTuneItems[] = {
    { "Top Servo",    nullptr, actTopServo, "Grips from above.",
      nullptr, 0, MenuTheme::Violet },
    { "Bottom Servo", nullptr, actBotServo, "Grips, centres and ejects.",
      nullptr, 0, MenuTheme::Blue },
    { "Ring",         nullptr, actRingPos,  "Stepper, not a servo.",
      nullptr, 0, MenuTheme::Green },
};
const MenuScreen kScreenServoTune = { "Servos", kServoTuneItems, 3, MenuTheme::Violet };

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
//
// Split three ways rather than crammed into one screen: five items is the
// design limit, and this was already at it. A screen that wants a sixth wants
// splitting, which is the same rule the real menu follows.
static const MenuItem kScreensItems[] = {
    { "Operations",  &kScreenOps,  nullptr, "Progress while working.",
      kPrevOps,  2, MenuTheme::Blue },
    { "Modes",       &kScreenModes, nullptr, "The five ways to run it.",
      kPrevModes, 5, MenuTheme::Red },
    { "Cube Views",  &kScreenCube, nullptr, "The unfolded cube net.",
      kPrevCube, 3, MenuTheme::Green },
    { "Messages",    &kScreenMsg,  nullptr, "Status, faults and records.",
      kPrevMsg,  3, MenuTheme::Violet },
    { "Navigation",  &kScreenNav,  nullptr, "Screens of every size.",
      kPrevNav,  4, MenuTheme::Red },
};
const MenuScreen kScreenScreens = { "Screens", kScreensItems, 5, MenuTheme::Yellow };

// Deliberately the SAME five items, in the same order, as the firmware's Modes
// menu. This sketch is the rehearsal for that tree, and a demo that groups the
// screens differently from the machine stops being a rehearsal of anything.
static const MenuItem kModesItems[] = {
    { "Scramble Solve", nullptr, actDemoScramble, "Two phases, two colors." },
    { "Idle Mode",      nullptr, actIdleMode,     "Awake, waiting, worth a glance." },
    { "Demo Mode",      nullptr, actDemoMode,     "Scramble and solve, looping." },
    { "Step Solve",     nullptr, actStepSolve,    "One move per press." },
    { "Patterns",       &kScreenPatterns, nullptr, "Previews in the side pane.",
      nullptr, 0, MenuTheme::Violet },
};
const MenuScreen kScreenModes = { "Modes", kModesItems, 5, MenuTheme::Red };

// The point of this screen: the preview pane shows what each pattern PRODUCES.
// A list of names would say nothing about what you are choosing between.
//
// Item order IS table order: each row's previewNet indexes the shared
// kPattern* tables by position, and actFoldPattern() reuses selectedIndex()
// the same way — the same contract as the firmware's Patterns screen, which
// this one rehearses row for row.
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
const MenuScreen kScreenPatterns = { "Patterns", kPatternItems, 4, MenuTheme::Violet };

// The two that are not modes: what the machine draws while it is scanning, and
// while it is learning colors. The rest moved to Modes.
static const MenuItem kOpsItems[] = {
    { "Scan Faces",  nullptr, actDemoSteps, "Faces fill as they are read." },
    { "Color Chips", nullptr, actDemoChips, "Two boards, six colors." },
};
const MenuScreen kScreenOps = { "Operations", kOpsItems, 2, MenuTheme::Blue };

static const MenuItem kCubeItems[] = {
    { "Solved Cube",     nullptr, actDemoNetSolved,    "Every face one color." },
    { "Scrambled Cube",  nullptr, actDemoNetScrambled, "Checkerboard, 9 of each." },
    { "Load Orientation",nullptr, actDemoNetLoad,      "The calibration prompt." },
};
const MenuScreen kScreenCube = { "Cube Views", kCubeItems, 3, MenuTheme::Green };

static const MenuItem kMsgItems[] = {
    { "Info Panel",   nullptr, actDemoInfo,  "Aligned label/value rows." },
    { "Error Screen", nullptr, actDemoError, "The red stopped look." },
    { "Stats",        nullptr, actStats,     "Solve records.",
      nullptr, 0, MenuTheme::Purple },
};
const MenuScreen kScreenMsg = { "Messages", kMsgItems, 3, MenuTheme::Red };

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
//  Jog pages — direct actuator control
// ---------------------------------------------------------------------------
//  A menu is the wrong shape for this. Driving a servo through Extend, Partial
//  and Retract is nine menu entries across three screens once the ring is in
//  too, and the face motors are twelve more — all of it clicked through one
//  item at a time, when what you actually want is to pick a thing and nudge it
//  while you watch it move.
//
//  So these two pages are direct manipulation instead, and they can be because
//  the wheel and the UP/DOWN BUTTONS are separate inputs on this encoder. The
//  menu collapses them into one meaning; here they get two:
//
//      wheel        choose which part
//      UP / DOWN    move that part
//      LEFT         back, exactly as everywhere else
//
//  One page for the grippers, one for the faces, and no submenu below either.
// One selector over the whole machine: three grippers, six face motors, two
// whole-cube rotations. Eleven things, no submenus, and the wheel wraps — so
// nothing is more than five or six detents away.
//
// Keeping them on ONE page is not only tidiness. A face motor cannot turn until
// the grippers are clear, so being able to see where the grippers are WHILE
// jogging a face is the difference between a considered press and a jam.
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

static const char* const kGripName[3] = { "Top servo", "Bottom servo", "Ring" };
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

static const char* const kFaceName[6] = { "Up", "Right", "Front", "Down", "Left", "Back" };

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

    cubeDisplay.showOperation(jogArmed ? Op::Info : Op::Calibrate,
                              "Actuators", nullptr, hint);
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
        gripAt[0] = gripAt[1] = gripAt[2] = 2;      // all extended
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
    jogSel   = 0;
    jogArmed = false;
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
//  duplicated. (See CubeTuneTable.h for the accessors-not-globals and
//  frozen-order essays that used to live here.)
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
    cubeDisplay.showOperation(Op::Error, "Reset Defaults",
                              "Discard all tuning?",
                              "SELECT to reset - LEFT to keep");
    cubeDisplay.setStatus("Every value goes back to compiled");
    Cube.displayUpdate();
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
//  Sensor screens
// ---------------------------------------------------------------------------
//  Split in two because they answer different questions. The color boards want
//  "is any sensor disagreeing with its neighbours", which is a picture. The
//  motor encoders want "what angle is each one reading", which is a list of
//  numbers.
//
//  The readings here are canned but MOVING, so the screens are exercised as live
//  ones rather than stills. The firmware's Sensor Test reads them for real:
//  colorSensorN.scanSingle(i) fills currentRGBW — four ints, R G B W; NOT the
//  getScanValRow() row, which serves scanFace()'s medians and a single-sensor
//  scan never touches — classify() judges it, and MotorEncoders[i]->scan()
//  returns a raw 12-bit angle or a negative I2C error.
static int8_t senSel = 0;          // 0..17 across both boards, or the raw view

static void senReadings(uint32_t t, int8_t* b1, int8_t* b2) {
    const int phase = (int)(t / 900);
    for (int i = 0; i < 9; ++i) {
        b1[i] = (int8_t)((i + phase) % 6);
        // Board 2 sensor 2 has a dead green channel on this machine (README),
        // so it never resolves to a color. A diagnostic that only ever shows
        // healthy hardware is not a diagnostic.
        b2[i] = (i == 1) ? (int8_t)-1 : (int8_t)((i + phase + 3) % 6);
    }
}

static void actColorSensors() {
    senSel = 0;
    showScreen(Op::Scan, "Color Sensors", nullptr, Live::Sensors);
}

static void updateColorSensors(uint32_t t) {
    static const char* rows[2] = { "Board 1\t9/9 healthy, sep 165",
                                   "Board 2\t8/9 healthy, sep 3" };
    static const CubeDisplay::RowMark marks[2] = { CubeDisplay::RowMark::Good,
                                                   CubeDisplay::RowMark::Bad };
    int8_t b1[9], b2[9];
    senReadings(t, b1, b2);

    char hint[40];
    snprintf(hint, sizeof(hint), "SELECT for board %d sensor %d",
             (senSel < 9) ? 1 : 2, (senSel % 9) + 1);

    cubeDisplay.showOperation(Op::Scan, "Color Sensors", nullptr, hint);
    cubeDisplay.setOpLines(rows, 2, marks);
    cubeDisplay.setOpChipRow(0, b1, kSensorCaps, 9, (senSel < 9) ? senSel : -1, 104);
    cubeDisplay.setOpChipRow(1, b2, nullptr,     9, (senSel < 9) ? -1 : senSel - 9, 140);
    Cube.displayUpdate();
}

// One sensor, in the numbers behind the color. This is the screen for "why did
// it call that sticker orange" — the classification is a judgement made from
// four values, and until you can see them the answer is a guess.
static void updateSensorRaw(uint32_t t) {
    const int board = (senSel < 9) ? 1 : 2;
    const int idx   = senSel % 9;
    const int phase = (int)(t / 900);

    // Stand-ins with the shape of real readings: a dominant channel, a white
    // channel that tracks the sum, and slow drift so the screen looks live.
    const int8_t col = (board == 2 && idx == 1) ? -1 : (int8_t)((idx + phase + (board - 1) * 3) % 6);
    int rgbw[4] = { 300, 300, 300, 900 };
    if (col >= 0) {
        static const int kBoost[6][3] = {
            {700,700,700}, {800,750,200}, {900,250,220},
            {880,480,200}, {250,780,300}, {230,300,820},
        };
        for (int k = 0; k < 3; ++k) rgbw[k] = kBoost[col][k] + (int)(t / 120) % 40;
        rgbw[3] = rgbw[0] + rgbw[1] + rgbw[2];
    }

    static char rows[5][40];
    const char* lines[5];
    CubeDisplay::RowMark marks[5] = { CubeDisplay::RowMark::Plain };
    static const char* kChan[4] = { "Red", "Green", "Blue", "White" };
    for (int k = 0; k < 4; ++k) {
        snprintf(rows[k], sizeof(rows[k]), "%s\t%d", kChan[k], rgbw[k]);
        lines[k] = rows[k];
        marks[k] = CubeDisplay::RowMark::Plain;
    }
    static const char* const kLetters[6] = { "White", "Yellow", "Red",
                                             "Orange", "Green", "Blue" };
    snprintf(rows[4], sizeof(rows[4]), "Reads as\t%s",
             (col >= 0) ? kLetters[col] : "unusable");
    lines[4] = rows[4];
    marks[4] = (col >= 0) ? CubeDisplay::RowMark::Good : CubeDisplay::RowMark::Bad;

    char title[32];
    snprintf(title, sizeof(title), "Board %d  Sensor %d", board, idx + 1);
    cubeDisplay.showOperation(Op::Scan, title, nullptr, "LEFT to go back");
    cubeDisplay.setOpLines(lines, 5, marks);
    Cube.displayUpdate();
}

// Seven encoders, seven numbers. Nothing here is a picture, because an angle is
// not one — what you are checking is whether a value moves when you turn a face,
// and whether any of them is reporting an I2C error instead.
static void actMotorSensors() {
    showScreen(Op::Solve, "Motor Sensors", nullptr, Live::Motors);
}

static void updateMotorSensors(uint32_t t) {
    static const char* const kMotor[7] = { "Up", "Right", "Front", "Down",
                                           "Left", "Back", "Ring" };
    static char rows[7][40];
    const char* lines[7];
    CubeDisplay::RowMark marks[7];

    for (int i = 0; i < 7; ++i) {
        // scan() returns a raw 12-bit angle, or a negative I2C error. Showing
        // the error rather than a plausible number is the point of the screen.
        const int raw = (int)((t / 3 + i * 517) % 4096);
        snprintf(rows[i], sizeof(rows[i]), "%s\t%d", kMotor[i], raw);
        lines[i] = rows[i];
        marks[i] = CubeDisplay::RowMark::Plain;
    }

    cubeDisplay.showOperation(Op::Solve, "Motor Sensors", nullptr,
                              "raw angle 0-4095   LEFT back");
    cubeDisplay.setOpLines(lines, 7, marks);
    Cube.displayUpdate();
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

    cubeDisplay.showOperation(Op::Info, "Fault Log", nullptr, hint);
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


// A scramble followed by a solve. The frame carries the phase — red while it is
// scrambling, green the moment it starts solving — which is the whole idea the
// real Scramble Solve is meant to prove, and the reason setOpKind() exists.
static void actDemoScramble() {
    showScreen(Op::Error, "Scramble Solve", "Scrambling", Live::Scramble);
}

// The six cube colors, in the order CubeDisplay's chips use them.
static const char kNetOrder[6] = { 'W', 'Y', 'R', 'O', 'G', 'B' };

// A solved cube in net order (U R F D L B). Same scheme the simulator scans.
static void buildSolvedNet(char* net) {
    static const char kFace[6] = { 'W', 'R', 'G', 'Y', 'O', 'B' };
    for (int f = 0; f < 6; ++f)
        for (int k = 0; k < 9; ++k) net[f * 9 + k] = kFace[f];
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
static void actDemoNetScrambled() {
    static const char kOpp[6] = { 'Y', 'W', 'O', 'R', 'B', 'G' };   // vs kNetOrder
    char net[CubeDisplay::kNetFacelets];
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

// Folding a pattern, animated from the firmware's own tables: the fold moves
// march the ribbon at the machine's pace, then the screen becomes the
// firmware's completion shape — Done frame, the pattern's net, its name on
// the line beneath. Moves, counts and nets are all CubeSystem's copy, so
// this rehearsal cannot drift from what the machine folds.
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
    cubeDisplay.setOpKind(Op::Done);
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
    cubeDisplay.showOperation(Op::Solve, "Step Solve", head,
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
    if (computing) {
        cubeDisplay.setOpKind(Op::Info);              // yellow: thinking
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
        cubeDisplay.setOpKind(Op::Error);             // red: disordering it
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
    showScreen(Op::Error, "Step Solve", "Scrambling", Live::StepScram);
    drawStepScramble(0);
}

// Scramble, solve, repeat, unattended. Both halves already existed — the phase
// colors from Scramble Solve, the ribbon from Step Solve — so this is a loop
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
    showScreen(Op::Error, "Demo Mode", "Scrambling", Live::Demo);
}

static void updateDemoMode(uint32_t t) {
    const uint32_t cycle = t % kDemoCycleMs;
    const int      run   = (int)(t / kDemoCycleMs) + 1;

    char sub[52];
    if (cycle < kDemoScrambleMs) {
        const int m = (int)((cycle * CubeSystem::kScrambleLen) / kDemoScrambleMs);
        cubeDisplay.setOpKind(Op::Error);                  // red: scrambling
        cubeDisplay.setMessage("Scrambling");
        snprintf(sub, sizeof(sub), "Run %d   -   Move %d of %d",
                 run, m + 1, CubeSystem::kScrambleLen);
        cubeDisplay.setOpRibbon(kScrambleMoves, CubeSystem::kScrambleLen, m);
        Cube.displayProgress(m, CubeSystem::kScrambleLen - 1);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs) {
        // The handover. The frame turns green here rather than at the first
        // solve move, because this is the moment it stops scrambling — and the
        // ribbon and the bar go, because neither has anything true to say about
        // a search that has not finished.
        cubeDisplay.setOpKind(Op::Solve);
        cubeDisplay.setMessage("Computing the solution");
        snprintf(sub, sizeof(sub), "Run %d", run);
        cubeDisplay.setOpRibbon(nullptr, 0, 0);
        Cube.displayProgress(0, 0);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs + kDemoSolveMs) {
        const int m = (int)(((cycle - kDemoScrambleMs - kDemoComputeMs) * 21)
                            / kDemoSolveMs);
        cubeDisplay.setOpKind(Op::Solve);                  // green: solving
        cubeDisplay.setMessage("Solving");
        snprintf(sub, sizeof(sub), "Run %d   -   Move %d of 21", run, m + 1);
        cubeDisplay.setOpRibbon(kStepMoves, 21, m);
        Cube.displayProgress(m, 20);
    } else {
        cubeDisplay.setOpKind(Op::Done);
        cubeDisplay.setMessage("Solved!");
        snprintf(sub, sizeof(sub), "Run %d   -   21 moves in 4.62 s", run);
        cubeDisplay.setOpRibbon(kStepMoves, 21, 20);
        Cube.displayProgress(20, 20);
    }
    cubeDisplay.setStatus(sub);
}

// The firmware's Scramble Solve, rehearsed: one run of Demo Mode's phases
// without the run counter, painting the status lines the firmware paints —
// red scramble with ribbon and bar, green "Computing" with the furniture
// cleared, green solve, then "Solved!". It loops like every other demo. The
// machine holds at Solved! until SELECT, but the handovers are what this
// rehearsal is for, and looping lets them be watched without re-picking the
// item.
static void updateScrambleSolve(uint32_t t) {
    const uint32_t cycle = t % kDemoCycleMs;

    char sub[48];
    if (cycle < kDemoScrambleMs) {
        const int m = (int)((cycle * CubeSystem::kScrambleLen) / kDemoScrambleMs);
        cubeDisplay.setOpKind(Op::Error);                  // red: disordering it
        cubeDisplay.setMessage("Scrambling");
        snprintf(sub, sizeof(sub), "Move %d of %d   %s",
                 m + 1, CubeSystem::kScrambleLen, kScrambleMoves[m]);
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(kScrambleMoves, CubeSystem::kScrambleLen, m);
        cubeDisplay.setOpProgress(m, CubeSystem::kScrambleLen);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs) {
        // The handover: green the moment it stops scrambling, and the
        // scramble's furniture goes — a finished ribbon and a full bar under
        // "Computing" read as a solve that finished before it started.
        cubeDisplay.setOpKind(Op::Solve);
        cubeDisplay.setMessage("Computing the solution");
        cubeDisplay.setStatus("");
        cubeDisplay.setOpRibbon(nullptr, 0, -1);
        cubeDisplay.setOpProgress(0, 0);
    } else if (cycle < kDemoScrambleMs + kDemoComputeMs + kDemoSolveMs) {
        const int m = (int)(((cycle - kDemoScrambleMs - kDemoComputeMs) * 21)
                            / kDemoSolveMs);
        cubeDisplay.setOpKind(Op::Solve);                  // green: solving
        cubeDisplay.setMessage("Solving");
        snprintf(sub, sizeof(sub), "Move %d/%d   %s", m + 1, 21, kStepMoves[m]);
        cubeDisplay.setStatus(sub);
        cubeDisplay.setOpRibbon(kStepMoves, 21, m);
        cubeDisplay.setOpProgress(m, 21);
    } else {
        cubeDisplay.setOpKind(Op::Done);
        cubeDisplay.setMessage("Solved!");
        cubeDisplay.setStatus("21 moves in 4.62 s");
        cubeDisplay.setOpRibbon(kStepMoves, 21, 20);
        cubeDisplay.setOpProgress(21, 21);
    }
}

// ---------------------------------------------------------------------------
//  Stats
// ---------------------------------------------------------------------------
//  Six rows, which is exactly enough — resist a seventh. The numbers here are
//  canned: the EEPROM block behind the real screen exists now (CubeStats,
//  whose header carries the why-its-own-block design brief this comment used
//  to hold), but the only question this demo answers on the bench is whether
//  the label/value columns line up at realistic widths.
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
    cubeDisplay.showOperation(Op::Solve, "Idle", "Ready",
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
        static const char kFaceColor[6] = { 'W', 'R', 'G', 'Y', 'O', 'B' };
        const uint32_t cycle    = t % 7200;
        const int      pass     = (int)(cycle / 1800);
        const bool     rotating = (cycle % 1800) > 1200;

        int8_t faces[6] = { -1, -1, -1, -1, -1, -1 };
        const int done = rotating ? pass + 1 : pass;
        for (int p = 0; p < done && p < CubeSystem::kScanPasses; ++p) {
            for (int k = 0; k < 2; ++k) {
                const int f = CubeSystem::kScanPassFaces[p][k];
                faces[f] = CubeSystem::chipIndexForColor(kFaceColor[f]);
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

    case Live::Sensors:
        updateColorSensors(t);
        break;

    case Live::SensorRaw:
        updateSensorRaw(t);
        break;

    case Live::Motors:
        updateMotorSensors(t);
        break;

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

// Input for a jog page.
//
// Separate from pollEvent() because it needs the wheel and the UP/DOWN buttons
// to mean DIFFERENT things, and pollEvent() deliberately collapses them into
// one event — which is right for a menu and wrong here. Only one of the two
// runs per pass, so they can share the edge-detection state below.
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
        return;
    }

    // The value editor needs the same split pollJog() gives the actuator page:
    // the wheel changes a number while the buttons stay buttons.
    if (state == TState::Params) {
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
                    // The edit path is the ONE place a preview runs: the
                    // operator is watching, and the gate has already been
                    // shown. Boot and reset call set() alone.
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
        return;
    }

    const MenuEvent ev = pollEvent();

    switch (state) {

    case TState::Menu:
        Menu.handle(ev);
        // Only redraw while the menu still has the panel: an action may have
        // moved us to a screen, and repainting the list over it would leave the
        // panel lying about what is on it.
        if (state == TState::Menu) Menu.render();
        break;

    case TState::Jog:
    case TState::Params:
        break;      // handled above; nothing here consumes a MenuEvent

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
        } else if (live == Live::Sensors && (ev == MenuEvent::Up || ev == MenuEvent::Down)) {
            senSel = (int8_t)((senSel + (ev == MenuEvent::Down ? 1 : 17)) % 18);
            updateColorSensors(millis() - liveStart);
        } else if (live == Live::Sensors && ev == MenuEvent::Select) {
            live = Live::SensorRaw;                 // drill into the one selected
        } else if (live == Live::SensorRaw && ev == MenuEvent::Back) {
            live = Live::Sensors;                   // and back out to the boards
        } else if (live == Live::Step && ev == MenuEvent::Select) {
            // SELECT means "next move" here, not "done looking". Only LEFT
            // leaves, which is the one meaning it has everywhere.
            if (stepAt < 20) { stepAt++; drawStepSolve(); }
            else             { cubeScrambled = false; toMenu(); }
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
