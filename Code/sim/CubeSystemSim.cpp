// =============================================================================
//  CubeSystemSim — the simulator's stand-in for CubeSystem.cpp
// =============================================================================
//
//  THIS IS THE ONLY FIRMWARE FILE THE SIMULATOR REPLACES.
//
//  It implements the real CubeSystem.h against no hardware: operations take a
//  plausible amount of time, report progress the way the real ones do, and can
//  be made to fail on demand. Everything else in the build — the sketch,
//  CubeMenu, CubeDisplay, RotaryEncoder, VirtualCube, the servo and motor
//  classes — is the shipping code, compiled unmodified.
//
//  Deliberately real, not faked:
//
//    * pumpTick(). Copied from the firmware, including the SELECT+LEFT chord,
//      the release latch and the suppression flag. The abort gesture is a thing
//      you have to be able to FEEL, so simulating it would defeat the purpose.
//
//    * The servos. topServo/botServo are the real CubeServo objects sweeping
//      through the real pumpDelay(), so a load or an eject takes as long here
//      as it does on the machine and the display keeps updating throughout.
//
//  Deliberately faked: scan, solve, execute and calibration durations. See the
//  constants below — they are compressed relative to the machine so that menu
//  work does not mean waiting out a 40-second scan every time.
//
//  What this file does NOT do is solve a cube. VirtualCube is real and its
//  ready/not-ready state is real (that is what drives the pre-scan vs post-scan
//  main menu), but kociemba is stubbed. Wiring in the real solver is Tier 2.
// =============================================================================

#include "CubeSystem.h"
#include "CubeHardwareConfig.h"
#include "SimHost.h"

// ---------------------------------------------------------------------------
//  Faked durations, in milliseconds
// ---------------------------------------------------------------------------
//  Roughly a fifth of the machine's real timings. Long enough that progress
//  screens, the abort chord and the display pump all get exercised; short
//  enough to iterate on.
namespace {
const unsigned long kScanPerFaceMs   = 900;    // x6 faces  (machine: ~5 s each)
const unsigned long kScanReorientMs  = 700;    // x2 rotations
const unsigned long kSolveComputeMs  = 900;    // kociemba on a Teensy
const unsigned long kRingMoveMs      = 450;
const unsigned long kPerMoveMs       = 220;    // machine: ~250-400 ms
const unsigned long kCalMotorMs      = 2800;   // 4 ALL moves + 3 x 300 ms settle
const unsigned long kCalColorMs      = 4000;

const int kFakeSolutionLength = 21;

// Fault codes chosen to exercise the sketch's error tables rather than to be
// realistic. Each is a code the corresponding *ErrorText() function knows.
const int kFakeScanFault  = 60;   // "Impossible cube, repair failed. Rescan."
const int kFakeSolveFault = 12;   // "No solution - illegal cube or timeout"
const int kFakeExecFault  = 121;  // falls through to "Move failed - cube released"
const int kFakeCalFault   = 8;    // "Save failed - machine is NOT calibrated"

bool g_motorCalibrated = true;
bool g_colorCalibrated = true;

// Drop a synthetic color calibration into both boards so Calibration Status
// has real numbers to lay out. Without it every sensor reads separation 0 and
// the screen only ever shows its degenerate case.
void seedFakeColorCalibration() {
    struct Ref { char c; int rgbw[4]; };
    static const Ref kRefs[6] = {
        { 'W', { 200, 200, 200, 600 } },
        { 'Y', { 200, 180,  40, 420 } },
        { 'R', { 200,  40,  40, 280 } },
        { 'O', { 200, 100,  40, 340 } },
        { 'G', {  40, 200,  60, 300 } },
        { 'B', {  40,  80, 200, 320 } },
    };

    for (int s = 0; s < 9; ++s) {
        for (int r = 0; r < 6; ++r) {
            int v[4];
            for (int ch = 0; ch < 4; ++ch) v[ch] = kRefs[r].rgbw[ch] + s * 3;
            colorSensor1.setColorCal(s, kRefs[r].c, v);

            // Board 2 sensor 2 gets a yellow that sits almost on top of its
            // white. That is the real machine's actual weak spot — the Y/W pair
            // is the closest on most sensors — and it is what makes Calibration
            // Status show its interesting case instead of nine clean ticks.
            int v2[4];
            const bool squash = (s == 2 && kRefs[r].c == 'Y');
            for (int ch = 0; ch < 4; ++ch) {
                v2[ch] = squash ? (kRefs[0].rgbw[ch] + 2)          // ~= white
                                : (kRefs[r].rgbw[ch] + s * 2);
            }
            colorSensor2.setColorCal(s, kRefs[r].c, v2);
        }
    }
    colorSensor1.computeSeparations();
    colorSensor2.computeSeparations();
}

}  // namespace

