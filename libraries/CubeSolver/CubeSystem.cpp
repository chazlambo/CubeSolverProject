// CubeSystem.cpp
#include "CubeSystem.h"

CubeSystem::CubeSystem() {}

void CubeSystem::begin() {    
    // Serial Communication Setup
    Serial.begin(baudRate);

    // Power Setup
    pinMode(POWPIN, INPUT);

    // IDC Setup
    Wire.begin();

    // Initialize ADC modules
    for (int i = 0; i < 6; i++) {
        ADC[i]->begin(ADC_ADDRESS[i]);
    }

    // Motor Setup
    cubeMotors.begin();

    // Servo Setup
    topServo.begin();
    botServo.begin();

    // Color Sensors Setup
    colorSensor1.begin();
    colorSensor2.begin();

    // MOtor Potentiometer Setup
    for (int i = 0; i < numMotors; i++) {
        MotorPots[i]->begin();
    }

    // Begin Rotary Encoder
    menuEncoder.begin();

    // Motor Initialization
    motorHomeState = -1;
    homeMotors();

}

bool CubeSystem::powerCheck() {
    return digitalRead(POWPIN);
}

bool CubeSystem::getMotorCalibration() {
    for (int i = 0; i < numMotors; i++) {
        if (MotorPots[i]->loadCalibration() != 0) return false;
    }
    return true;
}

int CubeSystem::calibrateMotorRotations(){
    int rawVals[6][4];

    cubeMotors.resetMotorPos();

    // Scan current position
    for (int i = 0; i < numMotors; i++) {
        rawVals[i][0] = MotorPots[i]->scan();
    }

    // Rotate and scan 3 more times
    for (int step = 1; step < 4; step++) {
        executeMove("ALL");

        for (int i = 0; i < numMotors; i++) {
            rawVals[i][step] = MotorPots[i]->scan();
        }
    }
    executeMove("ALL"); // Return to original position

    // Sort and rearrange each motor’s vector
    for (int i = 0; i < numMotors; i++) {
        // Sort ascending
        std::sort(rawVals[i], rawVals[i] + 4); // Sorts each motors calibrated values

        // Rotate: [a, b, c, d] → [b, c, d, a] so ~90 val is first
        int reordered[4] = {
            rawVals[i][1],
            rawVals[i][2],
            rawVals[i][3],
            rawVals[i][0]   
        };

        // Write into MotorPot
        for (int j = 0; j < 4; j++) {
            MotorPots[i]->setCalibration(j, reordered[j]);
        }

        // Write calibrated values to EEPROM
        MotorPots[i]->saveCalibration();
    }

    return 0;
}

int CubeSystem::homeMotors() {
    // Homes motors
    //
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    if (!getMotorCalibration()) return 1;

    // Initialize variables
    bool aligned = false;
    unsigned long t_home_start = millis();
    unsigned long t_home = millis();
    long pos[6];    // Temporary position vector

    // Retrieve calibrated values and check if calibrated
    if (!getMotorCalibration()) {
        return 1;
    }

    // Start loop
    cubeMotors.enableMotors();
    while (!aligned) {
        aligned = true;

        // Check each motor
        for (int i = 0; i < numMotors; i++) {

            // Update potentiometer values
            int currentVal = MotorPots[i]->scan();
            int targetVal = MotorPots[i]->getCalibration(0);

            // Get position values
            pos[i] = cubeMotors.getPos(i);

            if (abs(currentVal - targetVal) > threshold) {
                aligned = false;

                // If Upper Motor
                if (i == 0) { // Reverse direction for upper motor
                    if(currentVal > targetVal) {
                        pos[i] -= stepSize;
                    }
                    else {
                        pos[i] += stepSize;
                    }
                }

                // If any other motor
                else {
                    if(currentVal > targetVal) {
                        pos[i] += stepSize;
                    }
                    else {
                        pos[i] -= stepSize;
                    }
                }
            }
        }

        // Apply new positions
        cubeMotors.moveTo(pos);

        // Add function timeout
        t_home = millis();
        if(t_home - t_home_start > timeout) {
            return 2;
        }
    }
    cubeMotors.disableMotors();

    // Manually reset stepper position
    cubeMotors.resetMotorPos();
    
    return 0;   // Return successful alignment
}

void CubeSystem::topServoExtend() {
    topServo.extend();
}

void CubeSystem::topServoRetract() {
    topServo.retract();
}

void CubeSystem::botServoExtend() {
    botServo.extend();
}

void CubeSystem::botServoRetract() {
    botServo.retract();
}

void CubeSystem::toggleTopServo() {
    topServo.toggle();
}

void CubeSystem::toggleBotServo() {
    botServo.toggle();
}

void CubeSystem::toggleRing() {
    cubeMotors.ringToggle();
}

void CubeSystem::ringExtend() {
    cubeMotors.ringMove(2);
}

void CubeSystem::ringMiddle() {
    cubeMotors.ringMove(1);
}

void CubeSystem::ringRetract() {
    cubeMotors.ringMove(0);
}



void CubeSystem::executeMove(const String &move){
    cubeMotors.executeMove(move);
}
