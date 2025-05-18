#ifndef CubeHardwareConfig_h
#define CubeHardwareConfig_h

// Library Includes
#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_PCF8591.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <PWMServo.h>
#include <Adafruit_seesaw.h>
#include <string.h>

// Custom Includes
#include "CubeMotors.h"
#include "CubeServo.h"
#include "MotorPot.h"
#include "ColorSensor.h"
#include "RotaryEncoder.h"
#include "VirtualCube.h"

//================ EEPROM Setup ================
void initializeEEPROMLayout(int startAddress = 0);

// ================ Serial Communication Setup ================
extern const int baudRate;

// ================ Power Setup ================
extern const int POWPIN;    

// ================ ADC Module Setup ================
extern const int ADC_ADDRESS[6];
extern Adafruit_PCF8591 ADC1, ADC2, ADC3, ADC4, ADC5, ADC6;
extern Adafruit_PCF8591* ADC[6];

// ================ Motor Potentiometer Setup ================
extern const int numMotors;

// Pins
extern const int potADCPin[6];

// Initialize EEPROM Addresses
extern int motorCalFlagAddress;
extern int motorCalStartAddress;
extern int motorCalAddresses[6][4];

// Create MotorPot Objects
extern MotorPot motU, motR, motF, motD, motL, motB;
extern MotorPot* MotorPots[6];

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

// ADC Pointer Arrays
extern Adafruit_PCF8591* adcPtrs1[9];
extern Adafruit_PCF8591* adcPtrs2[9];

// Pins
extern int ledPins1[3], ledPins2[3];
extern int sensorPins1[9], sensorPins2[9];

// Initialize EEPROM
extern int colorSensor1EEPROMAddress, colorSensor2EEPROMAddress;

// Create ColorSensor Objects
extern ColorSensor colorSensor1, colorSensor2;

// ================ Rotary Encoder Setup ================
extern RotaryEncoder menuEncoder;

#endif