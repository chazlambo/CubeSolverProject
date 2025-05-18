#ifndef MotorPot_h
#define MotorPot_h

#include <Arduino.h>
#include <EEPROM.h>
#include <Adafruit_PCF8591.h>

class MotorPot{
public:
    MotorPot(int board, int pin, int eepromFlagAddr, int eepromAddr[4], Adafruit_PCF8591* adc);

    void begin();
    int scan();
    bool isCalibrated();
    int getCalibration(int index=0);
    int setCalibration(int index);             // Set value based on scan
    int setCalibration(int index, int value);  // Directly set value

    // EEPROM Load/Save Functions
    int loadCalibration();
    bool saveCalibration();

private:
    int board;
    int pin;
    int flagValue = 177;
    int eepromFlagAddr;
    int eepromAddr[4];  // 0 = manual home, 1–3 = 90°, 180°, 270°
    Adafruit_PCF8591* adc;
    int value;
    int calibration[4];

    int defaultVal[4] = {90, 174, 248, 26};
    int tolerance = 30;               // Allowed variance for values between pots
    int minVal[4];
    int maxVal[4];
    bool wrapped[4];

};

#endif
