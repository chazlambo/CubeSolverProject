#include "CubeServo.h"

CubeServo::CubeServo(int pin, int eepromAddr, unsigned int retPos, unsigned int extPos, int sweepDelay)
  : pin(pin), eepromAddr(eepromAddr), currentPos(retPos), retPos(retPos), extPos(extPos), extState(-1), sweepDelay(sweepDelay) {}

void CubeServo::begin() {
    servo.attach(pin);
    loadStateFromEEPROM();

    // Check if we need to retract servo then do it.
    switch (extState) {
        case 0: // Retracted
            currentPos = retPos;
            break;
        case 1: // Extended
            currentPos = extPos;
            retract(); // Always start in retracted position
            break;
        default: // Unknown
            currentPos = (extPos - retPos) / 2;
            retract();
            extState = 0;
            break;
    }
}

void CubeServo::extend() {
    // Set to unknown state temporarily
    extState = -1;
    updateEEPROM();

    // Sweep servo to desired position
    sweepTo(extPos);
    currentPos = extPos;

    // Update state
    extState = 1;
    updateEEPROM();
}

void CubeServo::retract() {
    // Set to unknown state temporarily
    extState = -1;
    updateEEPROM();

    // Sweep servo to desired position
    sweepTo(retPos);
    currentPos = retPos;

    // Update state
    extState = 0;
    updateEEPROM();
}

void CubeServo::toggle() {
    if(extState==1) {  // If Extended
    retract();  // Retract
  }
  else {                // If not extended
    extend();   // Extend
  }
}

bool CubeServo::isExtended() {
    return extState == 1;
}

void CubeServo::sweepTo(unsigned int newPos) {

    // Map 270 degree range to function that takes 180 degree input
    unsigned int currentAngle = map(currentPos, 0, 270, 0, 180);
    unsigned int newAngle = map(newPos, 0, 270, 0, 180);

    if(currentAngle < newAngle) {                               // If servo needs to go forwards
        for(unsigned int i=currentAngle; i < newAngle; i++) {   // Sweep through all angles in between current and desired position
        servo.write(i);                                         // Write servo to new angle
        delay(sweepDelay);                                      // Delay set amount
        }
    }
    else if(currentAngle > newAngle) {                          // If servo needs to go backwards
        for(unsigned int i=currentAngle; i > newAngle; i--) {   // Sweep through all angles in between current and desired position
        servo.write(i);                                         // Write servo to new angle
        delay(sweepDelay);                                      // Delay set amount
        }
    }
}

void CubeServo::loadStateFromEEPROM() {
    extState = EEPROM.read(eepromAddr);
}

void CubeServo::updateEEPROM() {
    EEPROM.update(eepromAddr, extState);
}