// ---------------------------------------------------------------------------
//  Cooperative pump — copied from the firmware, not simulated
// ---------------------------------------------------------------------------
CubeSystem* CubeSystem::s_pumpOwner = nullptr;

bool CubeSystem::pumpTrampoline() {
    if (s_pumpOwner == nullptr) return true;
    return s_pumpOwner->pumpTick();
}

bool CubeSystem::pumpTick() {
    // SDL first: without draining the event queue here, the window stops
    // responding for the whole of any operation and the abort chord could never
    // be seen. On the machine this line does not exist; everything below it is
    // the firmware's own code.
    sim::pumpEvents();

    // Mirrors the firmware: no display refresh while a stepper is moving.
    // sim::pumpEvents() above stays unconditional — that is the window's own
    // event queue, not the panel, and skipping it would hang the simulator
    // rather than model anything real.
    if (!pumpMotionOnly) displayUpdate();

    if (pumpAbortSuppressed) {
        chordHeldSince = 0;
        return true;
    }

    unsigned long now = millis();
    if (now - lastInputPoll >= 25) {
        // 400 ms, as in CubeSystem.cpp — it must clear the longest unpumped
        // stretch (see the note there).
        if (now - lastInputPoll > 400) {
            chordHeldSince = 0;
        }
        lastInputPoll = now;

        const uint8_t kChord = RotaryEncoder::BTN_SELECT | RotaryEncoder::BTN_LEFT;
        uint8_t btns = encoderInitialized ? menuEncoder.readButtons() : 0;
        bool held = ((btns & kChord) == kChord);

        if (chordMustRelease) {
            if (!held) chordMustRelease = false;
            chordHeldSince = 0;
        } else if (held) {
            if (chordHeldSince == 0) {
                chordHeldSince = now;
            } else if (now - chordHeldSince >= kAbortHoldMs) {
                if (!abortRequested) {
                    Serial.println(F("ABORT requested (SELECT+LEFT held)"));
                }
                abortRequested = true;
            }
        } else {
            chordHeldSince = 0;
        }
    }

    return !abortRequested;
}

// ---------------------------------------------------------------------------
//  Construction and start-up
// ---------------------------------------------------------------------------
CubeSystem::CubeSystem() {}

void CubeSystem::begin(bool withDisplay) {
    Serial.begin(baudRate);
    Serial.println(F("[sim] CubeSystem (simulated) starting"));

    // The desktop window IS the display; a sim run without it would show
    // nothing at all, so the opt-out is accepted but ignored.
    (void)withDisplay;
    displayInitialized = cubeDisplay.begin(10000000);

    s_pumpOwner = this;
    systemPump  = &CubeSystem::pumpTrampoline;
    clearAbort();

    // Real servo objects, real sweeps. begin() retracts them, which is a
    // visible couple of seconds here just as it is on the machine.
    topServo.begin();
    botServo.begin();

    // Nothing to probe, so every subsystem reports healthy. Press K to flip the
    // calibration flags; the boot self-test screen is exercised by editing
    // these, which is rare enough not to deserve a key.
    colorSensorsOk     = true;
    encoderMuxOk       = true;
    for (int i = 0; i < 7; ++i) motorEncoderOk[i] = true;
    encoderInitialized = menuEncoder.begin();

    if (numMotors > 6) numMotors = 6;
    if (numMotors < 1) numMotors = 1;

    seedFakeColorCalibration();

    clearAbort();
    motorHomeState = 0;
}

