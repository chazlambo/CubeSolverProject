// Wire.h — desktop shim. The I2C buses are not simulated; every transaction
// reports success and returns nothing.
#ifndef WIRE_H_SIM
#define WIRE_H_SIM
#include "Arduino.h"
class TwoWire {
public:
    void    begin() {}
    void    setClock(uint32_t) {}
    void    beginTransmission(uint8_t) {}
    uint8_t endTransmission() { return 0; }
    uint8_t endTransmission(bool) { return 0; }
    size_t  write(uint8_t) { return 1; }
    size_t  write(const uint8_t*, size_t n) { return n; }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    uint8_t requestFrom(int, int) { return 0; }
    int     available() { return 0; }
    int     read() { return 0; }
};
extern TwoWire Wire;
extern TwoWire Wire1;
#endif
