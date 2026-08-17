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
//  THE ACTUATORS MENU AND Load/Eject MOVE REAL HARDWARE. Everything under
//  Screens and Diagnostics is drawing and navigation only.
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
    Jog,        // direct actuator control; see the note above pollJog()
    Loading,    // clamp the cube, once
    Ejecting    // release and present it, once
};

static TState state = TState::Menu;

// A screen that redraws itself every pass (the live input report), or animates
// from canned data (the operation-screen demos).
enum class Live : uint8_t { None, Input, Steps, Chips, Scramble, Fold, Step, Demo,
                            Sensors, SensorRaw, Motors };
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

extern const MenuScreen kScreenDiag;
extern const MenuScreen kScreenThree;
extern const MenuScreen kScreenFour;
extern const MenuScreen kScreenFive;
extern const MenuScreen kScreenLong;
extern const MenuScreen kScreenDeep;

static void actLoad();
static void actEject();
static void actReport();
static void actInputReport();
static void actColourSensors();
static void actMotorSensors();
static void actDemoInfo();
static void actDemoSteps();
static void actDemoChips();
static void actDemoError();
static void actDemoScramble();
static void actStepSolve();
static void actDemoMode();
static void actDemoNetSolved();
static void actDemoNetScrambled();
static void actDemoNetLoad();
static void actFoldPattern();
static void actJog();

// ---------------------------------------------------------------------------
//  Menu tables
// ---------------------------------------------------------------------------
//  Between them these cover every visual state the theme has: all six frame
//  colours, screens of three, four and five items, captions of realistic
//  length, preview lists and the placeholder graphic, labels long enough to
//  ellipsise, and a stack deep enough to hit CubeMenu's depth limit.

static const char* const kPrevNav[]     = { "3 items", "4 items", "5 items", "Long" };
static const char* const kPrevScreens[] = { "Operations", "Cube views", "Messages",
                                            "Patterns" };

// Cube states for the pattern previews, in net order (U R F D L B).
//
// Computed by applying each sequence to a solved cube, not drawn by hand — and
// every one checked for nine of each colour, because a preview that is not a
// real cube would hide exactly the bugs this screen is for.
static const char kPatCheckerboard[55] =
    "WYWYWYWYW" "ROROROROR" "GBGBGBGBG" "YWYWYWYWY" "ORORORORO" "BGBGBGBGB";
static const char kPatCubeInCube[55] =
    "GGGGWWGWW" "RRWRRWWWW" "RGGRGGRRR" "BBBYYBYYB" "YYYOOYOOY" "OOOOBBOBB";
static const char kPatSixSpot[55] =
    "GGGGWGGGG" "WWWWRWWWW" "RRRRGRRRR" "BBBBYBBBB" "YYYYOYYYY" "OOOOBOOOO";
static const char kPatSuperflip[55] =
    "WBWOWRWGW" "RWRGRBRYR" "GWGOGRGYG" "YGYOYRYBY" "OWOBOGOYO" "BWBRBOBYB";
static const char* const kPrevOps[]  = { "Faces", "Chips", "Scramble", "Step",
                                         "Demo" };

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
static const int kScrambleLen = 30;
static const char* const kScrambleMoves[kScrambleLen] = {
    "D2", "L",  "B'", "R",  "U'", "F2", "D",  "L2", "B",  "R'",
    "U2", "F",  "D'", "L'", "B2", "R2", "U",  "F'", "D",  "L",
    "B",  "R",  "U2", "F2", "D'", "L2", "B'", "R2", "U'", "F",
};
static const char* const kPrevDiag[] = { "Navigation", "Input", "Colour", "Motor" };

// One caption per sticker on a colour board. Nine of them, in the order the
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
      kPrevScreens, 4, MenuTheme::Yellow },
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
    { "Navigation",   &kScreenNav, nullptr,        "Screens of every size.",
      kPrevNav, 4, MenuTheme::Red },
    { "Input Report", nullptr,     actInputReport, "Live wheel and buttons.",
      nullptr, 0, MenuTheme::Purple },
    { "Colour Sensors", nullptr,   actColourSensors, "Live, per board.",
      nullptr, 0, MenuTheme::Blue },
    { "Motor Sensors",  nullptr,   actMotorSensors,  "Raw encoder angles.",
      nullptr, 0, MenuTheme::Green },
};
const MenuScreen kScreenDiag = { "Diagnostics", kDiagItems, 4, MenuTheme::Purple };

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
      kPrevOps,  4, MenuTheme::Blue },
    { "Cube Views",  &kScreenCube, nullptr, "The unfolded cube net.",
      kPrevCube, 3, MenuTheme::Green },
    { "Messages",    &kScreenMsg,  nullptr, "Status and faults.",
      nullptr,   0, MenuTheme::Red },
    { "Patterns",    &kScreenPatterns, nullptr, "Previews in the side pane.",
      nullptr,   0, MenuTheme::Violet },
};
const MenuScreen kScreenScreens = { "Screens", kScreensItems, 4, MenuTheme::Yellow };

