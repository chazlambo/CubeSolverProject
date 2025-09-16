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

private:
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
    int turnStep = 100;

    int stepSpeed = 1000;
    int defaultStepDelay = 100;
    int rotStepDelay = 500;

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
