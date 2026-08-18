#include "CubeStats.h"

CubeStats cubeStats;

// Byte-at-a-time rather than EEPROM.get(), matching CubeTuning: the layout is
// then identical on the Teensy and on the host that runs the simulator, and
// does not quietly depend on either one's endianness.
uint16_t CubeStats::readU16(int a) const {
    return (uint16_t)(((uint16_t)EEPROM.read(a + 1) << 8) | EEPROM.read(a));
}

void CubeStats::writeU16(int a, uint16_t v) {
    EEPROM.update(a,     (uint8_t)(v & 0xFF));
    EEPROM.update(a + 1, (uint8_t)(v >> 8));
}

bool CubeStats::isValid() const {
    if (readU16(baseAddr)     != kMagic)   return false;
    if (readU16(baseAddr + 2) != kVersion) return false;
    // One-sided on purpose — see the header essay. A count beyond the fixed
    // footprint is corruption; a count merely smaller than today's FieldCount
    // is an older block whose missing fields read as zero.
    return EEPROM.read(baseAddr + 4) <= kMaxFields;
}

uint32_t CubeStats::get(uint8_t f) const {
    if (f >= kMaxFields) return 0;
    // A field appended since this block was last saved is not in it yet, and
    // zero is exactly what a fresh counter holds.
    if (f >= EEPROM.read(baseAddr + 4)) return 0;
    const int a = baseAddr + kHeaderBytes + f * 4;
    uint32_t v = 0;
    for (int b = 0; b < 4; ++b) v |= (uint32_t)EEPROM.read(a + b) << (8 * b);
    return v;
}

void CubeStats::save(const uint32_t* values, uint8_t count) {
    if (!values || count > kMaxFields) return;

    // Values first, header last, exactly as CubeTuning saves. Be honest about
    // what that buys: it protects only a block that has never yet been valid —
    // once one save has completed, the magic already matches during every
    // later save, so a power cut mid-values can tear individual counters
    // under a valid header. Accepted: these are advisory numbers, a torn one
    // is one bad row on a status screen, and the alternative (a double-buffer
    // or a per-save invalidate) would spend wear and complexity guarding
    // bragging rights.
    for (uint8_t i = 0; i < count; ++i) {
        const int      a = baseAddr + kHeaderBytes + i * 4;
        const uint32_t v = values[i];
        for (int b = 0; b < 4; ++b) EEPROM.update(a + b, (uint8_t)(v >> (8 * b)));
    }

    EEPROM.update(baseAddr + 4, count);
    writeU16(baseAddr + 2, kVersion);
    writeU16(baseAddr,     kMagic);
}

void CubeStats::clear() {
    writeU16(baseAddr, 0);
}
