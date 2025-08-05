#ifndef CubeHardwareConfig_h
#define CubeHardwareConfig_h

// Library Includes
#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_TCA9548A.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <PWMServo.h>
#include <Adafruit_seesaw.h>
#include <string.h>

// Custom Includes
#include "CubeMotors.h"
#include "CubeServo.h"
#include "MotorEncoder.h"
#include "ColorSensor.h"
#include "RotaryEncoder.h"
#include "VirtualCube.h"

//================ EEPROM Setup ================
void initializeEEPROMLayout(int startAddress = 0);

// ================ Serial Communication Setup ================
extern const int baudRate;

// ================ Power Setup ================
extern const int POWPIN;    

// ================ I2C Multiplexer Setup ================
extern Adafruit_TCA9548A encoderMux;
extern Adafruit_TCA9548A color1Mux1;
extern Adafruit_TCA9548A color1Mux2;
extern Adafruit_TCA9548A color2Mux1;
extern Adafruit_TCA9548A color2Mux2;


// ================ Motor Encoder Setup ================

// Initialize EEPROM Addresses
extern int motorCalFlagAddress;
extern int motorCalAddresses[7][4];

// Create Motor Encoder Objects
extern MotorEncoder motU, motR, motF, motD, motL, motB, motRing;
extern MotorEncoder* MotorEncoders[7];

// ================ Motor Setup ================

// Pin Definitions
extern const int ENPIN;
extern const int DIR1, STEP1, DIR2, STEP2, DIR3, STEP3;
extern const int DIR4, STEP4, DIR5, STEP5, DIR6, STEP6, DIR7, STEP7;
extern int STEPPINS[7];
extern int DIRPINS[7];

// Ring State EEPROM Variables
extern int ringStateEEPROMAddress;

// Motor Object
extern CubeMotors cubeMotors;

// ================ Servo Setup ================
// Servo Pins
extern const int TOPSERVO, BOTSERVO;

// Position & Speed Variables
extern unsigned int topExtPos, topRetPos, botExtPos, botRetPos;
extern int topSweepDelay, botSweepDelay;

// EEPROM Variables
extern int topServoEEPROMAddress, botServoEEPROMAddress;

// Servo Objects
extern CubeServo topServo, botServo;

// ================ Color Sensor Setup ================

// Pins
extern int ledPins1[3], ledPins2[3];
extern int sensorPins1[9], sensorPins2[9];

// Initialize EEPROM
extern int colorSensor1EEPROMFlag, colorSensor2EEPROMFlag;
extern int colorSensor1EEPROMAddresses[9][7][4], colorSensor2EEPROMAddresses[9][7][4];

// Create ColorSensor Objects
extern ColorSensor colorSensor1, colorSensor2;

// ================ Rotary Encoder Setup ================
extern RotaryEncoder menuEncoder;

#endif