// ---------------------------------------------------------------------------
//  Sim-only helpers
// ---------------------------------------------------------------------------
namespace {

// Wait, but let the abort chord through. Returns false if the user aborted.
bool simWait(unsigned long ms) {
    return pumpDelay(ms);
}

// Mark the virtual cube as a known, solved state. This is what flips the
// sketch's main menu from its pre-scan form to its post-scan form, and it goes
// through the real VirtualCube so isReady() means what it means on the machine.
void simMakeCubeKnown(VirtualCube& vc) {
    vc.resetCube();
    vc.setSolved();
    vc.setOrientation('G', 'O');     // matches VirtualCube's default orientation
    vc.buildUnorientedCubeArray();
    vc.buildCubeArray();             // this is what sets cubeReady
}

}  // namespace

// ---------------------------------------------------------------------------
//  Operations
// ---------------------------------------------------------------------------
int CubeSystem::scanCube() {
    const bool injectFault = sim::consumeFaultInjection();

    // Same three passes the real scanCube() runs, so the progress display is
    // exercised here exactly as it will be on the machine. The colors are the
    // standard scheme for a solved cube: U white, R red, F green, D yellow,
    // L orange, B blue.
    static const char kSimFaceColor[6] = { 'W', 'R', 'G', 'Y', 'O', 'B' };
    int8_t* faceChips = scanFaceChips;
    for (int f = 0; f < 6; ++f) faceChips[f] = -1;

    // The real scanCube()'s entry condition, kept here because the SKETCH can
    // see it: the servo and ring shims record state, cubeIsClamped() reads it,
    // and the sketch parks the machine clamped after a scan and after a solve.
    // Without this the simulated Scan would run — and the screens after it
    // would reason — from a clamped machine the firmware releases.
    displaySetStatus("Lowering the cube");
    displayUpdate();        // as the firmware: say it before the travel starts
    unloadCube();

    // The firmware squares the face fingers here, released, before the first
    // reorientation lifts the cube into them. Same status, same duration as
    // the homeMotors() stub below, and skipped on an uncalibrated machine
    // exactly as the firmware skips it.
    if (g_motorCalibrated) {
        displaySetStatus("Homing motors");
        if (homeMotors() != 0) return 70;
    }

    for (int pass = 0; pass < CubeSystem::kScanPasses; ++pass) {
        const int fa = CubeSystem::kScanPassFaces[pass][0];
        const int fb = CubeSystem::kScanPassFaces[pass][1];

        displayFaces(faceChips, fa, fb);
        displaySetStatus(CubeSystem::kScanPassLabels[pass]);
        if (!simWait(kScanPerFaceMs * 2)) return 70;      // "Scan aborted"

        faceChips[fa] = CubeSystem::chipIndexForColor(kSimFaceColor[fa]);
        faceChips[fb] = CubeSystem::chipIndexForColor(kSimFaceColor[fb]);
        displayFaces(faceChips);

        // Record the pass the way the real scanCube() does — in SCAN order,
        // incrementally — so a failed scan still leaves readings for the
        // screens that review one, as it does on the machine.
        for (int sen = 0; sen < 2; ++sen) {
            const int f = 2 * pass + sen;
            const char col = kSimFaceColor[CubeSystem::kScanPassFaces[pass][sen]];
            for (int k = 0; k < 9; ++k) scanColor[f][k] = col;
            scanFaceColor[f] = col;
        }
        scanFacesRecorded = 2 * pass + 2;

        if (pass < CubeSystem::kScanPasses - 1) {
            displaySetStatus("Rotating cube");
            if (!simWait(kScanReorientMs)) return 70;
        }
    }

    displayFaces(faceChips);

    if (injectFault) {
        // An impossible cube is diagnosed AFTER all six faces are read, so the
        // readings survive — which is the whole point of being able to review
        // them. Leave scanFacesRecorded alone.
        displaySetStatus("");
        return kFakeScanFault;
    }

    simMakeCubeKnown(virtualCube);
    scanFacesRecorded = 6;
    displaySetStatus("");
    return 0;
}

