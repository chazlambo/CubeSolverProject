// CubeSystem.cpp
#include "CubeSystem.h"

CubeSystem::CubeSystem() {}

void CubeSystem::begin() {    
    // Serial Communication Setup
    Serial.begin(baudRate);

    // Power Setup
    // pinMode(POWPIN, INPUT);
    // Serial.println("Waiting for power.");
    // bool powerCheck = 0;

    // while (!powerCheck){
    //     powerCheck = digitalRead(POWPIN);
    //     delay(20);
    // }
    
    // I2C Setup for color sensors and motor encoders
    // Wire (I2C0) uses pins: 18 (SDA0), 19 (SCL0)
    Wire.begin();
    Wire.setClock(100000);  // 100kHz for stability
    Serial.println("Wire (I2C0) initialized on pins 18/19 at 100kHz");

    // Display Setup
    displayInitialized = cubeDisplay.begin(10000000);


    // Motor Setup
    cubeMotors.begin();

    // Servo Setup
    topServo.begin();
    botServo.begin();

    // Color Sensors Setup
    colorSensor1.begin();
    colorSensor2.begin();

    // Motor Encoder Setup
    encoderMux.begin();                  // Begins I2C Mux for encoders
    for (int i = 0; i < 7; ++i) {
        int rc = MotorEncoders[i]->begin();
        if (rc != 0) {
            Serial.print("MotorEncoder "); Serial.print(i);
            Serial.print(" begin() failed rc="); Serial.println(rc);
        }
    }

    // Begin Rotary Encoder on Wire1 (separate I2C bus)
    menuEncoder.begin();

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
    //  3X - Failed to set orientation
    //  4X - Failed to build unoriented cube array
    //  5X - Failed to build cube array

    // Reset the virtual cube before scanning
    virtualCube.resetCube();

    // Move and scan the cube
    // Scan order is [L, B], [U, F], [D, R]
    char lastface1 = 'X';
    char lastface2 = 'X';

    for (int i = 0; i < 3; i++) {
        // Scan Sensors
        colorSensor1.scanFace();
        colorSensor2.scanFace();

        // Determine face being scanned by each sensor
        char face1 = colorSensor1.getColor(4, colorSensor1.getScanValRow(4));
        char face2 = colorSensor2.getColor(4, colorSensor2.getScanValRow(4));
        lastface1 = face1; lastface2 = face2;

        // TODO: DEBUG (REMOVE LATER)
        Serial.print("face1: "); Serial.println(face1);
        Serial.print("face2: "); Serial.println(face2);
        
        
        if (face1 == 'U') return 1;
        if (face2 == 'U') return 2;

        // Determine face left of the sensor (for orientation)
        char left1 = face2;     // Sensor 2 is to the left of Sensor 1
        char left2 = face1;     // Sensor 2 is read upside down, making sensor 1 to the left of it

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
            delay(servoDelay);
            ringMiddle();
            botServoPartial();
            delay(servoDelay);
            executeMove("ROTX");
            botServoExtend();
            delay(servoDelay);
            ringRetract();  // Used to be partial?
            executeMove("ROTZ");
            botServoRetract();
            delay(servoDelay);
        }
    }

    // Set Orientation of Cube
    char rightColor = lastface2;
    char backColor  = lastface1;

    // The cube's left face is opposite the right face
    char leftColor;
    switch (rightColor) {
    case 'R': leftColor = 'O'; break;
    case 'O': leftColor = 'R'; break;
    case 'G': leftColor = 'B'; break;
    case 'B': leftColor = 'G'; break;
    case 'W': leftColor = 'Y'; break;
    case 'Y': leftColor = 'W'; break;
    default:  leftColor = 'U'; // Invalid fallback
    }

    // Build Unoriented Cube Array
    int e = virtualCube.buildUnorientedCubeArray();
    if (e)   return 40 + e;

    // Pass corrected orientation to virtual cube
    e = virtualCube.setOrientation(leftColor, backColor);
    if (e)   return 30 + e;

    // Build Cube Array
    e = virtualCube.buildCubeArray();
    if (e)   return 50 + e;

    // TODO: DEBUG (REMOVE LATER)
    Serial.println("=== UNORIENTED CUBE ARRAY ===");
    virtualCube.printUnorientedCubeArray();
    
    Serial.println("=== ORIENTED CUBE ARRAY ===");
    virtualCube.printCubeArray();

    return 0; // Success
}

