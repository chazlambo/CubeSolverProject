#ifndef RotaryEncoder_h
#define RotaryEncoder_h

#include <Arduino.h>
#include <Adafruit_seesaw.h>

class RotaryEncoder {
public:
    RotaryEncoder(uint8_t address = 0x4E);

    void begin();
    int32_t getPosition();
    bool isPressed(uint8_t button);  // button: 1 = select, 2 = up, etc.

private:
    Adafruit_seesaw ano;
    uint8_t i2cAddress;
    int32_t lastPosition;

    static constexpr uint8_t SWITCH_SELECT = 1;
    static constexpr uint8_t SWITCH_UP     = 2;
    static constexpr uint8_t SWITCH_LEFT   = 3;
    static constexpr uint8_t SWITCH_DOWN   = 4;
    static constexpr uint8_t SWITCH_RIGHT  = 5;
};

#endif
