#include "CubeHardwareConfig.h"

// ================ EEPROM Setup ================
const int startAddress = 0;

// Early EEPROM layout setup
struct _EEPROMInit {
    _EEPROMInit() { initializeEEPROMLayout(startAddress); }
  } _earlyInit;

int motorCalFlagAddress;
int motorCalStartAddress;
int motorCalAddresses[7][4];
int ringStateEEPROMAddress;
int topServoEEPROMAddress;
int botServoEEPROMAddress;
int colorSensor1EEPROMAddress;
int colorSensor2EEPROMAddress;

// ================ Serial Communication Setup ================
 const int baudRate = 115200;

 // ================ Power Setup ================
 const int POWPIN = 32;  

// ================ Motor Encoder Setup ================

// Create Motor Encoder Multiplexer
const int ENC_MUX_ADDR = 0x70;
const int ENC_MUX_RST = 30;
Adafruit_TCA9548A encoderMux(ENC_MUX_ADR);

// Create MotorEncoder Objects
MotorEncoder motU(0, motorCalFlagAddress, motorCalAddresses[0], &encoderMux);
MotorEncoder motR(1, motorCalFlagAddress, motorCalAddresses[1], &encoderMux);
MotorEncoder motF(2, motorCalFlagAddress, motorCalAddresses[2], &encoderMux);
MotorEncoder motD(3, motorCalFlagAddress, motorCalAddresses[3], &encoderMux);
MotorEncoder motL(4, motorCalFlagAddress, motorCalAddresses[4], &encoderMux);
MotorEncoder motB(5, motorCalFlagAddress, motorCalAddresses[5], &encoderMux);
MotorEncoder motRing(6, motorCalFlagAddress, motorCalAddresses[6], &encoderMux);
MotorEncoder* MotorEncoders[] = {&motU, &motR, &motF, &motD, &motL, &motB, &motRing};

// ================ Motor Setup ================

// Pin Definitions
const int ENPIN = 26;

const int STEP_U = 0;
const int DIR_U  = 1;
const int STEP_R = 2;
const int DIR_R  = 3;
const int STEP_F = 4;
const int DIR_F  = 5;
const int STEP_D = 6;
const int DIR_D  = 7;
const int STEP_L = 8;
const int DIR_L  = 9;
const int STEP_B = 24;
const int DIR_B  = 25;
const int STEP_RING = 28;
const int DIR_RING = 29;

int STEPPINS[] = {STEP_U, STEP_R, STEP_F, STEP_D, STEP_L, STEP_B, STEP_RING};
int DIRPINS[] = {DIR_U, DIR_R, DIR_F, DIR_D, DIR_L, DIR_B, DIR_RING};

// Create motor object
CubeMotors cubeMotors(ENPIN, STEPPINS, DIRPINS, ringStateEEPROMAddress);

// ================ Servo Setup ================
// Servo Pins
const int TOPSERVO = 23;
const int BOTSERVO = 22;

// Top Servo Variables
unsigned int topExtPos = 235;
unsigned int topRetPos = 0;
int topSweepDelay = 15;

// Bottom Servo Variables
unsigned int botExtPos = 270;
unsigned int botRetPos = 10;
int botSweepDelay = 15;

// Create Servo Objects
CubeServo topServo(TOPSERVO, topServoEEPROMAddress, topRetPos, topExtPos, topSweepDelay);               
CubeServo botServo(BOTSERVO, botServoEEPROMAddress, botRetPos, botExtPos, botSweepDelay); 

