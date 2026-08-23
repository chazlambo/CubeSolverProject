#include "CubeMotors.h"
#include "CubePump.h"


// FACE-MOTOR MOVES ARE DELIBERATELY UNPUMPED.
//
// Pumping them was tried on the bench and removed: pumpOnce() every 5 ms
// triggers a seesaw I2C poll every 25 ms (~1 ms at 100 kHz), and at
// 1000 steps/s that steals a step's worth of time ~40 times a second through
// every turn — an audible growl and visible roughness on the machine's core
// mechanic. Turning a face cleanly outranks noticing the abort chord a move
// earlier, especially since between-move is the only mechanically safe place
// to act on an abort anyway (see executeSolve()). The chord still accumulates
// across moves: pumpTick()'s blind-gap threshold (400 ms) is sized above the
// longest face move.
//
// The RING is the exception and keeps its pumped loop below: its travel is
// ~2.8 s (a dead panel and an unheard chord for the whole stretch), it ramps
// through an acceleration profile that masks millisecond-scale jitter, and it
// is not part of the solve's move train.
//
// The simulator cannot show any of this — its ring is a pumpDelay() and its
// steppers are free — so this trade has to be read here, not there.
//
// runToNewPosition() is moveTo() followed by run-until-done, so this is that
// with the pump spliced into the loop. It does NOT abort the move: pumpOnce()
// only keeps the display refreshing and lets the chord accumulate its hold, so
// the gesture is noticed by whatever checks abortPending() next — a ring
// stopped halfway is a ring in an unknown position, which is worse than one
// that finishes.
static void runPumpedSingle(AccelStepper &st, long newPos) {
    const bool wasMotionOnly = pumpMotionOnly;
    pumpMotionOnly = true;

    st.moveTo(newPos);
    unsigned long lastPump = millis();
    while (st.run()) {
        if (millis() - lastPump >= 5) {
            lastPump = millis();
            pumpOnce();
        }
    }

    pumpMotionOnly = wasMotionOnly;
}

// Constructor 
CubeMotors::CubeMotors(int enpin, int step_pins[7], int dir_pins[7], int ringStateEEPROMAddress)
      :upStepper(1, step_pins[0], dir_pins[0]),
    rightStepper(1, step_pins[1], dir_pins[1]),
    frontStepper(1, step_pins[2], dir_pins[2]),
     downStepper(1, step_pins[3], dir_pins[3]),
     leftStepper(1, step_pins[4], dir_pins[4]),
     backStepper(1, step_pins[5], dir_pins[5]),
     ringStepper(1, step_pins[6], dir_pins[6]) {

    EN_PIN = enpin;
    for (int i = 0; i < 7; i++) {
        STEP_PINS[i] = step_pins[i];
        DIR_PINS[i] = dir_pins[i];
    }
    this->ringStateEEPROMAddress = ringStateEEPROMAddress;
}

void CubeMotors::begin() {
    // Initialize Enable Pin
    pinMode(EN_PIN, OUTPUT);
    disableMotors();

    // Initialize Cube Steppers and Multi Stepper
    initStepper(multiStep, upStepper);
    initStepper(multiStep, rightStepper);
    initStepper(multiStep, frontStepper);
    initStepper(multiStep, downStepper);
    initStepper(multiStep, leftStepper);
    initStepper(multiStep, backStepper);


    // Initialize Ring Stepper
    initRingStepper(ringStepper); 
}

void CubeMotors::enableMotors() {
    // The dwell applies only to a REAL off->on edge. Every move calls this
    // first, and with the hold latch set the drivers are already on, so a
    // dwell on every call would silently add to every move in a held train.
    const bool wasOff = (motEnableState != 0);
    motEnableState = 0;
    digitalWrite(EN_PIN, motEnableState);
    if (wasOff && enableDwellMs > 0) {
        delay(enableDwellMs);           // blocking on purpose: same argument as
                                        // the settle in executeMove()
    }
}

void CubeMotors::disableMotors() {
    // A no-op while held — see holdBegin(). The callers that disable are
    // executeMove(), ringMove(), the aligner's exits and spinEnd()'s owner;
    // none of them know whether they are inside someone else's move train,
    // and that is the point: the owner of the train decides, once.
    if (motHeld) return;
    motEnableState = 1;
    digitalWrite(EN_PIN, motEnableState);
}

