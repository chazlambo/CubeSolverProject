#include "CubeMotors.h"

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
    
    ringState = -1;                         // Set state to unknown so it will rehome if turned off midway
    enableMotors();                         // Enable motors
    ringStepper.runToNewPosition(newPos);   // Move ring to 
    ringPos = newPos;                       // Update position variable
    disableMotors();                        // Disable motors
    delay(20);                              // Short delay for timing

    ringState = state;                      // Update state variable
    EEPROM.put(ringStateEEPROMAddress, state); // Update state in EEPROM
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
    multiStep.runSpeedToPosition();  // Blocking run
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
    multiStep.runSpeedToPosition();
    delay(stepDelay);               // Delay to keep motors holding position to negate rotational inertia

    disableMotors();
}

// Private Methods
void CubeMotors::initStepper(MultiStepper &multiStepper, AccelStepper &newStepper) {
    newStepper.setCurrentPosition(0);
    newStepper.setMaxSpeed(stepSpeed);
    multiStepper.addStepper(newStepper);
}

void CubeMotors::initRingStepper(AccelStepper &ringStep) {// Initialize Ring Position
  ringStep.setMaxSpeed(ringStepSpeed);
  ringStep.setAcceleration(ringStepAccel);

  // Read saved state from EEPROM
  ringState = EEPROM.read(ringStateEEPROMAddress);

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
