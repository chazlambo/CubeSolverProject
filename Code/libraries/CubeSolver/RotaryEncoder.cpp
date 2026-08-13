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

uint8_t RotaryEncoder::readButtons() {
    const uint32_t mask = (1UL << PIN_SELECT) | (1UL << PIN_UP) |
                          (1UL << PIN_LEFT)   | (1UL << PIN_DOWN) |
                          (1UL << PIN_RIGHT);

    // Pins are INPUT_PULLUP, so a pressed button reads LOW. digitalReadBulk()
    // returns the raw levels for the requested mask in one transfer; invert to
    // get "pressed".
    const uint32_t raw = ss.digitalReadBulk(mask);

    uint8_t out = 0;
    if (!(raw & (1UL << PIN_SELECT))) out |= BTN_SELECT;
    if (!(raw & (1UL << PIN_UP)))     out |= BTN_UP;
    if (!(raw & (1UL << PIN_LEFT)))   out |= BTN_LEFT;
    if (!(raw & (1UL << PIN_DOWN)))   out |= BTN_DOWN;
    if (!(raw & (1UL << PIN_RIGHT)))  out |= BTN_RIGHT;
    return out;
}

bool RotaryEncoder::upPressed()     { return !ss.digitalRead(PIN_UP); }
bool RotaryEncoder::downPressed()   { return !ss.digitalRead(PIN_DOWN); }
bool RotaryEncoder::leftPressed()   { return !ss.digitalRead(PIN_LEFT); }
bool RotaryEncoder::rightPressed()  { return !ss.digitalRead(PIN_RIGHT); }
bool RotaryEncoder::selectPressed() { return !ss.digitalRead(PIN_SELECT); }