bool CubeSystem::powerCheck() {
    return digitalRead(POWPIN);
}

bool CubeSystem::getMotorCalibration() {
    // Returns if motor encoders are calibrated
    for (int i = 0; i < numMotors; i++) {
        if (MotorEncoders[i]->loadCalibration() != 0) return false;
    }
    return true;
}

int CubeSystem::calibrateMotorRotations(){
    int rawVals[6][4];

    cubeMotors.resetMotorPos();

    // Scan current position
    for (int i = 0; i < numMotors; i++) {
        rawVals[i][0] = MotorEncoders[i]->scan();
    }

    // Rotate and scan 3 more times
    for (int step = 1; step < 4; step++) {
        executeMove("ALL");
        delay(200);

        for (int i = 0; i < numMotors; i++) {
            rawVals[i][step] = MotorEncoders[i]->scan();
        }
    }
    executeMove("ALL"); // Return to original position

    // Sort and rearrange each motor's vector
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

        // Write into MotorEncoders
        for (int j = 0; j < 4; j++) {
            MotorEncoders[i]->setCalibration(j, reordered[j]);
        }

        // Write calibrated values to EEPROM
        MotorEncoders[i]->saveCalibration();
    }

    return 0;
}

int CubeSystem::alignMotorsInternal(bool selectiveAlign) {
    // Internal alignment function used by both homeMotors() and alignMotors()
    // 
    // selectiveAlign = false: Align all motors (full homing)
    // selectiveAlign = true:  Only align motors marked in motorMoved[]
    //
    // Outputs:
    //      0 - Success
    //      2 - Did not reach threshold in time

    bool aligned = false;
    unsigned long t_start = millis();
    unsigned long timeout = selectiveAlign ? alignTimeout : homeTimeout;
    long pos[6];

    // Initialize motor positions
    for (int i = 0; i < numMotors; i++) {
        pos[i] = cubeMotors.getPos(i);
    }

    // Enable motors
    cubeMotors.enableMotors();

    // Alignment loop
    while (!aligned) {
        aligned = true;

        // Check each motor
        for (int i = 0; i < numMotors; i++) {

            // Skip if selective alignment and motor didn't move
            if (selectiveAlign && !motorMoved[i]) {
                continue;
            }

            // Update encoder values
            int currentVal = MotorEncoders[i]->scan();

            // Find the closest of the 4 calibration values to move to
            int minDiff = 4096;
            int targetVal = MotorEncoders[i]->getCalibration(0);

            // Loop through each of the 4 aligned positions
            for (int j = 0; j < 4; j++) {

                // Skip the starting position if in selective mode
                if (selectiveAlign && motorMoved[i] && j == startCalIndex[i]) {
                    continue;
                }

                int calVal = MotorEncoders[i]->getCalibration(j);
                int diff = abs(currentVal - calVal);
                if (diff > 2048) diff = 4096 - diff;  // Account for wraparound

                if (diff < minDiff) {
                    minDiff = diff;
                    targetVal = calVal;
                }
            }

            // Compute wraparound-safe difference
            int diff = abs(currentVal - targetVal);
            if (diff > 2048) diff = 4096 - diff;

            // Check if within tolerance
            if (diff > motorAlignmentTol) {
                aligned = false;

                // Move toward target
                if (currentVal > targetVal)
                    pos[i] += stepSize;
                else
                    pos[i] -= stepSize;
            }
        }

        // Apply new positions
        cubeMotors.moveTo(pos);

        // Check for timeout
        if (millis() - t_start > timeout) {
            cubeMotors.disableMotors();
            return 2;
        }
    }

    // Disable motors
    cubeMotors.disableMotors();

    // Reset stepper position tracking
    cubeMotors.resetMotorPos();

    return 0;   // Success
}

