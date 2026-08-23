// CubePump.cpp
#include "CubePump.h"

PumpFn systemPump = nullptr;
bool   pumpMotionOnly = false;

bool pumpOnce() {
    if (systemPump == nullptr) return true;
    return systemPump();
}

bool pumpDelay(unsigned long ms) {
    // No pump registered: behave exactly like delay().
    if (systemPump == nullptr) {
        delay(ms);
        return true;
    }

    const unsigned long start = millis();

    // lv_conf.h sets LV_DEF_REFR_PERIOD to 33 ms. Pumping well under that keeps
    // the display smooth, and the input read inside the pump has its own 25 ms
    // throttle, so this does not spin the I2C bus.
    const unsigned long kPumpInterval = 5;

    // Pump at least once even for a zero-length wait, so pumpDelay(0) works as
    // a yield point.
    if (!pumpOnce()) return false;

    while (millis() - start < ms) {
        unsigned long elapsed = millis() - start;
        if (elapsed >= ms) break;

        unsigned long remaining = ms - elapsed;
        delay(remaining < kPumpInterval ? remaining : kPumpInterval);

        if (!pumpOnce()) return false;
    }

    return true;
}
