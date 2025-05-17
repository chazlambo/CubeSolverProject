#ifndef CubeSystem_h
#define CubeSystem_h

#include <Arduino.h>
#include "CubeHardwareConfig.h"

class CubeSystem {
public:
    CubeSystem();
    void begin();                // Initializes all hardware
    
    // Motor Calibration Functions
    bool getMotorCalibration();  // Checks EEPROM for calibration flag
    int homeMotors();            // Homes all 6 stepper motors

    // Move Functions
    void executeMove(const String& move);

    // Cube Loading Functions
    void toggleTopServo();
    void toggleBotServo();
    void toggleRing();
    void extendRing();
    void retractRing();


private:
    bool powerCheck();           // Reads POWPIN

private:
    // Motor Home Variables
    int threshold = 1;
    int stepSize = 1;
    unsigned long timeout = 5000;
    int motorHomeState;
    
    
};

#endif
