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

    // --- position tuning -----------------------------------------------------
    // The retracted and extended positions are handed in at construction and
    // owned here from then on. The globals they came from (topExtPos and
    // friends) are read exactly once, so writing those later changes nothing —
    // a settings screen that tried would appear to work and move no servo.
    // These are the way in.
    unsigned int retracted()      const { return retPos; }
    unsigned int extended()       const { return extPos; }
    int          sweepStepDelay() const { return sweepDelay; }

    void setRetracted(unsigned int pos);
    void setExtended(unsigned int pos);
    void setSweepStepDelay(int ms);

    // Drive the horn straight to a raw position with no sweep.
    //
    // For setting a position BY EYE, which is the only way an endpoint gets
    // set: the point is watching the horn while the number changes, and a sweep
    // at 15 ms a step cannot follow a wheel. Callers MUST step it — each call
    // jumps instantly, so a large change is a slam, not a move.
    //
    // Leaves the coarse state unknown, because after this the horn is at
    // neither endpoint. Does NOT write EEPROM; call persist() once the wheel
    // has stopped rather than burning a write per detent.
    void previewRaw(unsigned int pos);

    // Commit the tracked position to EEPROM. Worth being deliberate about:
    // begin() trusts the stored position to decide how far the first sweep has
    // to travel, so leaving it stale after a preview is what arms a full-travel
    // slam on the next power-up.
    void persist() { updateEEPROM(); }

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