int CubeSystem::homeMotors() {
    // Homes all motors from unknown position (full homing procedure)
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    // Check if motors are calibrated
    if (!getMotorCalibration()) {
        return 1;
    }

    // Clear all tracking variables for fresh start
    for (int i = 0; i < 6; i++) {
        startCalIndex[i] = -9999;
        motorMoved[i] = false;
    }

    // Call internal alignment with full homing mode
    return alignMotorsInternal(false);
}

int CubeSystem::alignMotors() {
    // Re-aligns motors after a move (only aligns motors that moved)
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    // Check if motors are calibrated
    if (!getMotorCalibration()) {
        return 1;
    }

    // Determine starting calibration index for each motor
    for (int i = 0; i < numMotors; i++) {
        int cur = MotorEncoders[i]->scan();

        int bestIdx = 0;
        int bestDiff = 9999;

        for (int j = 0; j < 4; j++) {
            int cal = MotorEncoders[i]->getCalibration(j);
            int diff = abs(cur - cal);
            if (diff > 2048) diff = 4096 - diff;
            if (diff < bestDiff) {
                bestDiff = diff;
                bestIdx = j;
            }
        }

        startCalIndex[i] = bestIdx;
    }

    // Call internal alignment with selective mode
    return alignMotorsInternal(true);
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

    // Center cube in chamber
    botServoPartial();
    botServoRetract();

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
    delay(servoDelay);
    botServoRetract();
    delay(servoDelay);

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
    delay(servoDelay);
    ringRetract();
    botServoRetract();
    delay(servoDelay);

    // ------------ STEP 3 ------------
    // Scan remaining top/bottom faces

    // Order of faces being scanned in step 3
    const char topFaces[4][2] = {
        {'Y', 'O'}, // Yellow Left, Orange Back
        {'R', 'Y'}, // Red Left, Yellow Back
        {'W', 'R'}, // White Left, Red Back
        {'O', 'W'}  // Orange Left, White Back
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
            delay(servoDelay);
            executeMove("ROTZ");
            //topServoRetract();
            botServoRetract();
            delay(servoDelay);
        }
    }

    // Save calibrated values to EEPROM
    colorSensor1.saveCalibration();
    colorSensor2.saveCalibration();

    return 0;
}

void CubeSystem::topServoExtend() {
    topServo.extend();
    delay(servoDelay);
}

void CubeSystem::topServoRetract() {
    topServo.retract();
    delay(servoDelay);
}

void CubeSystem::topServoPartial()
{
    topServo.partial();
    delay(servoDelay);
}

void CubeSystem::botServoExtend() {
    botServo.extend();
    delay(servoDelay);
}

void CubeSystem::botServoRetract() {
    botServo.retract();
    delay(servoDelay);
}

void CubeSystem::botServoPartial()
{
    botServo.partial();
    delay(servoDelay);
}

void CubeSystem::toggleTopServo() {
    topServo.toggle();
    delay(servoDelay);
}

void CubeSystem::toggleBotServo() {
    botServo.toggle();
    delay(servoDelay);
}

void CubeSystem::toggleRing() {
    cubeMotors.ringToggle();
    delay(servoDelay);
}

void CubeSystem::ringExtend() {
    cubeMotors.ringMove(2);
}

void CubeSystem::ringPartial(){
    cubeMotors.ringMove(3);
}

void CubeSystem::ringMiddle() {
    cubeMotors.ringMove(1);
}

void CubeSystem::ringRetract() {
    cubeMotors.ringMove(0);
}   

