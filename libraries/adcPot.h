// adcPot.h
#ifndef adcPot_h
#define adcPot_h

#include <Arduino.h>

class adcPot
{

public:
// ADC Board I2C Address
int Board;
int Pin; 

private:
// Color Calibration Values
int cal_R[4];   // Calibrated red RGBW
int cal_G[4];   // Calibrated green RGBW
int cal_B[4];   // Calibrated blue RGBW
int cal_Y[4];   // Calibrated yellow RGBW
int cal_O[4];   // Calibrated orange RGBW
int cal_W[4];   // Calibrated white RGBW
int cal_E[4];   // Calibrated empy RGBW

// Motor Calibration Values
int motorCal;


public:
    adcPot(int board, int pin);
    void setColorCal(char color);
    void setMotorCal(int cal);
    char colorParse(int rgbw[4], int tolerance);
};

#endif // adcPot_h
