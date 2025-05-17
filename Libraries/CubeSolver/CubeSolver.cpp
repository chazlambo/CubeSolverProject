#include "CubeSolver.h"

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
int numMotors = 6;
int potADCPin[6] = {3, 2, 1, 0, 1, 2};

// Initialize EEPROM Addresses
const int motorCalFlagAddress = 0;
const int motorCalStartAddress = motorCalFlagAddress + 1;

// Create MotorPot Objects
MotorPot motU(ADC_ADDRESS[2], potADCPin[0], motorCalFlagAddress, motorCalStartAddress + 0 * sizeof(int), ADC[2]);
MotorPot motR(ADC_ADDRESS[2], potADCPin[1], motorCalFlagAddress, motorCalStartAddress + 1 * sizeof(int), ADC[2]);
MotorPot motF(ADC_ADDRESS[2], potADCPin[2], motorCalFlagAddress, motorCalStartAddress + 2 * sizeof(int), ADC[2]);
MotorPot motD(ADC_ADDRESS[5], potADCPin[3], motorCalFlagAddress, motorCalStartAddress + 3 * sizeof(int), ADC[5]);
MotorPot motL(ADC_ADDRESS[5], potADCPin[4], motorCalFlagAddress, motorCalStartAddress + 4 * sizeof(int), ADC[5]);
MotorPot motB(ADC_ADDRESS[5], potADCPin[5], motorCalFlagAddress, motorCalStartAddress + 5 * sizeof(int), ADC[5]);

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

// Ring State EEPROM Variables
int ringStateEEPROMAddress = motorCalStartAddress + 6 * sizeof(int);

// Create motor object
CubeMotors cubeMotors(ENPIN, STEPPINS, DIRPINS, ringStateEEPROMAddress);

// ================ Servo Setup ================
// Servo Pins
const int TOPSERVO = 22;
const int BOTSERVO = 14;

// Top Servo Variables
unsigned int topExtPos = 235;
unsigned int topRetPos = 0;
int topSweepDelay = 20;

// Bottom Servo Variables
unsigned int botExtPos = 270;
unsigned int botRetPos = 0;
int botSweepDelay = 20;

// EEPROM Variables
const int topServoEEPROMAddress = motorCalStartAddress + 7 * sizeof(int);
const int botServoEEPROMAddress = topServoEEPROMAddress + sizeof(int);

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

// Initialize EEPROM
const int colorSensor1EEPROMAddress = botServoEEPROMAddress + sizeof(int) * 2;
const int colorSensor2EEPROMAddress = colorSensor1EEPROMAddress + sizeof(int);

// Create ColorSensor Objects
ColorSensor colorSensor1(adcPtrs1, sensorPins1, ledPins1, colorSensor1EEPROMAddress);
ColorSensor colorSensor2(adcPtrs2, sensorPins2, ledPins2, colorSensor2EEPROMAddress);

// ================ Rotary Encoder Setup ================
RotaryEncoder menuEncoder;


void mainSetup()
{
    Serial.begin(baudRate);
    pinMode(POWPIN, INPUT);

    // Initialize I2C Bus
    Wire.begin();

    // Initialize ADC modules
    ADC1.begin(ADC_ADDRESS[0]);
    ADC2.begin(ADC_ADDRESS[1]);
    ADC3.begin(ADC_ADDRESS[2]);
    ADC4.begin(ADC_ADDRESS[3]);
    ADC5.begin(ADC_ADDRESS[4]);
    ADC6.begin(ADC_ADDRESS[5]);

    // Begin Stepper Motors
    cubeMotors.begin();

    // Begin Servos
    topServo.begin();
    botServo.begin();

    // Begin Color Sensors
    colorSensor1.begin();
    colorSensor2.begin();

    // Begin Motor Pots
    for (int i = 0; i < numMotors; i++) {
        MotorPots[i]->begin();
    }

    // Begin Rotary Encoder
    // menuEncoder.begin();

    // Home motors
    motorHomeState = homeMotors();
}

bool powerCheck(){
    bool status = digitalRead(POWPIN);
    return status;
}

bool getMotorCalibration() {
    for (int i = 0; i < numMotors; i++) {
        if (MotorPots[i]->loadCalibration()) {
            return false;
        }
    }
    return true;
}

int homeMotors(){
    // Homes motors
    //
    // Outputs:
    //      0 - Success
    //      1 - Motors not calibrated
    //      2 - Did not reach threshold in time

    int threshold = 1;
    int stepSize = 1;
    unsigned long timeout = 5000; // Timeout after 5 seconds

    // Initialize variables
    bool aligned = false;
    unsigned long t_home_start = millis();
    unsigned long t_home = millis();
    long pos[6];    // Temporary position vector

    // Retrieve calibrated values and check if calibrated
    if (!getMotorCalibration()) {
        return 1;
    }

    // Start loop
    cubeMotors.enableMotors();
    while (!aligned) {
        aligned = true;

        // Check each motor
        for (int i = 0; i < numMotors; i++) {

            // Update potentiometer values
            int currentVal = MotorPots[i]->scan();
            int targetVal = MotorPots[i]->getCalibration();

            // Get position values
            pos[i] = cubeMotors.getPos(i);

            if (abs(currentVal - targetVal) > threshold) {
                aligned = false;

                // If Upper Motor
                if (i == 0) { // Reverse direction for upper motor
                    if(currentVal > targetVal) {
                        pos[i] -= stepSize;
                    }
                    else {
                        pos[i] += stepSize;
                    }
                }

                // If any other motor
                else {
                    if(currentVal > targetVal) {
                        pos[i] += stepSize;
                    }
                    else {
                        pos[i] -= stepSize;
                    }
                }
            }
        }

        // Apply new positions
        cubeMotors.moveTo(pos);

        // Add function timeout
        t_home = millis();
        if(t_home - t_home_start > timeout) {
            return 2;
        }
    }
    cubeMotors.disableMotors();

    // Manually reset stepper position
    cubeMotors.resetMotorPos();
    
    return 0;   // Return successful alignment
}
