// Adafruit_seesaw.h — desktop shim.
//
// Reports the SDL keyboard as the ANO wheel breakout, so RotaryEncoder.cpp
// compiles and runs unmodified and the SELECT+LEFT abort chord behaves exactly
// as it does on the machine.
//
//   arrow up / down, mouse wheel   the wheel (encoder count)
//   Return, Space                  SELECT
//   Left arrow, Backspace          LEFT
//   Right arrow                    RIGHT
//   W / S                          the discrete UP / DOWN buttons
#ifndef SEESAW_H_SIM
#define SEESAW_H_SIM

#include "Arduino.h"
#include "Wire.h"

class Adafruit_seesaw {
public:
    Adafruit_seesaw(TwoWire* = nullptr) {}

    bool begin(uint8_t = 0x49, int8_t = -1, bool = true) { return true; }
    void pinMode(uint8_t, uint8_t) {}

    // Pin numbering matches RotaryEncoder's PIN_* constants: SELECT 1, UP 2,
    // LEFT 3, DOWN 4, RIGHT 5. Pressed reads LOW, as with INPUT_PULLUP.
    bool     digitalRead(uint8_t pin);
    uint32_t digitalReadBulk(uint32_t pins);
    int32_t  getEncoderPosition();
};
#endif
