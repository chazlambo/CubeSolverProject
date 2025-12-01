#ifndef CubeSystem_h
#define CubeSystem_h

#include <Arduino.h>
#include "CubeHardwareConfig.h"

class CubeSystem {
public:
    CubeSystem();

    // Main Functions
    void begin();                   // Initializes all hardware
    int scanCube();
    
    // Motor Calibration Functions
    bool getMotorCalibration();     // Checks EEPROM for calibration flag
    int calibrateMotorRotations();  // Sets all motor calibration values based on current position
    int homeMotors();               // Homes all 6 stepper motors

    // Color Sensor Calibration Functions
    bool getColorCalibration();     // Checks EEPROM for calibration flag
    int calibrateColorSensors();    // Calibrates sensors using solved cube

    // Move Functions
    int executeMove(const String& move, bool moveVirtual = false, bool align = false);
    int alignMotors();              // Re-aligns motors after a move (faster, selective)
    bool checkAlignment();          // Check if motors are currently aligned
    int alignMotorsInternal(bool selectiveAlign);

    // Main Solving Functions
    void clearSolution();
    int solveVirtual();
    int executeSolve();

    // Cube Loading Functions
    // Top Servo Functions
    void topServoExtend();
    void topServoRetract();
    void topServoPartial();
    void toggleTopServo();

    // Bot Servo Functions
    void botServoExtend();
    void botServoRetract();
    void botServoPartial();
    void toggleBotServo();

    // Ring Functions
    void toggleRing();
    void ringExtend();
    void ringPartial();
    void ringMiddle();
    void ringRetract();

    // Display Functions
    void displayBegin(uint32_t spiSpeed = 10000000);
    void displaySetMessage(const char* msg);
    void displaySetStatus(const char* msg);
    void displayClearStatus();
    void displayUpdate();
    void displayWaitForSelect(const char* msg);
    bool displayReady();

private:
    bool powerCheck();

public:
    int numMotors = 6;
    bool displayInitialized = false;

public:
    // Virtual Cube
    VirtualCube virtualCube;

    // Motor Home Variables
    int threshold = 20;
    int stepSize = 1;
    int stableReq = 1;
    unsigned long alignmentTimeout = 1000;
    int motorHomeState;
    int motorAlignmentTol = 10;
    int startCalIndex[6] = {0,0,0,0,0,0};
    bool motorMoved[6] = {false, false, false, false, false, false};

    // Cube Solve Variables
    int servoDelay = 200;

    // Solution String
    int solutionLength = 0;
    static const int maxMoves = 50;
    String solveMoves[maxMoves];
    
};

#endif


