// CubeSolver.h
#ifndef cube_main_h
#define cube_main_h

#include <Arduino.h>


// TODO: Change to .h when converting to actual library!
//#include "virtual_cube.h"
#include "cube_motor.cpp"
#include "CubeServo.cpp"
#include "MotorPot.cpp"
#include "ColorSensor.cpp"
#include "RotaryEncoder.cpp"

// Pin Definitions
const int POWPIN = 23;       

// Serial Variables
const int baudRate = 11520;

// Setup Functions
void mainSetup();

// General Functions
bool powerCheck();

// Motor Initialization
int motorHomeState = -1;
int homeMotors();

#endif // CubeSolver_h  