// CubeServo.h
#ifndef CubeServo_h
#define CubeServo_h

#include <Arduino.h>
#include <PWMServo.h>
#include <EEPROM.h>
#include "CubePump.h"

class CubeServo {
public:
    

public:
    CubeServo(int pin, int eepromAddr, unsigned int retPos, unsigned int extPos, int sweepDelay = 20);

    void begin();      // Call in setup
    void extend();     // Move to extended position
    void partial();  // TO DO IMPLEMENT LATER
    void retract();    // Move to retracted position
    void toggle();     // Toggle between extended/retracted
    bool isExtended(); // Returns true if extended

private:
    PWMServo servo;
    int pin;                    // Servo pin
    int eepromAddr;             // Address where we store the state

    unsigned int currentPos;    // Position the servo is currently set to
    unsigned int retPos;        // Retracted position value (0-270)
    unsigned int extPos;        // Extended position value (0-270)
    int extState;               // 0 = retracted, 1 = extended, 2 = partially retracted, -1 = unknown
    int sweepDelay;             // Time in ms to delay between each sweep step

    unsigned int partialTarget() const;  // 3/4 of the way from retracted to extended

    // Returns false if the sweep was cut short by an abort. Callers MUST NOT
    // record the target position or a settled state when it returns false —
    // the horn did not get there.
    bool sweepTo(unsigned int newPos);
    void loadStateFromEEPROM();
    void updateEEPROM();
};

#endif // CubeServo_h
