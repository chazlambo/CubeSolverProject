#ifndef CubeSystem_h
#define CubeSystem_h

#include <Arduino.h>
#include "CubeHardwareConfig.h"

class CubeSystem {
public:
    CubeSystem();

    // Main Functions
    void begin();                   // Initializes all hardware
    int scanCube();
    
    // Motor Calibration Functions
    bool getMotorCalibration();     // Checks EEPROM for calibration flag
    int calibrateMotorRotations();  // Sets all motor calibration values based on current position
    int homeMotors();               // Homes all 6 stepper motors

    // Color Sensor Calibration Functions
    bool getColorCalibration();     // Checks EEPROM for calibration flag
    int calibrateColorSensors();    // Calibrates sensors using solved cube

    // Move Functions
    int executeMove(const String& move, bool moveVirtual = false, bool align = false);
    bool backoutMove(int targetPositions[6]);
    int alignMotors();              // Re-aligns motors after a move (faster, selective)
    bool checkAlignment();          // Check if motors are currently aligned
    int alignMotorsInternal(bool selectiveAlign);

    // Main Solving Functions
    void clearSolution();
    int solveVirtual();
    int executeSolve();

    // ================= Cooperative waiting / abort =================
    //
    // begin() registers pumpTrampoline() as the global systemPump, so every
    // pumpDelay() anywhere in the library refreshes the display and polls input
    // instead of blocking dead. This is what keeps the UI alive through the
    // ~25-40 s scan and gives an abort somewhere to be noticed.
    //
    // Abort gesture: hold SELECT for kAbortHoldMs. No new hardware needed.
    // Not touched by any ISR — plain state. Kept non-volatile deliberately so
    // the threading model isn't misrepresented.
    bool abortRequested = false;

    // Programmatic abort, for callers without a button (a serial command, a
    // limit switch, a future e-stop). The UI uses the SELECT-hold gesture.
    //
    // Mirrors clearAbort()'s discipline so the gesture state cannot be left in a
    // combination that says "abort pending" and "waiting for a release" at once.
    void requestAbort() {
        abortRequested   = true;
        selectMustRelease = false;
        selectHeldSince   = 0;
    }

    // Clearing also demands the button be RELEASED before a new hold can count.
    // Without that, the very SELECT press used to start an operation keeps the
    // button down, the hold timer restarts from here, and an abort fires ~1 s
    // into the thing the user just asked for.
    void clearAbort() {
        abortRequested   = false;
        selectHeldSince  = 0;
        selectMustRelease = true;
    }
    bool abortPending() const { return abortRequested; }

    static constexpr unsigned long kAbortHoldMs = 1000;

    // One pump iteration: refresh display, poll input, report whether to continue.
    bool pumpTick();

    // ================= Fail-safe handling =================
    // Put the machine into a state that is safe to leave unattended: motors
    // de-energised, cube released, virtual state invalidated so a stale
    // solution can never be replayed.
    //
    // Called from every error path that can leave the machine HOLDING the cube:
    // executeSolve()'s move failures, the abort paths, and scanCube()'s
    // reorientation failures. scanCube()'s colour/build errors deliberately do
    // NOT call it — at those points the ring and both servos are already
    // retracted (the scan loop ends with ringRetract/botServoRetract), and
    // wiping the scan record would destroy the data repairScan() needs.
    void safeStop(int faultCode = 0);

    // Release the cube. Order is mechanically load-bearing:
    //   ring retract -> top servo retract -> bottom servo retract
    // It is NOT the mirror of the load sequence, which is why it lives here
    // once rather than being re-derived at each call site.
    void unloadCube();

    // Latched code from the most recent safeStop(). 0 = no fault.
    // Read it in your error screen if you want the underlying cause rather than
    // the code the calling layer returned.
    int lastFault = 0;

    // Signed shortest-path error between two AS5600 readings, in (-2048, +2048].
    //
    // Replaces five hand-rolled copies of the 2048/4096 arithmetic. Two of them
    // disagreed: alignMotorsInternal() computed a wraparound-safe MAGNITUDE but
    // then chose its direction with a raw `cur > tgt` comparison, which sends
    // the motor the long way round whenever the error straddles the 0/4095
    // seam; backoutMove() handled the seam correctly but used the opposite sign.
    static inline int encError(int cur, int tgt) {
        int d = (cur - tgt) & 0x0FFF;          // mod 4096
        return (d > 2048) ? d - 4096 : d;      // fold into the short way round
    }

    // Which way to step when encError() is positive.
    //
    // +1 reproduces alignMotorsInternal()'s original behaviour. That routine
    // runs on every move of every solve on a machine that works, so its sign is
    // almost certainly the physically correct one — backoutMove() was the
    // inverted copy. If a bench test shows alignment running AWAY from target,
    // flip this to -1. It is the only place the convention is decided.
    static constexpr int kAlignStepSign = +1;

    // Error codes returned by the alignment / move paths.
    static constexpr int ERR_ALIGN_TIMEOUT  = 2;   // did not converge in time
    static constexpr int ERR_ENCODER_FAULT  = 4;   // encoder unreadable — NOT a jam
    static constexpr int ERR_ABORTED        = 5;   // user held SELECT to abort

    // Cube Loading Functions
    // Top Servo Functions
    void topServoExtend();
    void topServoRetract();
    void topServoPartial();
    void toggleTopServo();

    // Bot Servo Functions
    void botServoExtend();
    void botServoRetract();
    void botServoPartial();
    void toggleBotServo();

    // Ring Functions
    void toggleRing();
    void ringExtend();
    void ringPartial();
    void ringMiddle();
    void ringRetract();