// ================ Color Sensor Setup ================
//
// Color Sensor 1:
//
// Silkscreen Labeling:                         I2C-Mux-SCL/SDA:                            Corresponding Cube Face Square:      
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+ 
// |          |          |          |           |          |          |          |          |          |          |          |
// |    1     |    2     |    3     |           | I2C-1-2  | I2C-1-1  | I2C-1-0  |          |    UR    |    UM    |    UL    |
// |          |          |          |           |          |          |          |          |          |          |          |
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+
// |          |          |          |           |          |          |          |          |          |          |          |
// |    4     |    5     |    6     |           | I2C-2-0  | I2C-1-4  | I2C-1-3  |          |    MR    |    MM    |    ML    |
// |          |          |          |           |          |          |          |          |          |          |          |
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+
// |          |          |          |           |          |          |          |          |          |          |          |
// |    7     |    8     |    9     |           | I2C-2-3  | I2C-2-2  | I2C-2-1  |          |    DR    |    DM    |    DL    |
// |          |          |          |           |          |          |          |          |          |          |          |
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+
// 
// Top left Corner for Scan: 3
// ===========================================================================================================================
//
// Color Sensor 2:
//
// Silkscreen Labeling:                         I2C-Mux-SCL/SDA:                            Corresponding Cube Face Square:      
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+ 
// |          |          |          |           |          |          |          |          |          |          |          |
// |    9     |    8     |    7     |           | I2C-2-1  | I2C-2-2  | I2C-2-3  |          |    DL    |    DM    |    DR    |
// |          |          |          |           |          |          |          |          |          |          |          |
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+
// |          |          |          |           |          |          |          |          |          |          |          |
// |    6     |    5     |    4     |           | I2C-1-3  | I2C-1-4  | I2C-2-0  |          |    ML    |    MM    |    MR    |
// |          |          |          |           |          |          |          |          |          |          |          |
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+
// |          |          |          |           |          |          |          |          |          |          |          |
// |    3     |    2     |    1     |           | I2C-1-0  | I2C-1-1  | I2C-1-2  |          |    UL    |    UM    |    UR    |
// |          |          |          |           |          |          |          |          |          |          |          |
// +==========+==========+==========+           +==========+==========+==========+          +==========+==========+==========+
//
// Top Left Corner for Scan: 3 
// Result: Can scan sensors in same order even though the 2nd sensor is flipped because we're also flipping the reading.
// ===========================================================================================================================
                                                                                            
// Sensor Read Order        {UL, UM, UR, ML, MM, MR, DL, DM, DR}
int colorSensorMuxOrder[] =     { 1,  1,  1,  1,  1,  2,  2,  2,  2};   // Maps multiplexer to cube face squares
int colorSensorChannelOrder[] = { 0,  1,  2,  3,  4,  0,  1,  2,  3};   // Maps mux channel to cube face squares

// Set Color Sensor Mux Addresses
const int C1_MUX1_ADDR = 0x74;
const int C1_MUX2_ADDR = 0x75;
const int C2_MUX1_ADDR = 0x76;
const int C2_MUX2_ADDR = 0x77;

// Create Color Sensor Multiplexer Objects
Adafruit_TCA9548A color1Mux1(C1_MUX1_ADDR);
Adafruit_TCA9548A color1Mux2(C1_MUX2_ADDR);
Adafruit_TCA9548A* color1Muxes[] = {color1Mux1, color1Mux2};

Adafruit_TCA9548A color2Mux1(C2_MUX1_ADDR);
Adafruit_TCA9548A color2Mux2(C2_MUX2_ADDR);
Adafruit_TCA9548A* color2Muxes[] = {color2Mux1, color2Mux2};


// Initialize LED Pins for each Color Sensor
const int colorSensorLED1 = 41;   // White LED pin for color sensor 1
const int colorSensorLED2 = 40;   // White LED pin for color sensor 2

// Color Sensor EEPROM
int colorSensor1EEPROMFlag;
int colorSensor2EEPROMFlag;
int colorSensor1EEPROMAddresses[9][7][4];
int colorSensor2EEPROMAddresses[9][7][4];

// Create ColorSensor Objects
ColorSensor colorSensor1(color1Muxes, colorSensorLED1, colorSensorMuxOrder, colorSensorChannelOrder, colorSensor1EEPROMFlag, colorSensor1EEPROMAddresses);
ColorSensor colorSensor2(color2Muxes, colorSensorLED2, colorSensorMuxOrder, colorSensorChannelOrder, colorSensor2EEPROMFlag, colorSensor2EEPROMAddresses);

// ================ Rotary Encoder Setup ================
RotaryEncoder menuEncoder;

// ================ EEPROM Initialization Function ================
void initializeEEPROMLayout(int startAddress) {
    int addr = startAddress;

    // Motor Potentiometer Calibration EEPROM
    motorCalFlagAddress = addr;
    addr += sizeof(int);
    
    for (int i = 0; i < 7; i++) {
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
        for (int j = 0; j < 7; j++) {       // Loop through each color
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