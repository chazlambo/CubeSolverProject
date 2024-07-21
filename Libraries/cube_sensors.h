// cube_sensors_h
#ifndef cube_sensors_h
#define cube_sensors_h

// Includes
#include <Arduino.h>
#include <Adafruit_PCF8591.h>   // ADC Library
#include <Adafruit_seesaw.h>    // ANO Library
#include <EEPROM.h>             // Storing calibrated values
#include "colorPot.h"

// RGB Pins
const int COLOR1[3] = {34, 33, 35}  // R, G, B
const int COLOR2[3] = {37, 36, 38}  // R, G, B

// ADC Modules
const int ADC_ADDRESS[6] = {0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D};
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
colorPot C1_1(0x48, 2);
colorPot C1_2(0x48, 1);
colorPot C1_3(0x48, 0);
colorPot C1_4(0x49, 1);
colorPot C1_5(0X49, 0);
colorPot C1_6(0x48, 3);
colorPot C1_7(0x4A, 0);
colorPot C1_8(0x49, 3);
colorPot C1_9(0x49, 2);
colorPot* ColorSensor1[] = {&C1_1, &C1_2, &C1_3, &C1_4, &C1_5, &C1_6, &C1_7, &C1_8, &C1_9};

// Color Sensor 1
colorPot C2_1(0x48, 2);
colorPot C2_2(0x48, 1);
colorPot C2_3(0x48, 0);
colorPot C2_4(0x49, 1);
colorPot C2_5(0X49, 0);
colorPot C2_6(0x48, 3);
colorPot C2_7(0x4A, 0);
colorPot C2_8(0x49, 3);
colorPot C2_9(0x49, 2);
colorPot* ColorSensor2[] = {&C2_1, &C2_2, &C2_3, &C2_4, &C2_5, &C2_6, &C2_7, &C2_8, &C2_9};

// Motro Sensor Pins
const int MOT_ADDRESS[6] =  {0x4A,  0x4A,   0X4A,   0X4D,   0X4D,   0X4D};  // URFDLB
const int MOT_ADCPIN[6]  =  {   3,     2,      1,      0,      1,      2};  // URFDLB

// ANO Rotary Encoder 
const int ANO_ADDRESS = 0x4E;
#define ANO_SWITCH_SELECT 1
#define ANO_SWITCH_UP     2
#define ANO_SWITCH_LEFT   3
#define ANO_SWITCH_DOWN   4
#define ANO_SWITCH_RIGHT  5

Adafruit_seesaw ANO;
int32_t encoder_position;

// Setup functions
void setupRGB();    // Sets up RGB LED's on color sensors
void setupI2C();    // Sets up I2C devices
void setupANO();    // Sets up ANO rotary encoder

// EEPROM Setup
// EEPROM Variables
int eeAddress = 0; //EEPROM address of first calibration value


#endif // cube_sensors_h