void CubeMotors::homeRingStepper(AccelStepper &ringStep) {
    enableMotors();
    // Pumped for the ABORT WATCH, not for the panel. Homing is the longest
    // single move the machine makes, and CubeSystem::begin() registers the
    // pump before cubeMotors.begin() so the chord can still be heard
    // through it.
    //
    // The display genuinely IS frozen for this stretch — runPumpedSingle()
    // sets pumpMotionOnly, which is the whole point: an LVGL render inside
    // a step loop steals the time the steps need.
    runPumpedSingle(ringStep, ringExtPos);
    runPumpedSingle(ringStep, ringRetPos);
    disableMotors();

    ringState = 0;
    ringPos   = ringRetPos;

    // Persist it. Setting ringState in RAM only meant the EEPROM sentinel (-1)
    // written before an interrupted move survived the re-home: ringMove(0) then
    // early-returns because ringState is already 0, EEPROM keeps -1, and the
    // machine performs this blind out-and-back on EVERY boot until some other
    // ring move happens to write a valid state.
    EEPROM.put(ringStateEEPROMAddress, ringState);
}

void CubeMotors::ringMove(int state) {
    if(ringState == state) {
        return;
    }

    int newPos;
    switch(state){  // Determine which state we are moving to
        case 0:     // Retracted
            newPos = ringRetPos;
            break;
        case 1:     // Halfway
            newPos = ringHalfPos;
            break;
        case 2:     // Extended
            newPos = ringExtPos;
            break;
        case 3:     // Partially between retracted and halfway
            newPos = ringPartialPos;
            break;
        default:
            return;
    }
    
    // Mark the state unknown IN EEPROM before moving, not just in RAM. The RAM
    // copy is lost on power-off, and a stale EEPROM state is trusted by
    // initRingStepper(): for the retracted case it calls setCurrentPosition()
    // without moving, so a ring physically halfway out is believed to be at
    // zero and the next extend stops short while reporting success. Same
    // discipline as CubeServo, which writes -1 before every sweep.
    ringState = -1;
    EEPROM.put(ringStateEEPROMAddress, ringState);  // persist "in motion" BEFORE moving

    enableMotors();                         // Enable motors
    runPumpedSingle(ringStepper, newPos);   // Move ring to — pumped, see above
    ringPos = newPos;                       // Update position variable
    disableMotors();                        // Disable motors
    pumpDelay(20);                          // settle before the EEPROM write

    ringState = state;                      // Update state variable
    EEPROM.put(ringStateEEPROMAddress, ringState); // Update state in EEPROM
}

void CubeMotors::ringToggle() {
    int newRingState = ringState+1;

  if(newRingState>2){
    newRingState = 0;
  }
  ringMove(newRingState);
}

void CubeMotors::applyStepSpeed() {
    for (int i = 0; i < 6; ++i) {
        AccelStepper* st = stepperFor(i);
        if (st) st->setMaxSpeed(stepSpeed);
    }
}

void CubeMotors::applyRingMotion() {
    ringStepper.setMaxSpeed(ringStepSpeed);
    ringStepper.setAcceleration(ringStepAccel);
}

AccelStepper* CubeMotors::stepperFor(int idx) {
    switch (idx) {
        case 0: return &upStepper;
        case 1: return &rightStepper;
        case 2: return &frontStepper;
        case 3: return &downStepper;
        case 4: return &leftStepper;
        case 5: return &backStepper;
        default: return nullptr;
    }
}

void CubeMotors::spinBegin(int motorIdx, float stepsPerSec) {
    spinEnd();                       // idempotent; never hold two at once
    spinStepper = stepperFor(motorIdx);
    if (!spinStepper) return;
    spinIdx  = motorIdx;
    spinBase = spinStepper->currentPosition();

    // setSpeed() is clamped to maxSpeed, so raise the ceiling first or a slow
    // spin silently becomes no spin. Restored in spinEnd().
    if (stepsPerSec < 1.0f) stepsPerSec = 1.0f;
    spinStepper->setMaxSpeed(stepsPerSec);
    spinStepper->setSpeed(stepsPerSec);
}