int CubeSystem::solveVirtual() {
    if (!virtualCube.isReady()) return 11;           // "Cube not scanned yet"

    const bool injectFault = sim::consumeFaultInjection();

    // The wait's abort result is deliberately IGNORED: the real kociemba call
    // blocks unpumped, so the machine cannot notice the chord during the
    // compute — a latched abort surfaces at the first move boundary as 105,
    // and the sim must rehearse that path, not invent an abortable compute.
    simWait(kSolveComputeMs);
    if (injectFault) return kFakeSolveFault;

    // A canned solution. Nothing executes these; they exist so solutionLength
    // is a believable number on the progress and result screens.
    clearSolution();
    static const char* kCanned[kFakeSolutionLength] = {
        "R", "U2", "F'", "L", "D", "B2", "R'", "U", "F2", "L'",
        "D2", "B", "R2", "U'", "F", "L2", "D'", "B'", "R", "U2", "F'"
    };
    for (int i = 0; i < kFakeSolutionLength; ++i) solveMoves[i] = String(kCanned[i]);
    solutionLength = kFakeSolutionLength;
    return 0;
}

int CubeSystem::executeSolve() {
    if (!virtualCube.isReady()) return 1;
    if (solutionLength <= 0)    return 2;

    const bool injectFault = sim::consumeFaultInjection();

    for (int i = 0; i < solutionLength; ++i) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Move %d/%d   %s",
                 i + 1, solutionLength, solveMoves[i].c_str());
        displaySetStatus(msg);
        displayProgress(i, solutionLength);

        if (!simWait(kPerMoveMs)) {
            // Same unwind the firmware performs: release the cube, invalidate
            // the state, report the abort through the +100 code scheme.
            safeStop(ERR_ABORTED);
            return 100 + ERR_ABORTED;
        }

        // Fail two-thirds of the way in, where a real jam is most likely and
        // where the recovery path is most worth watching.
        if (injectFault && i == (solutionLength * 2) / 3) {
            safeStop(20 + ERR_ENCODER_FAULT);
            return kFakeExecFault;
        }
    }

    displaySetStatus("");
    return 0;
}

int CubeSystem::calibrateMotorRotations() {
    const bool injectFault = sim::consumeFaultInjection();
    if (!simWait(kCalMotorMs)) return 9;              // "Aborted - EEPROM untouched"
    if (injectFault) return kFakeCalFault;
    g_motorCalibrated = true;
    return 0;
}

int CubeSystem::calibrateColorSensors() {
    const bool injectFault = sim::consumeFaultInjection();

    // The real routine's entry condition, and its consequences for the sketch.
    // Color calibration is one menu level from the clamped rest state, so it
    // releases rather than inheriting; and its eight reorientations are not
    // tracked in the model, so the cube it leaves behind is NOT the one
    // virtualCube describes. Both are visible from the sketch here — through
    // cubeIsClamped() and through isReady()/"Cube Ready" — so a stub that
    // skipped them would show a menu the machine never shows.
    unloadCube();
    virtualCube.resetCube();
    clearSolution();

    // The real routine's color order, so the chips fill in the same sequence
    // here as on the machine. Each rotation feeds a DIFFERENT color to each
    // board, which is why the two rows do not fill together.
    uint8_t bits[2] = { 0, 0 };
    const int per = kCalColorMs / (CubeSystem::kCalSideRots + 1 + CubeSystem::kCalTopRots);

    for (int rot = 0; rot < CubeSystem::kCalSideRots; ++rot) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Side faces  (%d/%d)", rot + 1, CubeSystem::kCalSideRots);
        displaySetMessage("Sampling side faces");
        displaySetStatus(msg);
        bits[0] |= (uint8_t)(1u << CubeSystem::kCalSideColors[rot][0]);
        bits[1] |= (uint8_t)(1u << CubeSystem::kCalSideColors[rot][1]);
        displayChips(bits, 2);
        if (!simWait(per)) return 9;
    }

    displaySetMessage("Sampling empty slot");
    displaySetStatus("Reference reading");
    if (!simWait(per)) return 9;

    for (int rot = 0; rot < CubeSystem::kCalTopRots; ++rot) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Top and bottom  (%d/%d)", rot + 1, CubeSystem::kCalTopRots);
        displaySetMessage("Sampling top and bottom");
        displaySetStatus(msg);
        bits[0] |= (uint8_t)(1u << CubeSystem::kCalTopColors[rot][0]);
        bits[1] |= (uint8_t)(1u << CubeSystem::kCalTopColors[rot][1]);
        displayChips(bits, 2);
        if (!simWait(per)) return 9;
    }

    displaySetStatus("");
    if (injectFault) return kFakeCalFault;
    g_colorCalibrated = true;
    return 0;
}

