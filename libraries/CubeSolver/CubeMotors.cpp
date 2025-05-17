#include "CubeMotors.h"

// Constructor
CubeMotors::CubeMotors(int enpin, int step_pins[7], int dir_pins[7], int ringStateEEPROMAddress)
    :    upStepper(1, step_pins[0], dir_pins[0]),
      rightStepper(1, step_pins[1], dir_pins[1]),
      frontStepper(1, step_pins[2], dir_pins[2]),
       downStepper(1, step_pins[3], dir_pins[3]),
       backStepper(1, step_pins[4], dir_pins[4]),
       leftStepper(1, step_pins[5], dir_pins[5]),
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

    // Initialize stepper position:
    upStepper.setCurrentPosition(0);
    rightStepper.setCurrentPosition(0);
    frontStepper.setCurrentPosition(0);
    downStepper.setCurrentPosition(0);
    leftStepper.setCurrentPosition(0);
    backStepper.setCurrentPosition(0);

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
    EEPROM.update(ringStateEEPROMAddress, state); // Update state in EEPROM
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

void CubeMotors::executeMove(String move) {
    int stepDelay = defaultStepDelay;

    if (move == "U") {
        // Call the function for U move
        pos[0] += turnStep;
        //myCube.rotU(1);
        
    } else if (move == "U'") {
        // Call the function for U' move
        pos[0] -= turnStep;
        //myCube.rotU(-1);
        
    } else if (move == "U2") {
        // Call the function for U2 move
        pos[0] += turnStep*2;
        //myCube.rotU(2);
        
    } else if (move == "R") {
        // Call the function for R move
        pos[1] += turnStep;
        //myCube.rotR(1);
        
    } else if (move == "R'") {
        // Call the function for R' move
        pos[1] -= turnStep;
        //myCube.rotR(-1);
        
    } else if (move == "R2") {
        // Call the function for R2 move
        pos[1] += turnStep*2;
        //myCube.rotR(2);
        
    } else if (move == "F") {
        // Call the function for F move
        pos[2] += turnStep;
        //myCube.rotF(1);
        
    } else if (move == "F'") {
        // Call the function for F' move
        pos[2] -= turnStep;
        //myCube.rotF(-1);
        
    } else if (move == "F2") {
        // Call the function for F2 move
        pos[2] += turnStep*2;
        //myCube.rotF(2);
        
    } else if (move == "D") {
        // Call the function for D move
        pos[3] += turnStep;
        //myCube.rotD(1);
        
    } else if (move == "D'") {
        // Call the function for D' move
        pos[3] -= turnStep;
        //myCube.rotD(-1);
        
    } else if (move == "D2") {
        // Call the function for D2 move
        pos[3] += turnStep*2;
        //myCube.rotD(2);
        
    } else if (move == "L") {
        // Call the function for L move
        pos[4] += turnStep;
        //myCube.rotL(1);
        
    } else if (move == "L'") {
        // Call the function for L' move
        pos[4] -= turnStep;
        //myCube.rotL(-1);
        
    } else if (move == "L2") {
        // Call the function for L2 move
        pos[4] += turnStep*2;
        //myCube.rotL(2);
        
    } else if (move == "B") {
        // Call the function for B move
        pos[5] += turnStep;
        //myCube.rotB(1);
        
    } else if (move == "B'") {
        // Call the function for B' move
        pos[5] -= turnStep;
        //myCube.rotB(-1);
        
    } else if (move == "B2") {
        // Call the function for B2 move
        pos[5] += turnStep*2;
        //myCube.rotB(2);
    }
    else if (move == "ROT1") {
        // Call the function for B2 move
        pos[4] += turnStep;
        pos[1] -= turnStep;
        stepDelay = rotStepDelay;
    } 

    else if (move == "ROT2") {
        // Call the function for B2 move
        pos[0] += turnStep;
        pos[3] -= turnStep;
        stepDelay = rotStepDelay;
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

    default:
        ringState = -1;
        homeRingStepper(ringStep);
        return;
  }
}