int CubeSystem::executeMove(const String &move, bool moveVirtual, bool align) {
    // Outputs:
    //  0 - Ran successfully
    //  1X - Virtual move failed (X is error thrown by virtual move)
    //  2X - Alignment failed after retries
    //  3 - Move string is invalid

    // Pre-check alignment if alignment is requested
    if (align) {
        if (!checkAlignment()) {
            Serial.println("Motors misaligned before move - homing now");
            int homeResult = homeMotors();
            if (homeResult != 0) {
                Serial.println("ERROR: Pre-move homing failed");
                return 20 + homeResult;
            }
        }
    }

    const int MAX_RETRIES = 3;
    int retryCount = 0;

    while (retryCount < MAX_RETRIES) {
        // Store starting encoder positions
        int startPositions[6];
        for (int i = 0; i < numMotors; i++) {
            startPositions[i] = MotorEncoders[i]->scan();
        }

        // Reset motorMoved flags
        for (int i = 0; i < 6; i++) {
            motorMoved[i] = false;
        }

        // Mark motors that this move affects
        if (move == "U" || move == "U'" || move == "U2")
            motorMoved[0] = true;
        else if (move == "R" || move == "R'" || move == "R2")
            motorMoved[1] = true;
        else if (move == "F" || move == "F'" || move == "F2")
            motorMoved[2] = true;
        else if (move == "D" || move == "D'" || move == "D2")
            motorMoved[3] = true;
        else if (move == "L" || move == "L'" || move == "L2")
            motorMoved[4] = true;
        else if (move == "B" || move == "B'" || move == "B2")
            motorMoved[5] = true;
        else if (move == "ROTX") {
            motorMoved[4] = true;   // L
            motorMoved[1] = true;   // R (inverse)
        }
        else if (move == "ROTZ") {
            motorMoved[0] = true;   // U
            motorMoved[3] = true;   // D (inverse)
        }
        else if (move == "ALL") {
            for (int i = 0; i < 6; i++)
                motorMoved[i] = true;
        }
        else {
            return 3; // Invalid move
        }

        // Execute the physical move
        cubeMotors.executeMove(move);

        // Check if alignment is needed
        if (align) {
            // Determine starting calibration indices
            for (int i = 0; i < numMotors; i++) {
                if (!motorMoved[i]) continue;

                int bestIdx = 0;
                int bestDiff = 9999;

                for (int j = 0; j < 4; j++) {
                    int cal = MotorEncoders[i]->getCalibration(j);
                    int diff = abs(startPositions[i] - cal);
                    if (diff > 2048) diff = 4096 - diff;
                    if (diff < bestDiff) {
                        bestDiff = diff;
                        bestIdx = j;
                    }
                }
                startCalIndex[i] = bestIdx;
            }

            // Try to align
            int alignResult = alignMotorsInternal(true);
            
            if (alignResult == 0) {
                // Alignment successful!
                break;
            }
            
            // Alignment failed - likely jammed
            Serial.print("Alignment failed, attempt ");
            Serial.print(retryCount + 1);
            Serial.println(" - backing out and re-homing");

            // Step 1: Back out only the moved motor(s) to relieve jam
            bool backoutSuccess = backoutMove(startPositions);
            
            if (!backoutSuccess) {
                Serial.println("ERROR: Failed to back out of jammed position");
                return 20 + alignResult;
            }

            Serial.println("Backout complete - now homing all motors");

            // Step 2: Clear motorMoved flags so homeMotors aligns ALL motors
            for (int i = 0; i < 6; i++) {
                motorMoved[i] = false;
            }

            // Step 3: Re-home all motors from their current positions
            int homeResult = homeMotors();
            if (homeResult != 0) {
                Serial.println("ERROR: Re-homing failed");
                return 20 + homeResult;
            }

            retryCount++;
            
            if (retryCount >= MAX_RETRIES) {
                Serial.println("ERROR: Max retries exceeded");
                return 20 + alignResult;
            }
            
            // Loop will retry the move
            delay(100);
        }
        else {
            // No alignment requested, just do the move once
            break;
        }
    }

    // Execute virtual move if requested
    if (moveVirtual) {
        int result = virtualCube.executeMove(move);
        if (result) return 10 + result;
    }

    return 0;   // Success
}

