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
