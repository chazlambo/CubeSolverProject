// colorPot.h
#ifndef colorPot_h
#define colorPot_h

#include <Arduino.h>

class colorPot
{

private:
// ADC Board I2C Address
int Board;
int Pin; 

// Calibration values
cal_R[4];   // Calibrated red RGBW
cal_G[4];   // Calibrated green RGBW
cal_B[4];   // Calibrated blue RGBW
cal_Y[4];   // Calibrated yellow RGBW
cal_O[4];   // Calibrated orange RGBW
cal_W[4];   // Calibrated white RGBW
cal_E[4];   // Calibrated empy RGBW


public:
    colorPot(int board, int pin);
    void setCal(char color);
    char colorParse(int rgbw[4]);
    char colorScan();
};

#endif // colorPot_h
