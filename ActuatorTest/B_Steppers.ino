#include <AccelStepper.h>

// Pin Definitions
const int ENPIN = 0;
const int DIR1 = 1;
const int STEP1 = 2;
const int DIR2 = 3;
const int STEP2 = 4;
const int DIR3 = 5;
const int STEP3 = 6;
const int DIR4 = 7;
const int STEP4 = 8;
const int DIR5 = 9;
const int STEP5 = 10;
const int DIR6 = 11;
const int STEP6 = 12;
const int DIR7 = 28;
const int STEP7 = 29;

// Stepper Definitions
AccelStepper frontStepper(1, STEP1, DIR1);
AccelStepper backStepper(1, STEP2, DIR2);
AccelStepper leftStepper(1, STEP3, DIR3);
AccelStepper rightStepper(1, STEP4, DIR4);
AccelStepper upStepper(1, STEP5, DIR5);
AccelStepper downStepper(1, STEP6, DIR6);
AccelStepper ringStepper(1, STEP7, DIR7);

// Cube Stepper Variables
int cubeStepSpeed = 1600;
int cubeStepAccel = 3200;
int quarterTurn = 100;  // Steps
int halfTurn = 200;     // Steps
int fullTurn = 400;     // Steps

// Ring Stepper Variables
int ringStepSpeed = 800;
int ringStepAccel = 400;
int ringRetPos = 0;
int ringExtPos = 400;
int ringMovePos = 0;
bool ringExtended = 0;

// Stepper Position Variables
int frontPos = 0;
int backPos = 0;
int leftPos = 0;
int rightPos = 0;
int upPos = 0;
int downPos = 0;
int ringPos = 0;

void assignStepper(AccelStepper &newStepper, int stepSpeed, int stepAccel) {
  newStepper.setMaxSpeed(stepSpeed);
  newStepper.setAcceleration(stepAccel);
}

void initRingStepper() {
  // Initialize Ring Position
  ringStepper.runToNewPosition(ringExtPos);
  ringStepper.runToNewPosition(ringRetPos);
  ringExtended = 0;
}

void stepperSetup() {
  pinMode(ENPIN, OUTPUT);
  digitalWrite(ENPIN, HIGH);

  // Initialize Stepper Speeds and Accelerations
  assignStepper(frontStepper, cubeStepSpeed, cubeStepAccel);
  assignStepper(backStepper, cubeStepSpeed, cubeStepAccel);
  assignStepper(leftStepper, cubeStepSpeed, cubeStepAccel);
  assignStepper(rightStepper, cubeStepSpeed, cubeStepAccel);
  assignStepper(upStepper, cubeStepSpeed, cubeStepAccel);
  assignStepper(downStepper, cubeStepSpeed, cubeStepAccel);
  assignStepper(ringStepper, ringStepSpeed, ringStepAccel);

  // Initialize Ring Stepper
  digitalWrite(ENPIN, LOW);
  initRingStepper();
  digitalWrite(ENPIN, HIGH);
}


void ringRetract() {
  digitalWrite(ENPIN, LOW); // Enable motors
  if(!ringExtended){        // If ring is already retracted
    return;                 // Exit function
  }   
  ringStepper.runToNewPosition(ringRetPos); // Retract Ring
  ringExtended = 0;                         // Set ring state
  digitalWrite(ENPIN, HIGH);                // Disable motors
}

void ringExtend() {
  digitalWrite(ENPIN, LOW); // Enable motors
  if(ringExtended) {        // If ring is already extended
    return;                 // Exit Function
  }
  ringStepper.runToNewPosition(ringExtPos); // Extend Ring
  ringExtended = 1;                         // Set ring state
  digitalWrite(ENPIN, HIGH);                // Disable motors
}

void stepper90(AccelStepper &myStepper, int &currentPos, bool turnDir) {
  // Turns a stepper motor 90 degrees in a specified direction
  // If turnDir = 1, motor will move clockwise
  digitalWrite(ENPIN, LOW);
  int newPos = currentPos + quarterTurn*pow(-1,!turnDir);  // Calculates new positon based on old position and direction
  myStepper.runToNewPosition(newPos);
  currentPos = newPos;
  digitalWrite(ENPIN, HIGH);
}

void ringToggle() {
  if(ringExtended) {
    ringRetract();
  }
  else {
    ringExtend();
  }
}

void F90(bool dir) {
  stepper90(frontStepper, frontPos, dir);
}

void B90(bool dir) {
  stepper90(backStepper, backPos, dir);
}

void L90(bool dir) {
  stepper90(leftStepper, leftPos, dir);
}

void R90(bool dir) {
  stepper90(rightStepper, rightPos, dir);
}

void U90(bool dir) {
  stepper90(upStepper, upPos, dir);
}

void D90(bool dir) {
  stepper90(downStepper, downPos, dir);
}
