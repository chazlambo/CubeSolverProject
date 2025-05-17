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
        if (MotorPots[i]->loadCalibration()) return false;
    }
    return true;
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
            int targetVal = MotorPots[i]->getCalibration();

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

void CubeSystem::toggleTopServo() {
    topServo.toggle();
}

void CubeSystem::toggleBotServo() {
    botServo.toggle();
}

void CubeSystem::toggleRing() {
    cubeMotors.ringToggle();
}

void CubeSystem::extendRing() {
    cubeMotors.ringMove(1);
}

void CubeSystem::retractRing() {
    cubeMotors.ringMove(0);
}

void CubeSystem::executeMove(const String &move){
    cubeMotors.executeMove(move);
}
