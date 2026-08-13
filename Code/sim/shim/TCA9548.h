// TCA9548.h — desktop shim. Channel selection is a no-op; every mux reports
// present so the firmware's boot self-test passes.
#ifndef TCA9548_H_SIM
#define TCA9548_H_SIM
#include "Wire.h"
class TCA9548 {
public:
    TCA9548(uint8_t, TwoWire* = nullptr) {}
    bool begin() { return true; }
    bool isConnected() { return true; }
    void setChannelMask(uint8_t) {}
    bool enableChannel(uint8_t) { return true; }
    bool disableChannel(uint8_t) { return true; }
    bool selectChannel(uint8_t) { return true; }
    bool disableAllChannels() { return true; }
};
#endif
