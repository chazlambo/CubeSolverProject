#include "CubeHardwareConfig.h"

// ================ EEPROM Setup ================
const int startAddress = 0;

// Early EEPROM layout setup
struct _EEPROMInit {
    _EEPROMInit() { initializeEEPROMLayout(startAddress); }
  } _earlyInit;

int motorCalFlagAddress;
int motorCalStartAddress;
int motorCalAddresses[6][4];
int ringStateEEPROMAddress;
int topServoEEPROMAddress;
int botServoEEPROMAddress;
int colorSensor1EEPROMAddress;
int colorSensor2EEPROMAddress;

// ================ Serial Communication Setup ================
 const int baudRate = 115200;

 // ================ Power Setup ================
 const int POWPIN = 23;  

// ================ ADC Module Setup ================
const int ADC_ADDRESS[6] = {0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D};
Adafruit_PCF8591 ADC1 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC2 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC3 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC4 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC5 = Adafruit_PCF8591();
Adafruit_PCF8591 ADC6 = Adafruit_PCF8591();
Adafruit_PCF8591* ADC[] = {&ADC1, &ADC2, &ADC3, &ADC4, &ADC5, &ADC6};

// ================ Motor Potentiometer Setup ================
const int numMotors = 6;
const int potADCPin[6] = {3, 2, 1, 0, 1, 2};

// Create MotorPot Objects
MotorPot motU(ADC_ADDRESS[2], potADCPin[0], motorCalFlagAddress, motorCalAddresses[0], ADC[2]);
MotorPot motR(ADC_ADDRESS[2], potADCPin[1], motorCalFlagAddress, motorCalAddresses[1], ADC[2]);
MotorPot motF(ADC_ADDRESS[2], potADCPin[2], motorCalFlagAddress, motorCalAddresses[2], ADC[2]);
MotorPot motD(ADC_ADDRESS[5], potADCPin[3], motorCalFlagAddress, motorCalAddresses[3], ADC[5]);
MotorPot motL(ADC_ADDRESS[5], potADCPin[4], motorCalFlagAddress, motorCalAddresses[4], ADC[5]);
MotorPot motB(ADC_ADDRESS[5], potADCPin[5], motorCalFlagAddress, motorCalAddresses[5], ADC[5]);
MotorPot* MotorPots[] = {&motU, &motR, &motF, &motD, &motL, &motB};

// ================ Motor Setup ================

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
int STEPPINS[] = {STEP1, STEP2, STEP3, STEP4, STEP5, STEP6, STEP7};
int DIRPINS[] = {DIR1, DIR2, DIR3, DIR4, DIR5, DIR6, DIR7};

// Create motor object
CubeMotors cubeMotors(ENPIN, STEPPINS, DIRPINS, ringStateEEPROMAddress);

// ================ Servo Setup ================
// Servo Pins
const int TOPSERVO = 22;
const int BOTSERVO = 14;

// Top Servo Variables
unsigned int topExtPos = 235;
unsigned int topRetPos = 0;
int topSweepDelay = 15;

// Bottom Servo Variables
unsigned int botExtPos = 270;
unsigned int botRetPos = 0;
int botSweepDelay = 15;

// Create Servo Objects
CubeServo topServo(TOPSERVO, topServoEEPROMAddress, topRetPos, topExtPos, topSweepDelay);               
CubeServo botServo(BOTSERVO, botServoEEPROMAddress, botRetPos, botExtPos, botSweepDelay); 

// ================ Color Sensor Setup ================

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

// Initialize array of ADC pointers for each Color Sensor
Adafruit_PCF8591* adcPtrs1[9] = { 
    ADC[0], ADC[0], ADC[0], 
    ADC[1], ADC[1], ADC[0], 
    ADC[2], ADC[1], ADC[1]
};

Adafruit_PCF8591* adcPtrs2[9] = { 
    ADC[3], ADC[3], ADC[3], 
    ADC[3], ADC[5], ADC[4], 
    ADC[4], ADC[4], ADC[4]
};

// Initialize LED Pins for each Color Sensor
int ledPins1[3] = { 34, 33, 35 };  // R, G, B for Color Sensor 1
int ledPins2[3] = { 37, 36, 38 };  // R, G, B for Color Sensor 2

// Initialize Sensor Pins for each Color Sensor
int sensorPins1[9] = { 
    2, 1, 0,  // C1-1, C1-2, C1-3
    1, 0, 3,  // C1-4, C1-5, C1-6
    0, 3, 2   // C1-7, C1-8, C1-9
};

int sensorPins2[9] = { 
    0, 1, 2,  // C2-1, C2-2, C2-3
    3, 3, 0,  // C2-4, C2-5, C2-6
    1, 2, 3   // C2-7, C2-8, C2-9
};

// Color Sensor EEPROM
int colorSensor1EEPROMFlag;
int colorSensor2EEPROMFlag;
int colorSensor1EEPROMAddresses[9][7][4];
int colorSensor2EEPROMAddresses[9][7][4];

// Create ColorSensor Objects
ColorSensor colorSensor1(adcPtrs1, sensorPins1, ledPins1, colorSensor1EEPROMFlag, colorSensor1EEPROMAddresses);
ColorSensor colorSensor2(adcPtrs2, sensorPins2, ledPins2, colorSensor2EEPROMFlag, colorSensor2EEPROMAddresses);

// ================ Rotary Encoder Setup ================
RotaryEncoder menuEncoder;

// Functions
void initializeEEPROMLayout(int startAddress) {
    int addr = startAddress;

    // Motor Potentiometer Calibration EEPROM
    motorCalFlagAddress = addr;
    addr += sizeof(int);

    motorCalStartAddress = addr;
    addr += sizeof(int);
    
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 4; j++) {
            motorCalAddresses[i][j] = addr;
            addr += sizeof(int);
        }
    }

    // Ring State EEPROM
    ringStateEEPROMAddress = addr;
    addr += sizeof(int);

    // Servo EEPROM
    topServoEEPROMAddress = addr;
    addr += sizeof(int);
    botServoEEPROMAddress = addr;
    addr += sizeof(int);

    // Color Sensor EEPROM  
    colorSensor1EEPROMFlag = addr;          // Set flag address
    addr += sizeof(int);
    for (int i = 0; i < 9; i++) {           // Loop through each sensor
        for (int j = 0; j < 6; j++) {       // Loop through each color
            for (int k = 0; k < 4; k++) {   // Loop through RGBW values
                colorSensor1EEPROMAddresses[i][j][k] = addr;
                addr += sizeof(int);
            } 
        }
    }

    colorSensor2EEPROMFlag = addr;          // Set flag address
    addr += sizeof(int);
    for (int i = 0; i < 9; i++) {           // Loop through each sensor
        for (int j = 0; j < 7; j++) {       // Loop through each color (including empty)
            for (int k = 0; k < 4; k++) {   // Loop through RGBW values
                colorSensor2EEPROMAddresses[i][j][k] = addr;
                addr += sizeof(int);
            } 
        }
    }
}