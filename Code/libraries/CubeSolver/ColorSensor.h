#ifndef ColorSensor_h
#define ColorSensor_h

#include <Arduino.h>
#include <Adafruit_TCA9548A.h>
#include <veml6040.h>
#include <EEPROM.h>
#include <RunningMedian.h>

class ColorSensor {
public:
    ColorSensor(Adafruit_TCA9548A* multiplexers[2], const int LEDPIN, int muxOrder[9], int channelOrder[9], int eepromFlagAddress, int eepromAddresses[9][7][4]);

    int begin();

    // Scan Functions
    void setLED(bool ledState);
    void scanFace();
    void getFaceColors(char output[9]);
    const int* getScanValRow(int idx);

    // EEPROM Calibration Functions
    bool loadCalibration();
    bool saveCalibration();
    void resetCalibration();

    // Calibration Get/Set
    int setColorCal(int sensorIdx, char color, const int rgbw[4]);
    int getColorCal(int sensorIdx, char color, int channel);

    // Parse Color
    char getColor(int sensorIdx, const int rgbw[4]);

private:
    Adafruit_TCA9548A* multiplexers[2]; // The two muxes on each boards
    VEML6040 veml;                      // Color sensor object
    int ledPin;                         // LED output pin for the board
    int muxOrder[9];                    // Map from cube face square to mux {UL, UM, UR, ML, MM, MR, DL, DM, DR}
    int channelOrder[9];                // Map from cube face square to I2C channel {UL, UM, UR, ML, MM, MR, DL, DM, DR}

    // Calibration values saved on EEPROM
    int calVals[9][7][4];

    // Scan values
    int currentRGBW[4];
    int scanVals[9][4];
    int numScans = 1;
    int integrationTime = VEML6040_IT_80MS; // Integration time (40MS, 80MS, 160MS, 320MS, 640MS, 1280MS) (Time per scan per sensor)
    int colorTol = 20;

    // EEPROM Variables
    int eepromFlagAddr;
    int eepromAddr[9][7][4];
    int flagValue = 122;

    int colorDistance(const int rgbw1[4], const int rgbw2[4]);
    int readSensor(int sensorIdx);

    // Color Helper Function
    int colorIndex(char color);
};

#endif
