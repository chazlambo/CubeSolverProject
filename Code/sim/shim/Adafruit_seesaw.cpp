#include "Adafruit_seesaw.h"
#include "SimHost.h"

namespace {
const uint8_t PIN_SELECT = 1;
const uint8_t PIN_UP     = 2;
const uint8_t PIN_LEFT   = 3;
const uint8_t PIN_DOWN   = 4;
const uint8_t PIN_RIGHT  = 5;

bool pressed(uint8_t pin) {
    switch (pin) {
        case PIN_SELECT: return sim::keySelect();
        case PIN_UP:     return sim::keyUp();
        case PIN_LEFT:   return sim::keyLeft();
        case PIN_DOWN:   return sim::keyDown();
        case PIN_RIGHT:  return sim::keyRight();
        default:         return false;
    }
}
}  // namespace

bool Adafruit_seesaw::digitalRead(uint8_t pin) {
    return !pressed(pin);          // pulled up: pressed == LOW
}

uint32_t Adafruit_seesaw::digitalReadBulk(uint32_t pins) {
    // One sample of every button at one instant, which is the whole point of
    // the bulk read on the real part.
    uint32_t raw = 0xFFFFFFFFu;
    if (pressed(PIN_SELECT)) raw &= ~(1u << PIN_SELECT);
    if (pressed(PIN_UP))     raw &= ~(1u << PIN_UP);
    if (pressed(PIN_LEFT))   raw &= ~(1u << PIN_LEFT);
    if (pressed(PIN_DOWN))   raw &= ~(1u << PIN_DOWN);
    if (pressed(PIN_RIGHT))  raw &= ~(1u << PIN_RIGHT);
    (void)pins;
    return raw;
}

int32_t Adafruit_seesaw::getEncoderPosition() {
    return sim::encoderPosition();
}
