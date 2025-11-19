#include "RotaryEncoder.h"

RotaryEncoder::RotaryEncoder(TwoWire* wireBus)
: ss(wireBus), wire(wireBus), i2cAddr(0x49) {}

bool RotaryEncoder::begin(uint8_t addr) {
    i2cAddr = addr;

    wire->begin();
    wire->setClock(100000);
    delay(5);

    if (!ss.begin(i2cAddr)) {
        return false;
    }

    ss.pinMode(PIN_UP,     INPUT_PULLUP);
    ss.pinMode(PIN_DOWN,   INPUT_PULLUP);
    ss.pinMode(PIN_LEFT,   INPUT_PULLUP);
    ss.pinMode(PIN_RIGHT,  INPUT_PULLUP);
    ss.pinMode(PIN_SELECT, INPUT_PULLUP);

    return true;
}

int32_t RotaryEncoder::getPosition() {
    return ss.getEncoderPosition();
}

bool RotaryEncoder::upPressed()     { return !ss.digitalRead(PIN_UP); }
bool RotaryEncoder::downPressed()   { return !ss.digitalRead(PIN_DOWN); }
bool RotaryEncoder::leftPressed()   { return !ss.digitalRead(PIN_LEFT); }
bool RotaryEncoder::rightPressed()  { return !ss.digitalRead(PIN_RIGHT); }
bool RotaryEncoder::selectPressed() { return !ss.digitalRead(PIN_SELECT); }
