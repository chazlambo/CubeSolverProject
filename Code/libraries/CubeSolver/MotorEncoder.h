#ifndef MotorEncoder_h
#define MotorEncoder_h

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <TCA9548.h>

class MotorEncoder {
public:
    // channel: TCA9548A channel [0-7]
    // eepromFlagAddr: byte address for "calibrated" flag (value = 177)
    // eepromAddr[4]: starting addresses for four 16-bit calibration values
    // encoderMux: shared TCA9548A
    // ENC_ADDR: I2C address of AS5600 (default 0x36)

    MotorEncoder(int channel,
                 TCA9548* encoderMux,
                 int eepromFlagAddr,
                 int eepromAddr[4],
                 int ENC_ADDR = 0x36);

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
    int ENC_ADDR;
    TCA9548* encoderMux;

    // EEPROM & state
    static constexpr int kFlagValue = 177;  // Arbitrary calibration flag value 
    int eepromFlagAddr;
    int eepromAddr[4];                      // Addresses for int values
    uint16_t value = 0;                     // Most recent scan value
    int calibration[4] = {0, 0, 0, 0};      // Valid after successful load/calibration

    // Helpers
    bool selectMux();
    bool deselectMux();
};

#endif
