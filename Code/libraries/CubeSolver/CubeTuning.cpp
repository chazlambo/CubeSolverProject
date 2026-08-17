#include "CubeTuning.h"

CubeTuning cubeTuning;

// Byte-at-a-time rather than EEPROM.get(), matching CubeServo's position
// storage: the layout is then identical on the Teensy and on the host that
// runs the simulator, and does not quietly depend on either one's endianness.
uint16_t CubeTuning::readU16(int a) const {
    return (uint16_t)(((uint16_t)EEPROM.read(a + 1) << 8) | EEPROM.read(a));
}

void CubeTuning::writeU16(int a, uint16_t v) {
    EEPROM.update(a,     (uint8_t)(v & 0xFF));
    EEPROM.update(a + 1, (uint8_t)(v >> 8));
}

bool CubeTuning::isValid(uint8_t count) const {
    if (count > kMaxParams) return false;
    if (readU16(baseAddr)     != kMagic)   return false;
    if (readU16(baseAddr + 2) != kVersion) return false;
    return EEPROM.read(baseAddr + 4) == count;
}

int32_t CubeTuning::get(uint8_t i) const {
    if (i >= kMaxParams) return 0;
    const int a = baseAddr + kHeaderBytes + i * 4;
    uint32_t v = 0;
    for (int b = 0; b < 4; ++b) v |= (uint32_t)EEPROM.read(a + b) << (8 * b);
    return (int32_t)v;
}

void CubeTuning::save(const int32_t* values, uint8_t count) {
    if (!values || count > kMaxParams) return;

    // Values first, header last. A power cut mid-save then leaves the magic
    // either absent or stale-but-matching a block that is already complete —
    // never pointing at half-written values.
    for (uint8_t i = 0; i < count; ++i) {
        const int      a = baseAddr + kHeaderBytes + i * 4;
        const uint32_t v = (uint32_t)values[i];
        for (int b = 0; b < 4; ++b) EEPROM.update(a + b, (uint8_t)(v >> (8 * b)));
    }

    EEPROM.update(baseAddr + 4, count);
    writeU16(baseAddr + 2, kVersion);
    writeU16(baseAddr,     kMagic);
}

void CubeTuning::clear() {
    writeU16(baseAddr, 0);
}
