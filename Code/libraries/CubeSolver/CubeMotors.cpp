#include "CubeMotors.h"
#include "CubePump.h"


// Run a MultiStepper move to completion while servicing the cooperative pump.
//
// MultiStepper::runSpeedToPosition() is a blocking loop over run(); this is the
// same loop with a pump call, so it is behaviourally identical to the stepper
// but leaves no blind window. That matters more than it sounds: a half turn is
// 200 steps at 1000 steps/s = 200 ms, and pumpTick() treats a >200 ms poll gap
// as "we were blind, restart the hold timer" — so with the blocking version the
// SELECT-held abort gesture could never accumulate its 1 s during a solve
// containing half turns. Measured before this change: a 20-move solution with
// SELECT held for 5.2 s never fired the abort.
//
// It also unfreezes the display DURING the move rather than only after it.
static void runPumped(MultiStepper &ms) {
    unsigned long lastPump = millis();
    while (ms.run()) {
        if (millis() - lastPump >= 5) {
            lastPump = millis();
            pumpOnce();
        }
    }
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
    motEnableState = 0;
    digitalWrite(EN_PIN, motEnableState);
}

void CubeMotors::disableMotors() {
    motEnableState = 1;
    digitalWrite(EN_PIN, motEnableState);
}

void CubeMotors::homeRingStepper(AccelStepper &ringStep) {
    enableMotors();
    ringStep.runToNewPosition(ringExtPos);
    ringStep.runToNewPosition(ringRetPos);
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
    
    // Mark the state unknown IN EEPROM before moving, not just in RAM.
    //
    // The `ringState = -1` line below always had the right intent ("so it will
    // rehome if turned off midway") but only ever touched the RAM copy, which
    // is lost on power-off. EEPROM kept the previous — now wrong — state for
    // the entire ~2.8 s of travel. Lose power mid-move and initRingStepper()
    // trusts it: for the retracted case it calls setCurrentPosition() without
    // moving, so a ring physically halfway out is believed to be at zero, and
    // the next extend travels half the distance and stops short while
    // reporting success.
    //
    // CubeServo already does this correctly (writes -1 before sweeping); the
    // ring simply was not updated to match.
    ringState = -1;
    EEPROM.put(ringStateEEPROMAddress, ringState);  // persist "in motion" BEFORE moving

    enableMotors();                         // Enable motors
    ringStepper.runToNewPosition(newPos);   // Move ring to
    ringPos = newPos;                       // Update position variable
    disableMotors();                        // Disable motors
    delay(20);                              // settle before the EEPROM write

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
    runPumped(multiStep);   // blocking, but pumped — see runPumped()
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
    multiStep.moveTo(pos);
    runPumped(multiStep);

    // Settle before torque is removed. Deliberately NOT pumpDelay: an aborted
    // pumpDelay returns in ~0 ms, which would strip the 50 ms settle and
    // de-energise six steppers immediately after an abrupt stop with a clamped
    // cube's inertia still in the mechanism — strictly worse than blocking.
    delay(stepDelay);
    disableMotors();
    delay(stepDelay);       // decay time; see note above

    

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
  // first byte. That worked by little-endian accident for values 0-3, but the
  // "in motion" sentinel -1 (0xFFFFFFFF) came back as 255 rather than -1. It
  // still landed in the default branch and re-homed, so the behaviour happened
  // to be right — but for the wrong reason, and it would break the moment
  // anyone compared ringState against -1 directly.
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
