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
    void executeMove(String move);

private:
    // Private helper methods
    void initStepper(MultiStepper &multiStepper, AccelStepper &newStepper);
    void initRingStepper(AccelStepper &ringStep);

    // Stepper Objects
    AccelStepper upStepper;
    AccelStepper rightStepper;
    AccelStepper frontStepper;
    AccelStepper downStepper;
    AccelStepper backStepper;
    AccelStepper leftStepper;
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
    int ringHalfPos = 450;
    int ringExtPos = 800;
    int ringState = 0;
};

#endif
