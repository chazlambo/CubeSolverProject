#include "Adafruit_seesaw.h"
#include "SimHost.h"

namespace {
// The seesaw pin that each PHYSICAL key sits on. The breakout is mounted
// rotated 180 degrees on the machine, so the D-pad pairs are swapped — see the
// long note in RotaryEncoder.h. The shim models the board as mounted rather
// than as printed, so these must stay in step with the driver's constants:
// together they cancel, the simulator's arrow keys move the UI exactly as the
// bench buttons do, and a future "tidy-up" that reverts only one of the two
// files shows up immediately as inverted arrows in the sim.
const uint8_t PIN_SELECT = 1;
const uint8_t PIN_UP     = 4;
const uint8_t PIN_LEFT   = 5;
const uint8_t PIN_DOWN   = 2;
const uint8_t PIN_RIGHT  = 3;

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
