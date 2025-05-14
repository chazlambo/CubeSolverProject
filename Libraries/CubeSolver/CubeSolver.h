// CubeSolver.h
#ifndef cube_main_h
#define cube_main_h

#include <Arduino.h>

// TODO: Remove when converting to real library
#include "adcPot.cpp"

// TODO: Change to .h when converting to actual library!
#include "cube_basic.cpp"
//#include "virtual_cube.h"
#include "cube_motor.cpp"
#include "cube_servo.cpp"
#include "cube_sensors.cpp"

// TODO: Remove later
bool skipMotorInt = 0;  // Boolean to skip motor initialization

// Pin Definitions
const int POWPIN = 23;

// Serial Variables
const int baudRate = 11520;

// Setup Functions
void mainSetup();

// General Functions
bool powerCheck();

#endif // CubeSolver_h  