// The point of this screen: the preview pane shows what each pattern PRODUCES.
// A list of names would say nothing about what you are choosing between.
static const MenuItem kPatternItems[] = {
    { "Checkerboard", nullptr, actFoldPattern, "R2 L2 F2 B2 U2 D2",
      nullptr, 0, MenuTheme::Blue,   kPatCheckerboard },
    { "Cube in Cube", nullptr, actFoldPattern, "Fifteen moves.",
      nullptr, 0, MenuTheme::Green,  kPatCubeInCube },
    { "Six Spot",     nullptr, actFoldPattern, "U D' R L' F B' U D'",
      nullptr, 0, MenuTheme::Yellow, kPatSixSpot },
    { "Superflip",    nullptr, actFoldPattern, "Every edge flipped.",
      nullptr, 0, MenuTheme::Purple, kPatSuperflip },
};
const MenuScreen kScreenPatterns = { "Patterns", kPatternItems, 4, MenuTheme::Violet };

static const MenuItem kOpsItems[] = {
    { "Scan Faces",     nullptr, actDemoSteps,    "Faces fill as they are read." },
    { "Colour Chips",   nullptr, actDemoChips,    "Two boards, six colours." },
    { "Scramble Solve", nullptr, actDemoScramble, "Two phases, two colours." },
    { "Step Solve",     nullptr, actStepSolve,    "One move per press." },
    { "Demo Mode",      nullptr, actDemoMode,     "Scramble and solve, looping." },
};
const MenuScreen kScreenOps = { "Operations", kOpsItems, 5, MenuTheme::Blue };

static const MenuItem kCubeItems[] = {
    { "Solved Cube",     nullptr, actDemoNetSolved,    "Every face one colour." },
    { "Scrambled Cube",  nullptr, actDemoNetScrambled, "Checkerboard, 9 of each." },
    { "Load Orientation",nullptr, actDemoNetLoad,      "The calibration prompt." },
};
const MenuScreen kScreenCube = { "Cube Views", kCubeItems, 3, MenuTheme::Green };

static const MenuItem kMsgItems[] = {
    { "Info Panel",   nullptr, actDemoInfo,  "Aligned label/value rows." },
    { "Error Screen", nullptr, actDemoError, "The red stopped look." },
};
const MenuScreen kScreenMsg = { "Messages", kMsgItems, 2, MenuTheme::Red };

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
static const char* const kFaceMove[6][2] = {
    { "U", "U'" }, { "R", "R'" }, { "F", "F'" },
    { "D", "D'" }, { "L", "L'" }, { "B", "B'" },
};

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
    const char* mv = (i < kJogFaces) ? kFaceMove[i][dir > 0 ? 0 : 1]
                                     : kRotMove[i - kJogFaces];
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
        Cube.botServoPartial(); // then present the cube
        gripAt[0] = 0;          // top    retracted
        gripAt[1] = 1;          // bottom partial
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
//  Split in two because they answer different questions. The colour boards want
//  "is any sensor disagreeing with its neighbours", which is a picture. The
//  motor encoders want "what angle is each one reading", which is a list of
//  numbers.
//
//  The readings here are canned but MOVING, so the screens are exercised as live
//  ones rather than stills. On hardware they come from
//  colorSensorN.getScanValRow(i) — four ints, R G B W — and
//  MotorEncoders[i]->scan(), a raw 12-bit angle or a negative I2C error.
static int8_t senSel = 0;          // 0..17 across both boards, or the raw view

static void senReadings(uint32_t t, int8_t* b1, int8_t* b2) {
    const int phase = (int)(t / 900);
    for (int i = 0; i < 9; ++i) {
        b1[i] = (int8_t)((i + phase) % 6);
        // Board 2 sensor 2 has a dead green channel on this machine (README),
        // so it never resolves to a colour. A diagnostic that only ever shows
        // healthy hardware is not a diagnostic.
        b2[i] = (i == 1) ? (int8_t)-1 : (int8_t)((i + phase + 3) % 6);
    }
}

static void actColourSensors() {
    senSel = 0;
    showScreen(Op::Scan, "Colour Sensors", nullptr, Live::Sensors);
}

