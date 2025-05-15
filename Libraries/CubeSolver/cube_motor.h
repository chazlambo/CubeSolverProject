// cube_servo_h
#ifndef cube_motor_h
#define cube_motor_h

#include <Arduino.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <EEPROM.h>

// Pin Definitions
const int ENPIN = 0;
const int DIR1 = 1;
const int STEP1 = 2;
const int DIR2 = 3;
const int STEP2 = 4;
const int DIR3 = 5;
const int STEP3 = 6;
const int DIR4 = 25;
const int STEP4 = 26;
const int DIR5 = 27;
const int STEP5 = 28;
const int DIR6 = 29;
const int STEP6 = 30;
const int DIR7 = 31;
const int STEP7 = 32;

// Initialize Stepper Motor Objects
AccelStepper frontStepper(1, STEP3, DIR3);  // 3
AccelStepper backStepper(1, STEP5, DIR5);   // 5
AccelStepper leftStepper(1, STEP6, DIR6);   // 6
AccelStepper rightStepper(1, STEP2, DIR2);  // 2
AccelStepper upStepper(1, STEP1, DIR1);     // 1
AccelStepper downStepper(1, STEP4, DIR4);   // 4
AccelStepper ringStepper(1, STEP7, DIR7);
MultiStepper multiStep; // Top, Right, Front, Down, Left, Back

// Initialize Position Variable
long int pos[6] = {0, 0, 0, 0, 0, 0};
int turnStep = 100; // Quarter Turn

// Initialize Stepper Speed Variable
int stepSpeed = 1000;       // Set to 1000?
int defaultStepDelay = 100; // Delay to keep motor powered after moving
int rotStepDelay = 500;     // Delay to keep motor powered after rotating cube

// Motor Enable
bool motEnableState = 0;

// Initialize Ring Stepper Variables
int ringPos = 0;
int ringStepSpeed = 800;
int ringStepAccel = 400;
int ringRetPos = 0;
int ringHalfPos = 400;
int ringExtPos = 800;
int ringState = 0;

// Ring State EEPROM Variables
//motorCalStartAddress + 6 * sizeof(int) = 1 + 12 = 13 --> Use 15 for space
const int ringStateEEPROMAddress = 15;

void stepperSetup();                                     // Setup function to run

void initRingStepper(AccelStepper &ringStep);                           // Function to initialize ring stepper motor with presets
void enableMotors();
void disableMotors();
void initStepper(MultiStepper &multiStepper, AccelStepper &newStepper); // Function to initialize cube stepper motors with presets

void homeRingStepper(AccelStepper &ringStep);
void ringMove(int state);                       // 0 = Retracted, 1 = Halfway, 2 = Extended
void ringToggle();

void executeMove(String move); // E.G. "U", "U'", "U2"

#endif // cube_motor_h