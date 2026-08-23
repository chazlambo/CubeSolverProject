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
    int       scan();                        // returns raw 12-bit angle [0-4095], or -1/-2/-3 on I2C failure

    // scan() with retry. Returns [0-4095], or -1 if the encoder could not be
    // read after `retries` extra attempts.
    //
    // ALWAYS prefer this over raw scan() in control paths, and ALWAYS check the
    // sign. A negative return is a sensor fault, not a position — consuming it
    // as one makes the alignment loop step the motor blind until its timeout,
    // roughly 45-90 degrees at full torque with the cube clamped.
    int       scanChecked(int retries = 2);

    // The AS5600's own view of its magnet: STATUS (0x0B: bit5 MD magnet
    // detected, bit4 ML too weak, bit3 MH too strong), AGC (0x1A: the gain
    // the part needed — it rails toward 0 or 255 as the magnet gets too close
    // or too far) and MAGNITUDE (0x1B-0x1C, 12 bits: field strength).
    //
    // Exists because the ANGLE register cannot distinguish a rotor that moved
    // from a magnet that moved. Magnitude and AGC can: a magnet that is loose
    // on its shaft changes its gap as it rattles, and these two change with
    // it, while a rigid magnet reads the same gap forever. Read them from a
    // diagnostics page and before/after a long run of moves, never inside a
    // step loop.
    //
    // Returns 0 on success, or the negative scan()-style I2C code.
    int       readHealth(uint8_t* status, uint8_t* agc, uint16_t* magnitude);
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