static void updateColourSensors(uint32_t t) {
    static const char* rows[2] = { "Board 1\t9/9 healthy, sep 165",
                                   "Board 2\t8/9 healthy, sep 3" };
    static const CubeDisplay::RowMark marks[2] = { CubeDisplay::RowMark::Good,
                                                   CubeDisplay::RowMark::Bad };
    int8_t b1[9], b2[9];
    senReadings(t, b1, b2);

    char hint[40];
    snprintf(hint, sizeof(hint), "SELECT for board %d sensor %d",
             (senSel < 9) ? 1 : 2, (senSel % 9) + 1);

    cubeDisplay.showOperation(Op::Scan, "Colour Sensors", nullptr, hint);
    cubeDisplay.setOpLines(rows, 2, marks);
    cubeDisplay.setOpChipRow(0, b1, kSensorCaps, 9, (senSel < 9) ? senSel : -1, 104);
    cubeDisplay.setOpChipRow(1, b2, nullptr,     9, (senSel < 9) ? -1 : senSel - 9, 140);
    Cube.displayUpdate();
}

// One sensor, in the numbers behind the colour. This is the screen for "why did
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
    showScreen(Op::Scan, "Scan Faces", "Reading the cube", Live::Steps);
}

static void actDemoChips() {
    // Headline left empty: the demo sets it per stage below, and a fixed one
    // here would sit contradicting the stage line underneath it.
    showScreen(Op::Calibrate, "Colour Chips", "", Live::Chips);
}


// A scramble followed by a solve. The frame carries the phase — red while it is
// scrambling, green the moment it starts solving — which is the whole idea the
// real Scramble Solve is meant to prove, and the reason setOpKind() exists.
static void actDemoScramble() {
    showScreen(Op::Error, "Scramble Solve", "Scrambling", Live::Scramble);
}

// The six cube colours, in the order CubeDisplay's chips use them.
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
    cubeDisplay.setStatus("Every face one colour");
    Cube.displayUpdate();
}

// The classic checkerboard: each sticker is either its own face colour or the
// opposite one. Worth having as the scrambled case because it is a REAL state —
// five of a face's own colour and four of its opposite, so each opposite pair
// still totals nine of each. A random splash of colour would not be a cube, and
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

// The orientation colour calibration requires. Drawn from the firmware's own
// constant, not a copy, so this cannot drift from what the machine expects.
static void actDemoNetLoad() {
    showScreen(Op::Calibrate, "Load Orientation", nullptr);
    cubeDisplay.setOpCubeNet(CubeSystem::kCalStartFacelets);
    cubeDisplay.setStatus(CubeSystem::kCalStartText);
    Cube.displayUpdate();
}

// Folding a pattern: the solve screen with the pattern named, then the result.
// Running the moves is all the machine side would add — they are already known.
static const char* g_foldNet  = nullptr;
static const char* g_foldName = nullptr;

static void actFoldPattern() {
    const MenuItem* it = Menu.selectedItem();
    g_foldNet  = (it && it->previewNet) ? it->previewNet : nullptr;
    g_foldName = (it && it->label) ? it->label : "Pattern";
    showScreen(Op::Solve, "Patterns", "Folding", Live::Fold);
    cubeDisplay.setStatus(g_foldName);
    Cube.displayUpdate();
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

static void actStepSolve() {
    stepAt = 0;
    showScreen(Op::Solve, "Step Solve", nullptr, Live::Step);
    drawStepSolve();
}

// Scramble, solve, repeat, unattended. Both halves already existed — the phase
// colours from Scramble Solve, the ribbon from Step Solve — so this is a loop
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
        const int m = (int)((cycle * kScrambleLen) / kDemoScrambleMs);
        cubeDisplay.setOpKind(Op::Error);                  // red: scrambling
        cubeDisplay.setMessage("Scrambling");
        snprintf(sub, sizeof(sub), "Run %d   -   Move %d of %d",
                 run, m + 1, kScrambleLen);
        cubeDisplay.setOpRibbon(kScrambleMoves, kScrambleLen, m);
        Cube.displayProgress(m, kScrambleLen - 1);
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
        // of the real scan, played back against a solved cube's colours.
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

    case Live::Demo:
        updateDemoMode(t);
        break;

    case Live::Sensors:
        updateColourSensors(t);
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
        break;      // handled above; nothing here consumes a MenuEvent

    case TState::Screen:
        if (live == Live::Sensors && (ev == MenuEvent::Up || ev == MenuEvent::Down)) {
            senSel = (int8_t)((senSel + (ev == MenuEvent::Down ? 1 : 17)) % 18);
            updateColourSensors(millis() - liveStart);
        } else if (live == Live::Sensors && ev == MenuEvent::Select) {
            live = Live::SensorRaw;                 // drill into the one selected
        } else if (live == Live::SensorRaw && ev == MenuEvent::Back) {
            live = Live::Sensors;                   // and back out to the boards
        } else if (live == Live::Step && ev == MenuEvent::Select) {
            // SELECT means "next move" here, not "done looking". Only LEFT
            // leaves, which is the one meaning it has everywhere.
            if (stepAt < 20) { stepAt++; drawStepSolve(); }
            else             { toMenu(); }
        } else if (ev == MenuEvent::Select || ev == MenuEvent::Back) {
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