// A single move, for the manual actuator screens and the sketch-side modes
// (scramble, idle turns, pattern folds). The real one drives a stepper and
// checks the encoder; here it takes a plausible amount of time, can be
// aborted, and consumes the F key's armed fault — the modes' per-move failure
// paths (and jog's failure screen) are unreachable without that. Whole-cube
// rotations take longer because they re-grip.
//
// moveVirtual is honored as the real code honors it: applied AFTER the wait
// (physical first, model last, so an abort mid-move leaves the model
// untracked) and for EVERY token, rotations included. That reproduces a trap
// on purpose: VirtualCube parses a move's first character as its face, so
// "ROTX" with moveVirtual=true silently runs an R turn on the model. The
// callers are contracted around that, and skipping the model move for
// ROT/ALL here would hide exactly the caller bug the sim exists to catch.
// It is also what lets the sketch's modes genuinely disorder the virtual
// cube, which is the only way "skip the scramble when already scrambled"
// and "refuse a pattern on an unsolved cube" can be seen working.
//
// Known wrinkle: solveVirtual()'s canned solution bears no relation to the
// scrambled state, so a sim mode-solve leaves the model disordered — press C
// to reset it. Wiring the real solver fixes that (Tier 2, see the banner).
int CubeSystem::executeMove(const String& move, bool moveVirtual, bool align) {
    (void)align;
    // The real failure point is the pre-move encoder read, before any motion
    // — so the injected fault fires before the wait, not after it.
    if (sim::consumeFaultInjection()) return 20 + ERR_ENCODER_FAULT;   // 24, "Encoder unreadable"
    const bool whole = move.startsWith("ROT");
    // ALL re-grips nothing (it turns all six faces at once), so it keeps the
    // short duration.
    if (!simWait(whole ? kRingMoveMs : kPerMoveMs)) return 20 + ERR_ABORTED;
    if (moveVirtual) {
        const int r = virtualCube.executeMove(move);
        if (r != 0) return 10 + r;
    }
    return 0;
}

int CubeSystem::homeMotors() {
    if (!simWait(1200)) return ERR_ABORTED;
    motorHomeState = 0;
    return 0;
}

bool CubeSystem::getMotorCalibration() { return g_motorCalibrated; }
bool CubeSystem::getColorCalibration() { return g_colorCalibrated; }

// Called by main.cpp between passes of the sketch's loop(), so the K and C
// keys can flip machine state without waiting out an operation.
void simApplyHotkeys(CubeSystem& cube) {
    if (sim::consumeCalToggle()) {
        g_motorCalibrated = !g_motorCalibrated;
        g_colorCalibrated = g_motorCalibrated;
        Serial.print(F("[sim] calibration flags -> "));
        Serial.println(g_motorCalibrated ? "CALIBRATED" : "NOT CALIBRATED");
    }
    if (sim::consumeCubeToggle()) {
        if (cube.virtualCube.isReady()) {
            cube.virtualCube.resetCube();
            cube.clearSolution();
            Serial.println(F("[sim] cube state -> unknown (pre-scan menu)"));
        } else {
            simMakeCubeKnown(cube.virtualCube);
            Serial.println(F("[sim] cube state -> known (post-scan menu)"));
        }
    }
}

// ---------------------------------------------------------------------------
//  Mechanics
// ---------------------------------------------------------------------------
//  The servos are real objects doing real sweeps. The ring is a stepper the
//  shim cannot move, so its travel is a plain wait of about the right length.
void CubeSystem::topServoExtend()  { topServo.extend();  }
void CubeSystem::topServoRetract() { topServo.retract(); }
void CubeSystem::topServoPartial() { topServo.partial(); }
void CubeSystem::topServoEject()   { topServo.eject();   }   // untuned: same pose as partial
void CubeSystem::toggleTopServo()  { topServo.toggle();  }