    // Display Functions
    // NOTE: displayBegin() was removed — the display is now initialised inside
    // begin(). The declaration outlived its definition and any sketch calling
    // it failed at link time with an error pointing at the .ino rather than here.
    void displaySetMessage(const char* msg);
    void displaySetStatus(const char* msg);
    void displayClearStatus();
    void displayUpdate();
    void displayWaitForSelect(const char* msg);
    bool displayReady();

private:
    bool powerCheck();

    // Restore the in-RAM colour calibration after a failed or aborted
    // calibration. Leaving EEPROM untouched is necessary but not sufficient —
    // setColorCal() has already written partial/stale values into calVals[],
    // and nothing reloads them, so every later scan this power cycle would
    // classify against a poisoned table.
    void calibrationBail(int why);

    // systemPump is a plain function pointer, so route it through a static
    // trampoline bound to whichever CubeSystem called begin() last. There is
    // exactly one of these in any real firmware.
    static CubeSystem* s_pumpOwner;
    static bool pumpTrampoline();

    unsigned long selectHeldSince = 0;   // for the long-press abort gesture
    unsigned long lastInputPoll   = 0;   // throttles the I2C read in pumpTick()

    // Suppresses abort DETECTION (not just the flag) while safeStop() unloads.
    // Clearing abortRequested alone is not enough: the gesture is a 1 s hold, so
    // the button is still down when safeStop runs, and the very first pumpTick()
    // inside unloadCube() would see a stale selectHeldSince already older than
    // kAbortHoldMs and instantly re-latch — truncating every servo sweep and
    // leaving the cube clamped, which is the exact failure safeStop prevents.
    bool pumpAbortSuppressed = false;

    // Set by clearAbort(); cleared by pumpTick() the first time it sees SELECT
    // up. Until then the hold timer does not run.
    bool selectMustRelease = false;

public:
    int numMotors = 6;
    bool displayInitialized = false;
    bool encoderInitialized = false;   // seesaw menu encoder responded at boot
    bool colorSensorsOk = false;       // both ColorSensor::begin() returned 0

public:
    // Virtual Cube
    VirtualCube virtualCube;

    // Motor Home Variables
    int stepSize = 1;
    int stableReq = 3;
    unsigned long homeTimeout = 1000;       // Timeout for full homing
    unsigned long alignTimeout = 500;       // Timeout for quick alignment
    int motorHomeState;
    int motorAlignmentTol = 20;             // Alignment threshold (20)

    // E1: log the per-move alignment error in encoder counts and equivalent
    // steps. OFF by default — this prints once per moved motor per move, which
    // is far too chatty for a demo. Turn it on to characterise whether residual
    // error is missed-steps-during-motion or drift-while-de-energised.
    bool debugAlignLog = false;
    int startCalIndex[6] = {0,0,0,0,0,0};
    bool motorMoved[6] = {false, false, false, false, false, false};

    // Cube Solve Variables
    int servoDelay = 200;

    // ================= Scan record, for constraint repair =================
    //
    // scanCube() records what each sensor actually saw, not just the letter it
    // settled on, so a failed validation can be retried in software before
    // spending any physical moves on a rescan.
    //
    // The dominant scan error on this machine is a COMPENSATING pair: one Y
    // read as W and one W read as Y leaves every colour count at exactly 9, so
    // it sails through the count check and only piece-level validation sees it.
    // Both offending stickers necessarily have low margin — their whole problem
    // is that Y and W sit ~0.03 apart — and each has the other as its runner-up.
    // Substituting the runner-up on the least-confident stickers therefore fixes
    // precisely the case that counting cannot catch, at zero mechanical cost.
    char  scanColor[6][9];        // classified colour per sticker
    char  scanAlt[6][9];          // runner-up colour per sticker
    float scanConf[6][9];         // confidence [0,1], 0 = totally ambiguous
    char  scanFaceColor[6];       // centre colour of each scanned face
    char  scanLeftColor[6];       // 'left' argument used when setting that face
    char  scanOrientLeft = 'X';   // orientation arguments captured at scan time
    char  scanOrientBack = 'X';
    int   scanFacesRecorded = 0;

    // Replay the recorded scan into virtualCube (reset -> 6 faces -> orient ->
    // build). Returns 0 on success, non-zero using scanCube's code scheme.
    int rebuildFromScan();

    // Try to repair a scan that failed validation by substituting runner-up
    // colours on the least-confident stickers. Returns 0 if a substitution
    // produced a physically valid cube, non-zero if none did (in which case the
    // original scan is restored).
    // Defaults cover EVERY candidate, deliberately.
    //
    // classify() returns a runner-up for every sticker, so a real scan has ~48
    // candidates, not a handful. With a window of 6 the pair search examined 15
    // of 1128 possible pairs — 1.3% — and the compensating Y/W swap this
    // function exists to fix is a PAIR, so it was only ever found when both
    // misread stickers happened to be the least confident on the whole cube.
    //
    // Measured cost of searching everything: one rebuild plus both validators is
    // ~0.5 us on x86 -O2, so 1176 substitutions is ~0.6 ms there and realistically
    // 3-10 ms on a 600 MHz Cortex-M7. (An earlier comment here claimed ~10 us
    // total — that was wrong by two to three orders of magnitude.) Against a
    // 25-40 s rescan it is still nothing, and repairScan() runs at most once per
    // scan, so there is no reason to window it.
    int repairScan(int maxSingles = 54, int maxPairs = 54);

    // Solution String
    int solutionLength = 0;
    static const int maxMoves = 50;
    String solveMoves[maxMoves];
    
};

#endif


