#ifndef MotorEncoder_h
#define MotorEncoder_h

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_TCA9548A.h>

// Reads an AS5600 via a TCA9548A I2C mux.
// Calibration is mandatory; no default approximations are used.
class MotorEncoder {
public:
    // channel: TCA9548A channel [0-7]
    // eepromFlagAddr: byte address for "calibrated" flag (value = 177)
    // eepromAddr[4]: starting addresses for four 16-bit calibration values
    // tca: shared Adafruit_TCA9548A
    // as5600Addr: I2C address of AS5600 (default 0x36)
    MotorEncoder(int channel,
                 int eepromFlagAddr,
                 int eepromAddr[4],
                 Adafruit_TCA9548A* tca,
                 int as5600Addr = 0x36);

    int       begin();                       
    int       scan();                        // returns raw 12-bit angle [0-4095]
    bool      isCalibrated();                // EEPROM flag check
    int       getCalibration(int index = 0); // Returns stored value [0-4095]
    int       setCalibration(int index);     // Set value based on scan
    int       setCalibration(int index, int value);    // Directly Set Value

    // EEPROM load/save
    int  loadCalibration();
    bool saveCalibration();

private:
    int channel;
    int as5600Addr;
    Adafruit_TCA9548A* tca;

    // EEPROM & state
    static constexpr int kFlagValue = 177;  // Arbitrary calibration flag value 
    int eepromFlagAddr;
    int eepromAddr[4];                      // Addresses for int values
    int value = 0;                          // Most recent scan value
    int calibration[4] = {0, 0, 0, 0};      // Valid after successful load/calibration

    // Helpers
    bool selectMux();
    bool deselectMux();
};

#endif
