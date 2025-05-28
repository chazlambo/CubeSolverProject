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

    // Motor Potentiometer Setup
    for (int i = 0; i < numMotors; i++) {
        MotorPots[i]->begin();
    }

    // Begin Rotary Encoder
    // menuEncoder.begin();

    // Motor Initialization
    motorHomeState = -1;
    if(getMotorCalibration()){
        homeMotors();
    }
}

int CubeSystem::scanCube(){
    // Scans cube colors and builds virtual cube for solving
    // Outputs:
    //  0 - Success
    //  1 - Color Sensor 1 encountered an invalid face value
    //  2 - Color Sensor 2 encountered an invalid face value
    //  3 - Opposite faces scanned next to eachother (scan error)
    //  1X - Sensor 1 failed to set color array
    //  2X - Sensor 2 failed to set color array

    // Reset the virtual cube before scanning
    virtualCube.resetCube();

    // Move and scan the cube
    // Scan order is [L, B], [U, F], [D, R]
    for (int i = 0; i < 3; i++) {
        // Scan Sensors
        colorSensor1.scanFace();
        colorSensor2.scanFace();

        // Determine face being scanned by each sensor
        char face1 = colorSensor1.getColor(4, colorSensor1.getScanValRow(4));
        char face2 = colorSensor2.getColor(4, colorSensor2.getScanValRow(4));

        // TODO: DEBUG (REMOVE LATER)
        Serial.print("face1: "); Serial.println(face1);
        Serial.print("face2: "); Serial.println(face2);
        
        
        if (face1 == 'U') return 1;
        if (face2 == 'U') return 2;

        // Determine face left of the sensor (for orientation)
        char left1 = face2;     // Sensor 2 is to the left of Sensor 1
        char left2;             // Left 2 is opposite of sensor 1 
        switch (face1) {
            case 'R': left2 = 'O'; break;
            case 'O': left2 = 'R'; break;
            case 'W': left2 = 'Y'; break;
            case 'Y': left2 = 'W'; break;
            case 'G': left2 = 'B'; break;
            case 'B': left2 = 'G'; break;
            default:  left2 = 'X';  // Invalid
        }

        // TODO: DEBUG (REMOVE LATER)
        Serial.print("left1: "); Serial.println(left1);
        Serial.print("left2: "); Serial.println(left2);

        // Check for impossible face pairing: opposite faces scanned adjacent
        if (left2 == face2) return 3;

        // Convert scan values to color array
        char face1_colors[9];
        char face2_colors[9];
        colorSensor1.getFaceColors(face1_colors);
        colorSensor2.getFaceColors(face2_colors);

        // TODO: DEBUG (REMOVE LATER)
        Serial.print("face1_colors: ");
        for (int i = 0; i < 9; i++) {
        Serial.print(face1_colors[i]); Serial.print(' ');
        }
        Serial.print("\nface2_colors: ");
        for (int i = 0; i < 9; i++) {
        Serial.print(face2_colors[i]); Serial.print(' ');
        }
        Serial.println();

        // Update virtual cube
        int res1 = virtualCube.setColorArray(face1, face1_colors, left1);
        int res2 = virtualCube.setColorArray(face2, face2_colors, left2);
        if(res1) return 10+res1;
        if(res2) return 20+res2;

        // Rotate the cube to next scanning orientation
        if (i < 2) {
            botServoExtend();
            delay(500);
            executeMove("ROTZ");
            ringMiddle();
            botServoPartial();
            delay(500);
            executeMove("ROTX");
            botServoExtend();
            delay(500);
            ringRetract();
            botServoRetract();
            delay(500);
        }
    }

    return 0; // Success
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
    if (colorSensor1.loadCalibration() != 0) return false;
    if (colorSensor2.loadCalibration() != 0) return false;
    
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
            colorSensor1.setColorCal(i, faceColors[rot][0], colorSensor1.getScanValRow(i));
            colorSensor2.setColorCal(i, faceColors[rot][1], colorSensor2.getScanValRow(i));
        }

        // Rotate cube orientation (skip last time)
        if (rot < 3) {
            botServoPartial();
            //topServoExtend();
            delay(500);
            executeMove("ROTZ");
            //topServoRetract();
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
    botServoRetract();
    delay(500);

    // Scan and set color calibration values for empty face
    colorSensor1.scanFace();
    colorSensor2.scanFace();
    for (int i = 0; i < 9; i++) {
        colorSensor1.setColorCal(i, 'E', colorSensor1.getScanValRow(i));
        colorSensor2.setColorCal(i, 'E', colorSensor2.getScanValRow(i));
    }

    // Rotate cube about x-axis and retract
    executeMove("ROTX");
    botServoExtend();
    delay(500);
    ringRetract();
    botServoRetract();
    delay(500);

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
            colorSensor1.setColorCal(i, topFaces[rot][0], colorSensor1.getScanValRow(i));
            colorSensor2.setColorCal(i, topFaces[rot][1], colorSensor2.getScanValRow(i));
        }

        // Rotate cube orientation (skip last time)
        if (rot < 3) {
            botServoExtend();
            //topServoExtend();
            delay(500);
            executeMove("ROTZ");
            //topServoRetract();
            botServoRetract();
            delay(500);
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

void CubeSystem::topServoPartial()
{
    topServo.partial();
}

void CubeSystem::botServoExtend() {
    botServo.extend();
}

void CubeSystem::botServoRetract() {
    botServo.retract();
}

void CubeSystem::botServoPartial()
{
    botServo.partial();
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

int CubeSystem::executeMove(const String &move, bool moveVirtual, bool align){
    // Function to execute any turn of the cube
    // Inputs:
    //  bool moveVirtual - set true to turn virtual cube
    //  bool align       - set true to activate alignment after movement
    // Outputs:
    //  0 - Ran succesfully
    //  1X - Virtual move failed (X is error thrown by virtual move)
    //  2X - Home motors failed

    // Turn real cube side
    cubeMotors.executeMove(move);

    // Turn Virtual Cube
    int result = 0;
    if (moveVirtual) {
        result = virtualCube.executeMove(move);
        if(result) return 10+result;
    }

    // If alignment is active and motor is misaligned
    result = 0;
    if(align && !checkAlignment()){
        result = homeMotors();
        if(result) return 20+result;
    }

    return 0;   // Success
}

bool CubeSystem::checkAlignment()
{
    // Returns true if aligned
    // Returns false if one or more motors is misaligned
    for (int i = 0; i < numMotors; i++) {
        int currentVal = MotorPots[i]->scan();
        bool aligned = false;

        for (int j = 0; j < 4; j++) {
            int calVal = MotorPots[i]->getCalibration(j);
            if (abs(currentVal - calVal) <= motorAlignmentTol) {
                aligned = true;
                break;
            }
        }

        if (!aligned) {
            return false;
        }
    }

    return true;
}