void CubeSystem::botServoExtend()  { botServo.extend();  }
void CubeSystem::botServoRetract() { botServo.retract(); }
void CubeSystem::botServoPartial() { botServo.partial(); }
void CubeSystem::botServoEject()   { botServo.eject();   }
void CubeSystem::toggleBotServo()  { botServo.toggle();  }

// The ring DOES go through cubeMotors, even though the shim cannot turn a
// stepper: ringMove() is what records ringState, and cubeIsClamped() and the
// Hardware Test ring row both read it — a bare wait here leaves them frozen at
// whatever boot decided. It is safe because the AccelStepper shim reports
// every move as instantly arrived, so ringMove() does its bookkeeping and
// returns without spinning. The pumpDelay after it stands in for the travel
// that removes — keep it, or the ring teleports and the operation screens
// flash past unreadably.
void CubeSystem::ringExtend()  { cubeMotors.ringMove(2); pumpDelay(kRingMoveMs); }
void CubeSystem::ringPartial() { cubeMotors.ringMove(3); pumpDelay(kRingMoveMs); }
void CubeSystem::ringMiddle()  { cubeMotors.ringMove(1); pumpDelay(kRingMoveMs); }
void CubeSystem::ringRetract() { cubeMotors.ringMove(0); pumpDelay(kRingMoveMs); }
void CubeSystem::toggleRing()  { cubeMotors.ringToggle(); pumpDelay(kRingMoveMs); }

void CubeSystem::unloadCube() {
    // Same order as the firmware, and for the same mechanical reason.
    const bool wasSuppressed = pumpAbortSuppressed;
    pumpAbortSuppressed = true;

    unloadCubeKeepBottom();
    botServoRetract();

    pumpAbortSuppressed = wasSuppressed;
    chordHeldSince = 0;
}

// The partial release: ring and top only, the cube left up on the bottom
// gripper. Nothing here can drop a cube, but the timings are what the sketch
// waits through, so it must cost the same as the firmware's.
void CubeSystem::unloadCubeKeepBottom() {
    const bool wasSuppressed = pumpAbortSuppressed;
    pumpAbortSuppressed = true;

    ringRetract();
    topServoRetract();

    pumpAbortSuppressed = wasSuppressed;
    chordHeldSince = 0;
}

void CubeSystem::safeStop(int faultCode) {
    lastFault = faultCode;

    const bool wasAborted = abortRequested;
    pumpAbortSuppressed = true;
    abortRequested = false;
    chordHeldSince = 0;

    unloadCube();

    virtualCube.resetCube();
    clearSolution();

    pumpAbortSuppressed = false;
    chordHeldSince = 0;
    abortRequested = wasAborted;

    if (faultCode != 0) {
        Serial.print(F("safeStop: cube released, machine halted safely, fault code "));
        Serial.println(faultCode);
    }
}

void CubeSystem::clearSolution() {
    solutionLength = 0;
    for (int i = 0; i < maxMoves; ++i) solveMoves[i] = String("");
}

// ---------------------------------------------------------------------------
//  Display pass-throughs — identical to the firmware
// ---------------------------------------------------------------------------
void CubeSystem::displaySetMessage(const char* msg) {
    if (displayInitialized) cubeDisplay.setMessage(msg);
}
void CubeSystem::displaySetStatus(const char* msg) {
    if (displayInitialized) cubeDisplay.setStatus(msg);
}
void CubeSystem::displayClearStatus() {
    if (displayInitialized) cubeDisplay.clearStatus();
}
void CubeSystem::displayFaces(const int8_t* faces, int activeA, int activeB) {
    if (displayInitialized) cubeDisplay.setOpFaces(faces, activeA, activeB);
}
void CubeSystem::displayChips(const uint8_t* bits, int boards) {
    if (displayInitialized) cubeDisplay.setOpChips(bits, boards);
}
void CubeSystem::displayProgress(int done, int total) {
    if (displayInitialized) cubeDisplay.setOpProgress(done, total);
}
void CubeSystem::displayUpdate() {
    if (displayInitialized) cubeDisplay.update();
}
void CubeSystem::displayWaitForSelect(const char* msg) {
    if (displayInitialized) cubeDisplay.waitForSelect(msg);
}
bool CubeSystem::displayReady() { return displayInitialized; }
