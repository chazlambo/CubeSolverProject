#ifndef ColorSensor_h
#define ColorSensor_h

#include <Arduino.h>
#include <Adafruit_PCF8591.h>
#include <EEPROM.h>

class ColorSensor {
public:
    ColorSensor(Adafruit_PCF8591* adcArray[9], const int pinArray[9], const int ledPins[3], int eepromFlagAddress, int eepromAddresses[9][7][4]);

    void begin();

    // Scan Functions
    void setLED(char color);
    //void scanFace
    void scanFace(char* output[9], int numScans = 5, int delayMs = 30, int tolerance = 20);
    void getFaceColors(char* output[9]);

    // EEPROM Calibration Functions
    bool loadCalibration();
    bool saveCalibration();
    void resetCalibration(int value);

    // Calibration Get/Set
    int setColorCal(int sensorIdx, char color, const int rgbw[4]);
    int getColorCal(int sensorIdx, char color, int channel);

private:
    Adafruit_PCF8591* adcs[9]; // ADC object per sensor
    int pins[9];               // ADC pin (0-3) for each sensor
    int ledPins[3];            // R, G, B output pins for the board

    // Calibration values saved on EEPROM
    int calVals[9][7][4];

    // Scan values
    int scanVals[9][4];
    int numScans = 5;
    int scanDelay = 10;
    int colorTol = 30;

    // EEPROM Variables
    int eepromFlagAddr;
    int eepromAddr[9][7][4];
    int flagValue = 122;

    int colorDistance(const int rgbw1[4], const int rgbw2[4]);
    int readADC(int index);

    // Color Helper Function
    char getColor(int sensorIdx, const int rgbw[4], int tolerance);
    int colorIndex(char color);
};

#endif
