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
    bool checkAlignment();

    // Cube Loading Functions
    // Top Servo Functions
    void topServoExtend();
    void topServoRetract();
    void topServoPartialRetract();
    void toggleTopServo();

    // Bot Servo Functions
    void botServoExtend();
    void botServoRetract();
    void botServoPartialRetract();
    void toggleBotServo();

    // Ring Functions
    void toggleRing();
    void ringExtend();
    void ringMiddle();
    void ringRetract();

private:
    bool powerCheck();           // Reads POWPIN

public:
    int numMotors = 6;

private:
    // Virtual Cube
    VirtualCube virtualCube;

    // Motor Home Variables
    int threshold = 1;
    int stepSize = 1;
    unsigned long timeout = 5000;
    int motorHomeState;
    int motorAlignmentTol = 1;
    
};

#endif
