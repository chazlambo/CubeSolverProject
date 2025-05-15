#ifndef ColorSensor_h
#define ColorSensor_h

#include <Arduino.h>
#include <Adafruit_PCF8591.h>

class ColorSensor {
public:
    ColorSensor(Adafruit_PCF8591* adcArray[9], const int pinArray[9], const int ledPins[3], int eepromStartAddress);

    void begin();

    void scanFace(char output[9], int numScans = 5, int delayMs = 30, int tolerance = 20);

    // Calibration Functions
    bool loadCalibration();
    bool saveCalibration();
    void setColorCal(int sensorIdx, char color, const int rgbw[4]);

    char getColor(int sensorIdx, const int rgbw[4], int tolerance);

private:
    Adafruit_PCF8591* adcs[9]; // ADC object per sensor
    int pins[9];               // ADC pin (0-3) for each sensor
    int ledPins[3];            // R, G, B output pins for the board

    // Calibration values saved on EEPROM
    int cal_R[9][4];
    int cal_G[9][4];
    int cal_B[9][4];
    int cal_Y[9][4];
    int cal_O[9][4];
    int cal_W[9][4];
    int cal_E[9][4];

    // EEPROM Variables
    int eepromBase;
    int flagValue = 122;

    void setLED(char color);
    int colorDistance(const int rgbw1[4], const int rgbw2[4]);
    int readADC(int index);
};

#endif
