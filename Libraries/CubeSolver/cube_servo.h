// cube_servo_h
#ifndef cube_servo_h
#define cube_servo_h

#include <Arduino.h>
#include <PWMServo.h>
#include <EEPROM.h>

// Servo Pins
const int TOPSERVO = 22;
const int BOTSERVO = 14;

// Servo Variables
unsigned int topServoPos = 0;
unsigned int topExtPos = 235;
unsigned int topRetPos = 0;
int topExtState = 0;

unsigned int botServoPos = 0;
unsigned int botExtPos = 270;
unsigned int botRetPos = 0;
int botExtState = 0;

// Servo Sweep Variables
int sweepDelay = 20;

// Servo Definitions
PWMServo topServo;
PWMServo botServo;

// EEPROM Variables
const int topServoEEPROMAddress = 17;
const int botServoEEPROMAddress = 19;


// Setup Function
void servoSetup();      // Setup function

// Basic Servo Functions
void servoSweep(PWMServo &servo, unsigned int &currentPos, unsigned int newPos);
void topServoExtend();  // Moves top servo to extended position
void topServoRetract(); // Moves top servo to retracted position

void botServoExtend();  // Moves bottom servo to extended position
void botServoRetract(); // Moves bottom servo to retracted position

void topServoToggle();  // Toggles top servo position
void botServoToggle();  // Toggles bottom servo position

// EEPROM Functions

#endif // cube_servo_h
