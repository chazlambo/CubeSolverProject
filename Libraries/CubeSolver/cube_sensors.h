// cube_sensors_h
#ifndef cube_sensors_h
#define cube_sensors_h

// Includes
#include <Arduino.h>
#include "string.h"             
#include <Adafruit_PCF8591.h>   // ADC Library
#include <Adafruit_seesaw.h>    // ANO Library
#include <EEPROM.h>             // Storing calibrated values
#include "adcPot.h"             // adcPot Class

// ANO Rotary Encoder 
const int ANO_ADDRESS = 0x4E;
#define ANO_SWITCH_SELECT 1
#define ANO_SWITCH_UP     2
#define ANO_SWITCH_LEFT   3
#define ANO_SWITCH_DOWN   4
#define ANO_SWITCH_RIGHT  5

Adafruit_seesaw ANO;
int32_t encoder_position;

// RGB Pins
const int COLOR1[3] = {34, 33, 35}; // R, G, B
const int COLOR2[3] = {37, 36, 38}; // R, G, B

// ADC Modules
constexpr int ADC_ADDRESS[6] = {0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D};
Adafruit_PCF8591 ADC1 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC2 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC3 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC4 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC5 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC6 = Adafruit_PCF8591();

// Color Sensor Pin Table
// +======+======+======+======+======+======+
// |      |      |  A0  |  A1  |  A2  |  A3  |
// +======+======+======+======+======+======+
// | 0x48 | ADC1 | C1-3 | C1-2 | C1-1 | C1-6 |
// +------+------+------+------+------+------+
// | 0x49 | ADC2 | C1-5 | C1-4 | C1-9 | C1-8 |
// +------+------+------+------+------+------+
// | 0x4A | ADC3 | C1-7 | M-F  | M-R  | M-U  |
// +------+------+------+------+------+------+
// | 0x4B | ADC4 | C2-1 | C2-2 | C2-3 | C2-4 |
// +------+------+------+------+------+------+
// | 0x4C | ADC5 | C2-6 | C2-7 | C2-8 | C2-9 |
// +------+------+------+------+------+------+
// | 0x4D | ADC6 | M-D  | M-L  | M-B  | C2-5 |
// +------+------+------+------+------+------+

//Color Sensor Pins
// Color Sensor 1
adcPot C1_1(0x48, 2);
adcPot C1_2(0x48, 1);
adcPot C1_3(0x48, 0);
adcPot C1_4(0x49, 1);
adcPot C1_5(0X49, 0);
adcPot C1_6(0x48, 3);
adcPot C1_7(0x4A, 0);
adcPot C1_8(0x49, 3);
adcPot C1_9(0x49, 2);
adcPot* ColorSensor1[] = {&C1_1, &C1_2, &C1_3, &C1_4, &C1_5, &C1_6, &C1_7, &C1_8, &C1_9};

// Color Sensor 2
adcPot C2_1(0x48, 2);
adcPot C2_2(0x48, 1);
adcPot C2_3(0x48, 0);
adcPot C2_4(0x49, 1);
adcPot C2_5(0X49, 0);
adcPot C2_6(0x48, 3);
adcPot C2_7(0x4A, 0);
adcPot C2_8(0x49, 3);
adcPot C2_9(0x49, 2);
adcPot* ColorSensor2[] = {&C2_1, &C2_2, &C2_3, &C2_4, &C2_5, &C2_6, &C2_7, &C2_8, &C2_9};

// Motor Potentiometers
adcPot MOT_U(0x4A, 3);
adcPot MOT_R(0x4A, 2);
adcPot MOT_F(0x4A, 1);
adcPot MOT_D(0x4D, 0);
adcPot MOT_L(0x4D, 1);
adcPot MOT_B(0x4D, 2);
adcPot* MotorPots[] = {&MOT_U, &MOT_R, &MOT_F, &MOT_D, &MOT_L, &MOT_B};

// EEPROM Setup
// EEPROM Variables
const int motorCalFlagAddress = 0;
const int motorCalStartAddress = 1;
const int motorCalFlag = 177;       // Arbitrary unique integer
const int numMotors = 6;
bool motorsCalibrated = 0;

// Motor Potentiometer Variables
int motCalMin = 60;
int motCalMax = 120;
int motCalDefault = 90;
int motorCals[numMotors]; // Stores EEPROM saved calibration values for potentiometers
int motorVals[numMotors]; // Stores current readings of potentiometers

// Color Sensor Scan Variables
char faceScanArray[9];       // Array to store characters for most recent color scan
int colorDelay = 30;    // Time in ms for scan to wait after each LED is turned on to allow potentiometer to adjust
int numScans = 5;       // Number of readings to take for average reading
int colorScanTolerance = 20;

// Setup functions
void setupRGB();    // Sets up RGB LED's on color sensors
void setupI2C();    // Sets up I2C devices
void setupANO();    // Sets up ANO rotary encoder

// ADC Scan Functions
int scanADC(adcPot* pot);   // Returns the read value of the specified pot
void updateMotorVals();

// Potentiometer Calibration Function
bool isMotorCalibrated();
bool getMotorCalibration();
bool setMotorCalibration();

// Color Sensor Scan Functions
void setLED(char color);
void faceScan(adcPot* colorSensor[9]);

#endif // cube_sensors_h