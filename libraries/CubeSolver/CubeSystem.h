#ifndef CubeSystem_h
#define CubeSystem_h

#include <Arduino.h>
#include "CubeHardwareConfig.h"

class CubeSystem {
public:
    CubeSystem();
    void begin();                // Initializes all hardware
    
    // Motor Calibration Functions
    bool getMotorCalibration();     // Checks EEPROM for calibration flag
    int calibrateMotorRotations();  // Sets all motor calibration values based on current position
    int homeMotors();               // Homes all 6 stepper motors

    // Color Sensor Calibration Functions
    bool getColorCalibration();     // Checks EEPROM for calibration flag
    int calibrateColorSensors();    // Calibrates sensors using solved cube

    // Move Functions
    void executeMove(const String& move);

    // Cube Loading Functions
    void topServoExtend();
    void topServoRetract();
    void botServoExtend();
    void botServoRetract();
    void toggleTopServo();
    void toggleBotServo();
    void toggleRing();
    void ringExtend();
    void ringMiddle();
    void ringRetract();

private:
    bool powerCheck();           // Reads POWPIN

public:
    int numMotors = 6;

private:
    // Motor Home Variables
    int threshold = 1;
    int stepSize = 1;
    unsigned long timeout = 5000;
    int motorHomeState;
    
    
};

#endif
