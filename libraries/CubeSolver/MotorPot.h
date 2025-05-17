#ifndef MotorPot_h
#define MotorPot_h

#include <Arduino.h>
#include <EEPROM.h>
#include <Adafruit_PCF8591.h>

class MotorPot{
public:
    MotorPot(int board, int pin, int eepromFlagAddr, int eepromAddr, Adafruit_PCF8591* adc);

    void begin();
    int scan();
    bool isCalibrated();
    int loadCalibration();
    bool saveCalibration();
    int getCalibration();

private:
    int board;
    int pin;
    int eepromFlagAddr;
    int eepromAddr;
    Adafruit_PCF8591* adc;
    int value;
    int calibration;

    static const int minValid = 60;
    static const int maxValid = 120;
    static const int defaultVal = 90;
    static const int flagValue = 177;
};

#endif
