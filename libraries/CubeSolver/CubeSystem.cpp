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
    // Returns if motor potentiometers are calibrated
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

            // Find the closest of the 4 calibration values to move to
            int minDiff = 256;
            int targetVal = MotorPots[i]->getCalibration(0);
            for (int j = 0; j < 4; j++) {
                int calVal = MotorPots[i]->getCalibration(j);
                int diff = abs(currentVal - calVal);
                if (diff > 128) diff = 256 - diff;  // Account for wraparound

                if (diff < minDiff) {
                    minDiff = diff;
                    targetVal = calVal;
                }
            }

            // Get position values
            pos[i] = cubeMotors.getPos(i);

            // Move motor if not within threshold of calibrated value
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

bool CubeSystem::getColorCalibration(){
    // Returns if color sensors are calibrated
    if (colorSensor1->loadCalibration() != 0) return false;
    if (colorSensor2->loadCalibration() != 0) return false;
    
    return true;
}

int CubeSystem::calibrateColorSensors(){
    // Outputs:
    //  0 - Success
    //  1 - Cube not loaded (NOT IMPLEMENTED)

    // ------------ STEP 1 ------------
    // Scan first 4 sides

    // Order of faces being scanned in step 1
    const char faceColors[4][2] = {
        {'R', 'G'}, // Red Left, Green Back
        {'B', 'R'}, // Blue Left, Red Back
        {'O', 'B'}, // Orange Left, Blue Back
        {'G', 'O'}  // Green Left, Orange Back
    };

    // Scan the four side faces
    for (int rot = 0; rot < 4; rot++) {

        // Scan current face configuration
        colorSensor1.scanFace();
        colorSensor2.scanFace();

        // Set color calibration values for designated faces
        for (int i = 0; i < 9; i++) {
            colorSensor1.setColorCal(i, faceColors[rot][0], colorSensor1.scanVals[i]);
            colorSensor2.setColorCal(i, faceColors[rot][1], colorSensor2.scanVals[i]);
        }

        // Rotate cube orientation (skip last time)
        if (rot < 3) {
            topServoExtend();
            botServoExtend();
            delay(500);
            executeMove("ROTZ");
            delay(200);
            topServoRetract();
            botServoRetract();
            delay(500);
        }
    }

    // ------------ STEP 2 ------------
    // Scan empty slot

    // Rotate cube 
    botServoExtend();
    ringMiddle();
    delay(500);
    topServoRetract();
    botServoRetract();
    delay(500);

    // Scan and set color calibration values for empty face
    colorSensor1.scanFace();
    colorSensor2.scanFace();
    for (int i = 0; i < 9; i++) {
        colorSensor1.setColorCal(i, 'E', colorSensor1.scanVals[i]);
        colorSensor2.setColorCal(i, 'E', colorSensor2.scanVals[i]);
    }

    // Rotate cube about x-axis and retract
    executeMove("ROTX");
    delay(300);
    botServoExtend();
    ringRetract();
    delay(300);
    botServoRetract();

    // ------------ STEP 3 ------------
    // Scan remaining top/bottom faces

    // Order of faces being scanned in step 3
    const char topFaces[4][2] = {
        {'G', 'Y'}, // Green Left, Yellow Back
        {'W', 'G'}, // White Left, Green Back
        {'B', 'W'}, // Blue Left, White Back
        {'Y', 'B'}  // Yellow Left, Blue Back
    };
    
    // Scan the four side faces
    for (int rot = 0; rot < 4; rot++) {
        colorSensor1.scanFace();
        colorSensor2.scanFace();

        // Set color calibration values for designated faces
        for (int i = 0; i < 9; i++) {
            colorSensor1.setColorCal(i, topFaces[rot][0], colorSensor1.scanVals[i]);
            colorSensor2.setColorCal(i, topFaces[rot][1], colorSensor2.scanVals[i]);
        }

        // Rotate cube orientation (skip last time)
        if (rot < 3) {
            botServoExtend();
            delay(300);
            executeMove("ROTZ");
            delay(300);
            botServoRetract();
        }
    }

    // Save calibrated values to EEPROM
    colorSensor1.saveCalibration();
    colorSensor2.saveCalibration();

    return 0;
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