long CubeMotors::spinService() {
    if (!spinStepper) return 0;
    // runSpeed() steps only when the interval has elapsed, so calling it more
    // often costs nothing and calling it late just delays one step rather than
    // bunching several.
    spinStepper->runSpeed();
    return spinStepper->currentPosition() - spinBase;
}

void CubeMotors::spinEnd() {
    if (!spinStepper) return;
    // Hand the real position back to the pos[] MultiStepper works from. The
    // stepper moved without this class being told, so without it the next
    // moveTo() would compute its distance from a stale D.
    pos[spinIdx] = spinStepper->currentPosition();
    spinStepper->setMaxSpeed(stepSpeed);   // undo the ceiling spinBegin() lowered
                                           // (same value applyStepSpeed() gives the other five)
    spinStepper = nullptr;
    spinIdx     = -1;
}

long CubeMotors::getPos(int posIdx){
    if (posIdx < 0 || posIdx > 5) {
        return 0;
    }

    return pos[posIdx];
}

void CubeMotors::moveTo(long newPos[6]){
    for (int i = 0; i < 6; i++) {
        pos[i] = newPos[i];  // Update internal pos
    }
    multiStep.moveTo(pos);
    multiStep.runSpeedToPosition();  // Blocking, unpumped — see the note at the top of this file
}

void CubeMotors::resetMotorPos(){
    // Manually reset stepper position
    upStepper.setCurrentPosition(0);
    rightStepper.setCurrentPosition(0);
    frontStepper.setCurrentPosition(0);
    downStepper.setCurrentPosition(0);
    leftStepper.setCurrentPosition(0);
    backStepper.setCurrentPosition(0);

    // Reset internal position tracking
    for (int i = 0; i < 6; i++) {
        pos[i] = 0;
    }
}

void CubeMotors::executeMove(String moveString) {

    int stepDelay = defaultStepDelay;

    CubeMove move = parseMove(moveString);

    switch (move) {
        case MOVE_U:     pos[0] += turnStep; break;
        case MOVE_Up:    pos[0] -= turnStep; break;
        case MOVE_U2:    pos[0] += 2 * turnStep; break;

        case MOVE_R:     pos[1] += turnStep; break;
        case MOVE_Rp:    pos[1] -= turnStep; break;
        case MOVE_R2:    pos[1] += 2 * turnStep; break;

        case MOVE_F:     pos[2] += turnStep; break;
        case MOVE_Fp:    pos[2] -= turnStep; break;
        case MOVE_F2:    pos[2] += 2 * turnStep; break;

        case MOVE_D:     pos[3] += turnStep; break;
        case MOVE_Dp:    pos[3] -= turnStep; break;
        case MOVE_D2:    pos[3] += 2 * turnStep; break;

        case MOVE_L:     pos[4] += turnStep; break;
        case MOVE_Lp:    pos[4] -= turnStep; break;
        case MOVE_L2:    pos[4] += 2 * turnStep; break;

        case MOVE_B:     pos[5] += turnStep; break;
        case MOVE_Bp:    pos[5] -= turnStep; break;
        case MOVE_B2:    pos[5] += 2 * turnStep; break;

        // Left/Right rotation
        case MOVE_ROT_X:
            pos[4] += turnStep;
            pos[1] -= turnStep;
            stepDelay = rotStepDelay;
            break;

        // Top/Bottom rotation
        case MOVE_ROT_Z:
            pos[0] += turnStep;
            pos[3] -= turnStep;
            stepDelay = rotStepDelay;
            break;

        case MOVE_ALL:
            for(int i = 0; i < 6; i++) {
                pos[i] += turnStep;
            }
            break;

        case MOVE_INVALID:
        default:
            return;  // silently ignore
    }
    

    // Move Steppers to position
    enableMotors();
    if (stepAccel > 0) {
        runRamped();
    } else {
        multiStep.moveTo(pos);
        multiStep.runSpeedToPosition();
    }

    // Settle before torque is removed. Deliberately NOT pumpDelay: an aborted
    // pumpDelay returns in ~0 ms, which would strip the settle and de-energise
    // six steppers immediately after an abrupt stop with a clamped cube's
    // inertia still in the mechanism — strictly worse than blocking.
    delay(stepDelay);
    disableMotors();
}

