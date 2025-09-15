#include "RotaryEncoder.h"

RotaryEncoder::RotaryEncoder(uint8_t address)
    : i2cAddress(address), lastPosition(0) {}

void RotaryEncoder::begin() {
    ano.begin(i2cAddress);

    // Initialize ANO pins
    ano.pinMode(SWITCH_SELECT, INPUT_PULLUP);
    ano.pinMode(SWITCH_UP,     INPUT_PULLUP);
    ano.pinMode(SWITCH_LEFT,   INPUT_PULLUP);
    ano.pinMode(SWITCH_DOWN,   INPUT_PULLUP);
    ano.pinMode(SWITCH_RIGHT,  INPUT_PULLUP);

    // Get starting encoder position
    lastPosition = ano.getEncoderPosition();

    // Enable Interrupts
    ano.enableEncoderInterrupt();
    ano.setGPIOInterrupts((uint32_t)1 << SWITCH_UP, 1);
}

int32_t RotaryEncoder::getPosition() {
    return ano.getEncoderPosition();
}

bool RotaryEncoder::isPressed(uint8_t button) {
    return !ano.digitalRead(button);
}