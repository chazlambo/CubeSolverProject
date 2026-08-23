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
    // Undefined if the seesaw is absent — the library leaves its read buffer
    // unfilled on a failed transfer — so callers must gate on begin() having
    // succeeded, as CubeSystem::pumpTick() does.
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

    // Seesaw pin IDs for the ANO encoder wheel breakout.
    //
    // These deliberately do NOT read 1,2,3,4,5 in the order the part's own
    // pinout lists them. The breakout is mounted rotated 180 degrees in its own
    // plane on the machine, so each D-pad pin sits under the OPPOSITE physical
    // button: the pin the pinout calls UP (2) is under the button the operator
    // presses as DOWN, and the same for LEFT (3) and RIGHT (5). Swapping the
    // two pairs here is the whole correction, and this is the only place that
    // knows about it — every caller, including the SELECT+LEFT abort chord and
    // the bring-up sketches, then names a physical button and gets it. Do not
    // "fix" these back into ascending order without re-mounting the board.
    //
    // SELECT sits on the axis of rotation, so it does not move. The wheel is
    // untouched on purpose: an in-plane rotation swaps the buttons but does not
    // reverse the encoder's sense of clockwise, and the bench confirms rotation
    // already reads the right way round. Inverting it here would break it.
    static constexpr int PIN_SELECT = 1;
    static constexpr int PIN_UP     = 4;   // pinout's DOWN
    static constexpr int PIN_LEFT   = 5;   // pinout's RIGHT
    static constexpr int PIN_DOWN   = 2;   // pinout's UP
    static constexpr int PIN_RIGHT  = 3;   // pinout's LEFT
};

#endif