void CubeMotors::runRamped() {
    // Unpumped, like the MultiStepper path, and for the same reason (see the
    // note at the top of this file).
    AccelStepper* moving[6];
    int n = 0;
    for (int i = 0; i < 6; ++i) {
        AccelStepper* st = stepperFor(i);
        if (!st || st->currentPosition() == pos[i]) continue;

        // Reset the profile state before EVERY ramped move. MultiStepper's
        // moveTo() — which the alignment loop uses between face moves — leaves
        // each stepper believing it is already travelling at full speed
        // (_speed = +-stepSpeed, _n = 1), and AccelStepper::run() started from
        // that state computes its stopping distance from the stale speed and
        // launches at the cruise rate with no ramp at all. setCurrentPosition()
        // with the position it already has zeroes _speed, _n and the interval
        // without moving anything; it is the only public way to do that.
        st->setCurrentPosition(st->currentPosition());
        st->setMaxSpeed(stepSpeed);
        st->setAcceleration(stepAccel);
        st->moveTo(pos[i]);
        moving[n++] = st;
    }

    // One pass per loop, every participant every pass. run() is cheap when no
    // step is due, so the loop rate is set by the step intervals and the
    // participants cannot drift apart: same distance, same profile, same
    // micros() sample each pass.
    bool any;
    do {
        any = false;
        for (int k = 0; k < n; ++k) any |= moving[k]->run();
    } while (any);
}

// Private Methods
void CubeMotors::initStepper(MultiStepper &multiStepper, AccelStepper &newStepper) {
    newStepper.setCurrentPosition(0);
    newStepper.setMaxSpeed(stepSpeed);
    newStepper.setPinsInverted(true, false);    // Reverse direction
    multiStepper.addStepper(newStepper);
}

void CubeMotors::initRingStepper(AccelStepper &ringStep) {// Initialize Ring Position
  ringStep.setMaxSpeed(ringStepSpeed);
  ringStep.setAcceleration(ringStepAccel);
  ringStep.setPinsInverted(true, false);    // Reverse direction

  // Read saved state from EEPROM.
  //
  // Must use get(), not read(): ringMove() persists this with EEPROM.put(),
  // which writes sizeof(int) == 4 bytes, while EEPROM.read() returns only the
  // first byte — fine for 0-3 by little-endian accident, but the "in motion"
  // sentinel -1 (0xFFFFFFFF) comes back as 255, and getRingState() promises
  // callers a -1.
  EEPROM.get(ringStateEEPROMAddress, ringState);

  // Assign position based on state, or rehome if unknown.
  switch(ringState){  // Determine which state we are moving to
    case 0:     // Retracted
      ringPos = ringRetPos;
      ringStep.setCurrentPosition(ringPos);
      break;
    case 1:     // Halfway
      ringPos = ringHalfPos;
      ringStep.setCurrentPosition(ringPos);
      ringMove(0);
      break;
    case 2:     // Extended
      ringPos = ringExtPos;
      ringStep.setCurrentPosition(ringPos);
      ringMove(0);
      break;

    case 3:     // Partial Retract (Between retracted and halfway)
        ringPos = ringPartialPos;
        ringStep.setCurrentPosition(ringPos);
        ringMove(0);
        break;

    default:
        ringState = -1;
        homeRingStepper(ringStep);
        return;
  }
}

CubeMotors::CubeMove CubeMotors::parseMove(const String &move){
    if (move == "U") return MOVE_U;
    if (move == "U'") return MOVE_Up;
    if (move == "U2") return MOVE_U2;
    if (move == "R") return MOVE_R;
    if (move == "R'") return MOVE_Rp;
    if (move == "R2") return MOVE_R2;
    if (move == "F") return MOVE_F;
    if (move == "F'") return MOVE_Fp;
    if (move == "F2") return MOVE_F2;
    if (move == "D") return MOVE_D;
    if (move == "D'") return MOVE_Dp;
    if (move == "D2") return MOVE_D2;
    if (move == "L") return MOVE_L;
    if (move == "L'") return MOVE_Lp;
    if (move == "L2") return MOVE_L2;
    if (move == "B") return MOVE_B;
    if (move == "B'") return MOVE_Bp;
    if (move == "B2") return MOVE_B2;
    if (move == "ROTX") return MOVE_ROT_X;
    if (move == "ROTZ") return MOVE_ROT_Z;
    if (move == "ALL") return MOVE_ALL;

    return MOVE_INVALID;
}
