// EEPROM.h — desktop shim, backed by a file.
//
// Persisting to disk rather than to RAM is deliberate: the calibration flags
// and the servo positions are read at startup, so a RAM-only EEPROM would make
// every simulator run look like a factory-fresh machine and the "already
// calibrated" paths would never be exercised.
//
// The backing file is sim-eeprom.bin in the working directory. Delete it to
// simulate a virgin board.
#ifndef EEPROM_H_SIM
#define EEPROM_H_SIM

#include "Arduino.h"

class EEPROMSim {
public:
    static const int kSize = 4284;   // Teensy 4.1's emulated EEPROM size

    uint8_t  read(int addr);
    void     write(int addr, uint8_t value);
    void     update(int addr, uint8_t value);
    unsigned length() const { return kSize; }

    template <typename T> T& get(int addr, T& t) {
        uint8_t* p = reinterpret_cast<uint8_t*>(&t);
        for (unsigned i = 0; i < sizeof(T); ++i) p[i] = read(addr + (int)i);
        return t;
    }
    template <typename T> const T& put(int addr, const T& t) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&t);
        for (unsigned i = 0; i < sizeof(T); ++i) update(addr + (int)i, p[i]);
        return t;
    }
};
extern EEPROMSim EEPROM;
#endif