bool CubeSystem::backoutMove(int targetPositions[6]) {
    // Try to return motors to their starting positions
    // Returns true if successful, false if failed
    
    const unsigned long BACKOUT_TIMEOUT = 2000;
    unsigned long startTime = millis();
    
    long pos[6];
    for (int i = 0; i < 6; i++) {
        pos[i] = cubeMotors.getPos(i);
    }
    
    cubeMotors.enableMotors();
    
    bool aligned = false;
    while (!aligned && (millis() - startTime < BACKOUT_TIMEOUT)) {
        aligned = true;
        
        for (int i = 0; i < numMotors; i++) {
            if (!motorMoved[i]) continue;
            
            int currentPos = MotorEncoders[i]->scan();
            int diff = abs(currentPos - targetPositions[i]);
            if (diff > 2048) diff = 4096 - diff;
            
            if (diff > motorAlignmentTol) {
                aligned = false;
                
                // Determine direction to move back
                int rawDiff = currentPos - targetPositions[i];
                if (rawDiff > 2048) rawDiff -= 4096;
                if (rawDiff < -2048) rawDiff += 4096;
                
                if (rawDiff > 0)
                    pos[i] -= stepSize;
                else
                    pos[i] += stepSize;
            }
        }
        
        cubeMotors.moveTo(pos);
    }
    
    cubeMotors.disableMotors();
    
    if (!aligned) {
        Serial.println("Backout timeout - could not return to start");
        return false;
    }
    
    // Reset position tracking
    cubeMotors.resetMotorPos();
    return true;
}

bool CubeSystem::checkAlignment() {
    // Returns true if all motors are aligned
    // Returns false if one or more motors is misaligned
    
    for (int i = 0; i < numMotors; i++) {
        int currentVal = MotorEncoders[i]->scan();
        bool aligned = false;

        for (int j = 0; j < 4; j++) {
            int calVal = MotorEncoders[i]->getCalibration(j);
            int diff = abs(currentVal - calVal);
            if (diff > 2048) diff = 4096 - diff;  // Account for wraparound
            
            if (diff <= motorAlignmentTol) {
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

void CubeSystem::displaySetMessage(const char* msg) {
    if (displayInitialized) {
        cubeDisplay.setMessage(msg);
    }
}

void CubeSystem::displaySetStatus(const char* msg) {
    if (displayInitialized) {
        cubeDisplay.setStatus(msg);
    }
}

void CubeSystem::displayClearStatus() {
    if (displayInitialized) {
        cubeDisplay.clearStatus();
    }
}

void CubeSystem::displayUpdate() {
    if (displayInitialized) {
        cubeDisplay.update();
    }
}

void CubeSystem::displayWaitForSelect(const char* msg) {
    if (displayInitialized) {
        cubeDisplay.waitForSelect(msg);
    }
}

bool CubeSystem::displayReady() {
    return displayInitialized;
}

void CubeSystem::clearSolution(){
    // Resets the class solution string
    solutionLength = 0;
    for (int i = 0; i < maxMoves; ++i) {
        solveMoves[i] = "";
    }
}

int CubeSystem::solveVirtual(){
    // Updates the solution for the current state of the cube
    // Outputs:
    //  0 - Success
    //  1X - Virtual Cube Solve Error

    // Clear current solution
    clearSolution();

    // Solve Cube
    int solveOutput = virtualCube.solveCube(solveMoves, maxMoves);
    if(solveOutput < 0){
        clearSolution();
        return 10 - solveOutput;
    }

    solutionLength = solveOutput;

    return 0;
}

int CubeSystem::executeSolve(){
    // If the cube has been solved for, executes moves to solve it
    // Outputs:
    //  0 - Success
    //  1 - Cube is not ready
    //  2 - No solution
    //  1XX - 

    // Ensure the virtual cube is ready
    if (!virtualCube.isReady()) {
        return 1; // Cube not ready
    }

    // Check for empty solution
    if (solutionLength == 0 || solveMoves[0].length() == 0) {
        return 2; // No moves found
    }

    // Execute each move
    for (int i = 0; i < solutionLength; i++) {
        int e = 0;
        e = executeMove(solveMoves[i], 1, 1);

        if (e) return 100+e;
    }
    
    return 0;
}
