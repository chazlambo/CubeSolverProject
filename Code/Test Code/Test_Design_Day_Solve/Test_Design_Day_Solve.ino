#include "CubeSystem.h"

// Create Cube System
CubeSystem Cube;

// Result variables
int scanResult = -1;
int solveResult = -1;
int execResult = -1;

void setup() {
    Serial.begin(115200);
    Serial.println("==== Cube Solver UI Startup ====");
    
      // Initialize cube hardware
    Cube.begin();

    Cube.displaySetMessage("Initializing Cube...");
    Cube.displayUpdate();

    Cube.displaySetMessage("Ready.");
    Cube.displayClearStatus();
}

void loop() {
    // Update display
    Cube.displayUpdate();

    // -------------------- STEP 1: SCAN --------------------
    Cube.displayWaitForSelect("Insert scrambled cube, then:");

    Cube.displaySetMessage("Scanning cube...");
    Cube.displayUpdate();
    delay(5);

    Serial.println("Starting Scan...");
    scanResult = Cube.scanCube();

    if (scanResult != 0) {
        Cube.displaySetMessage("SCAN ERROR!");
        char buf[40];
        sprintf(buf, "Err %d", scanResult);
        Cube.displaySetStatus(buf);
        Serial.print("Scan failed - Error: ");
        Serial.println(scanResult);
        while (true) { 
            Cube.displayUpdate(); 
            delay(20); 
        }
    }

    Cube.displaySetMessage("Scan complete.");
    Serial.println("Scan successful.");

    // -------------------- STEP 2: SOLVE VIRTUALLY --------------------
    Cube.displaySetMessage("Solving cube...");
    Cube.displayUpdate();
    delay(5);

    Serial.println("Computing virtual solution...");
    solveResult = Cube.solveVirtual();

    if (solveResult != 0) {
        Cube.displaySetMessage("SOLVE ERROR!");
        char buf[40];
        sprintf(buf, "Err %d", solveResult);
        Cube.displaySetStatus(buf);
        Serial.print("Virtual solve failed: ");
        Serial.println(solveResult);
        while (1) { 
            Cube.displayUpdate(); 
            delay(20); 
        }
    }

    Cube.displaySetMessage("Solution found!");
    Cube.displaySetStatus("Press SELECT to load cube");
    Serial.println("Virtual solution ready.");

    // -------------------- STEP 3: LOAD CUBE --------------------
    Cube.displayWaitForSelect("Solution found! To load cube:");

    Cube.displaySetMessage("Loading cube...");
    Cube.displayClearStatus();
    Serial.println("Loading cube...");

    Cube.botServoExtend();
    Cube.ringExtend();
    Cube.topServoExtend();

    Cube.displaySetMessage("Cube loaded.");
    Cube.displaySetStatus("Cube ready to solve.\nPress SELECT to begin.");
    Serial.println("Cube loaded and ready.");

    // -------------------- STEP 4: EXECUTE SOLVE --------------------
    Cube.displayWaitForSelect("Cube ready to solve.\n");

    Cube.displaySetMessage("Solving...");
    Cube.displayClearStatus();
    Cube.displayUpdate();
    delay(5);

    Serial.println("Executing solve...");

    // Time the physical solve
    unsigned long t0 = millis();
    execResult = Cube.executeSolve();
    unsigned long t1 = millis();

    if (execResult != 0) {
        Cube.displaySetMessage("EXEC ERROR!");
        char buf[40];
        sprintf(buf, "Err %d", execResult);
        Cube.displaySetStatus(buf);
        Serial.print("Execute solve failed - Error: ");
        Serial.println(execResult);
        while (true) { 
            Cube.displayUpdate(); 
            delay(20); 
        }
    }

    float elapsedSec = (t1 - t0) / 1000.0f;
    int moves = Cube.solutionLength;

    char resultBuf[80];
    sprintf(resultBuf, "Solve complete!\n%d moves in %.2f sec", moves, elapsedSec);
    Cube.displaySetMessage(resultBuf);

    Serial.println("Solve complete!");
    Serial.print("Moves: ");
    Serial.println(moves);
    Serial.print("Time: ");
    Serial.print(elapsedSec, 2);
    Serial.println(" sec");

    Cube.ringRetract();
    Cube.topServoRetract();
    Cube.botServoRetract();

    // Done – stay on result screen
    while (1) { 
        Cube.displayUpdate(); 
        delay(30); 
    }
}