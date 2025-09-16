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

// ================ Motor Encoder Setup ================

// Initialize EEPROM Addresses
extern int motorCalFlagAddress;
extern int motorCalAddresses[7][4];

// Create Motor Encoder Mux Object
extern const int ENC_MUX_ADDR;
extern const int ENC_MUX_RST;
extern Adafruit_TCA9548A encoderMux;

// Mux channels for each encoder {U, R, F, D, L, B, RING}
int encoderChannels[7];

// Create Motor Encoder Objects
extern MotorEncoder motU, motR, motF, motD, motL, motB, motRing;
extern MotorEncoder* MotorEncoders[7];

// ================ Motor Setup ================

// Pin Definitions
extern const int ENPIN;
extern const int DIR_U, DIR_R, DIR_F, DIR_D, DIR_L, DIR_B, DIR_RING;
extern const int STEP_U, STEP_R, STEP_F, STEP_D, STEP_L, STEP_B, STEP_RING;
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

// I2C Multiplexer
extern const int C1_MUX1_ADDR;
extern const int C1_MUX2_ADDR;
extern const int C2_MUX1_ADDR;
extern const int C2_MUX2_ADDR;

extern Adafruit_TCA9548A color1Mux1;
extern Adafruit_TCA9548A color1Mux2;
extern Adafruit_TCA9548A* color1Muxes[2];

extern Adafruit_TCA9548A color2Mux1;
extern Adafruit_TCA9548A color2Mux2;
extern Adafruit_TCA9548A* color2Muxes[2];

// Sensor Read Order {UL, UM, UR, ML, MM, MR, DL, DM, DR}
extern int colorSensorMuxOrder[9];
extern int colorSensorChannelOrder[9];

// LED Pins
extern const int colorSensorLED1;
extern const int colorSensorLED2;

// Initialize EEPROM
extern int colorSensor1EEPROMFlag, colorSensor2EEPROMFlag;
extern int colorSensor1EEPROMAddresses[9][7][4], colorSensor2EEPROMAddresses[9][7][4];

// Create ColorSensor Objects
extern ColorSensor colorSensor1, colorSensor2;

// ================ Rotary Encoder Setup ================
extern RotaryEncoder menuEncoder;

#endif