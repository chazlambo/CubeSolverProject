#ifndef CUBEMOTORS_H
#define CUBEMOTORS_H

#include <Arduino.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <EEPROM.h>


class CubeMotors {
public:
    // Constructor
    CubeMotors(int enpin, int step_pins[7], int dir_pins[7], int ringStateEEPROMAddress);

    // Cube Moves
    enum CubeMove {
        MOVE_U, MOVE_Up, MOVE_U2,
        MOVE_R, MOVE_Rp, MOVE_R2,
        MOVE_F, MOVE_Fp, MOVE_F2,
        MOVE_D, MOVE_Dp, MOVE_D2,
        MOVE_L, MOVE_Lp, MOVE_L2,
        MOVE_B, MOVE_Bp, MOVE_B2,
        MOVE_ROT_X, MOVE_ROT_Z, MOVE_ALL,
        MOVE_INVALID
    };

    // Public methods
    void begin();
    void enableMotors();
    void disableMotors();
    void homeRingStepper(AccelStepper &ringStep);
    void ringMove(int state);
    void ringToggle();
    long getPos(int posIdx);
    void moveTo(long newPos[6]);
    void resetMotorPos();
    void executeMove(String moveString);

    // --- motion tuning -------------------------------------------------------
    // Read at the point of use rather than latched at begin(), so changing one
    // takes effect on the very next move. That is what makes tuning them from a
    // settings screen worth doing at all — you turn the wheel, then turn a face,
    // and see the difference immediately.
    //
    // Every setter clamps. These drive real steppers, and a zero speed is a
    // machine that hangs mid-move with the grippers closed.
    int  getTurnStep()     const { return turnStep; }
    int  getStepSpeed()    const { return stepSpeed; }
    int  getStepDelay()    const { return defaultStepDelay; }
    int  getRotStepDelay() const { return rotStepDelay; }
    int  getRingSpeed()    const { return ringStepSpeed; }
    int  getRingAccel()    const { return ringStepAccel; }

    // Steps per quarter turn is READ-ONLY on purpose, and there is no setter to
    // add later without thinking about this: it is a property of the gearing,
    // not a preference. A wrong value does not make solves worse, it makes them
    // impossible — every face ends up mis-indexed and the cube jams. It belongs
    // in the source next to the pin assignments, changed once per machine by
    // someone holding a calculator, not on a screen next to the ring speed.
    void setStepSpeed(int v)    { stepSpeed        = clampTune(v, 50, 5000); }
    void setStepDelay(int v)    { defaultStepDelay = clampTune(v, 0,  500);  }
    void setRotStepDelay(int v) { rotStepDelay     = clampTune(v, 0,  500);  }
    void setRingSpeed(int v)    { ringStepSpeed    = clampTune(v, 50, 5000); }
    void setRingAccel(int v)    { ringStepAccel    = clampTune(v, 50, 5000); }

    // Ring positions, in steps from the home switch. Unlike the speeds these
    // are mechanical: the ring has to clear the cube at retract and grip it at
    // extend, and getting one wrong drives the carriage into a hard stop.
    // 2000 is well past the travel and exists only to stop a runaway, not to
    // describe the machine — the useful range is far narrower and is what the
    // tuning screen offers.
    int  getRingRetPos()     const { return ringRetPos;     }
    int  getRingPartialPos() const { return ringPartialPos; }
    int  getRingHalfPos()    const { return ringHalfPos;    }
    int  getRingExtPos()     const { return ringExtPos;     }

    void setRingRetPos(int v)     { ringRetPos     = clampTune(v, 0, 2000); }
    void setRingPartialPos(int v) { ringPartialPos = clampTune(v, 0, 2000); }
    void setRingHalfPos(int v)    { ringHalfPos    = clampTune(v, 0, 2000); }
    void setRingExtPos(int v)     { ringExtPos     = clampTune(v, 0, 2000); }

private:
    static int clampTune(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Private helper methods
    void initStepper(MultiStepper &multiStepper, AccelStepper &newStepper);
    void initRingStepper(AccelStepper &ringStep);

    CubeMove parseMove(const String& move);

private:
    // Stepper Objects
    AccelStepper upStepper;
    AccelStepper rightStepper;
    AccelStepper frontStepper;
    AccelStepper downStepper;
    AccelStepper leftStepper;
    AccelStepper backStepper;
    AccelStepper ringStepper;
    MultiStepper multiStep;

    // Motor Pins
    int EN_PIN;
    int STEP_PINS[7];
    int DIR_PINS[7];
    int ringStateEEPROMAddress;

    // Position and State Variables
    long pos[6] = {0, 0, 0, 0, 0, 0};
    int expectedCalIndex[6] = {0,0,0,0,0,0};
    int turnStep = 100;

    int stepSpeed = 1000;
    int defaultStepDelay = 50; // 100?
    int rotStepDelay = 60;

    bool motEnableState = false;

    int ringPos = 0;
    int ringStepSpeed = 800;
    int ringStepAccel = 400;
    int ringRetPos = 0;
    int ringPartialPos = 200;
    int ringHalfPos = 450;
    int ringExtPos = 800;
    int ringState = 0;
    
};

#endif
