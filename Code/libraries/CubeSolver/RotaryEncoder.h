#ifndef ROTARYENCODER_H
#define ROTARYENCODER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_seesaw.h>

class RotaryEncoder {
public:
    RotaryEncoder(TwoWire* wireBus = &Wire);

    bool begin(uint8_t addr = 0x49);

    int32_t getPosition();                // Returns current encoder count

    // Button bits returned by readButtons(). Set == pressed.
    static constexpr uint8_t BTN_SELECT = 0x01;
    static constexpr uint8_t BTN_UP     = 0x02;
    static constexpr uint8_t BTN_LEFT   = 0x04;
    static constexpr uint8_t BTN_DOWN   = 0x08;
    static constexpr uint8_t BTN_RIGHT  = 0x10;

    // All five buttons in ONE I2C transaction, sampled at one instant.
    //
    // The per-button methods below are each a separate transaction, so polling
    // all five costs five round trips to the seesaw on Wire1 — and, worse, the
    // five samples are taken at five different times. That makes a CHORD (two
    // buttons held together) unreliable to detect: the abort gesture is
    // SELECT+LEFT, and reading them milliseconds apart means a press that
    // straddles the two reads registers as neither.
    //
    // Returns 0 if the seesaw is absent, which reads as "nothing pressed".
    uint8_t readButtons();

    // Kept because the bring-up sketches use them by name, and because a single
    // button check should not have to mask a bitfield. Prefer readButtons() in
    // any loop that samples more than one.
    bool upPressed();
    bool downPressed();
    bool leftPressed();
    bool rightPressed();
    bool selectPressed();

private:
    Adafruit_seesaw ss;
    TwoWire* wire;
    uint8_t i2cAddr;

    // Button IDs for ANO encoder wheel breakout
    static constexpr int PIN_SELECT = 1;
    static constexpr int PIN_UP     = 2;
    static constexpr int PIN_LEFT   = 3;
    static constexpr int PIN_DOWN   = 4;
    static constexpr int PIN_RIGHT  = 5;
};

#endif
