#include "CubeFaultLog.h"

CubeFaultLog cubeFaultLog;

// Indexed by Source. Short on purpose: the row is "when  source<TAB>code" in
// a 40-character line, and the code carries the detail.
const char* const CubeFaultLog::kSourceNames[CubeFaultLog::SourceCount] = {
    "Scan", "Solve", "Cal", "Mode", "Jog"
};

// Byte-at-a-time rather than EEPROM.get(), matching CubeTuning: the layout is
// then identical on the Teensy and on the host that runs the simulator, and
// does not quietly depend on either one's endianness.
uint16_t CubeFaultLog::readU16(int a) const {
    return (uint16_t)(((uint16_t)EEPROM.read(a + 1) << 8) | EEPROM.read(a));
}

void CubeFaultLog::writeU16(int a, uint16_t v) {
    EEPROM.update(a,     (uint8_t)(v & 0xFF));
    EEPROM.update(a + 1, (uint8_t)(v >> 8));
}

uint32_t CubeFaultLog::readU32(int a) const {
    uint32_t v = 0;
    for (int b = 0; b < 4; ++b) v |= (uint32_t)EEPROM.read(a + b) << (8 * b);
    return v;
}

void CubeFaultLog::writeU32(int a, uint32_t v) {
    for (int b = 0; b < 4; ++b) EEPROM.update(a + b, (uint8_t)(v >> (8 * b)));
}

bool CubeFaultLog::isValid() const {
    if (readU16(baseAddr)     != kMagic)   return false;
    if (readU16(baseAddr + 2) != kVersion) return false;
    // Sanity on the ring indices too: a header whose numbers point outside
    // the ring is corruption, and reading through it would serve garbage
    // entries under a valid magic.
    if (EEPROM.read(baseAddr + 4) >= kCapacity) return false;
    if (EEPROM.read(baseAddr + 5) >  kCapacity) return false;
    return true;
}

uint8_t CubeFaultLog::count() const {
    return isValid() ? EEPROM.read(baseAddr + 5) : 0;
}

void CubeFaultLog::append(uint32_t upSec, uint16_t code, uint8_t source) {
    uint8_t head = 0, cnt = 0;
    if (isValid()) {
        head = EEPROM.read(baseAddr + 4);
        cnt  = EEPROM.read(baseAddr + 5);
    }

    // Entry bytes FIRST, ring indices after, magic last — the mirror image of
    // CubeTuning's values-first-header-last save, for the same reason: a
    // power cut mid-append garbles at most the ONE entry being written, and
    // the header never points at half-written values. The magic only changes
    // on first use; after that update() re-writes it for free.
    const int a = entryAddr(head);
    writeU32(a,     upSec);
    writeU16(a + 4, code);
    EEPROM.update(a + 6, source);
    EEPROM.update(a + 7, 0);            // spare, reserved

    EEPROM.update(baseAddr + 4, (uint8_t)((head + 1) % kCapacity));
    EEPROM.update(baseAddr + 5, (uint8_t)(cnt < kCapacity ? cnt + 1 : kCapacity));
    writeU16(baseAddr + 2, kVersion);
    writeU16(baseAddr,     kMagic);
}

bool CubeFaultLog::get(uint8_t i, Entry& out) const {
    if (i >= count()) return false;     // count() is 0 when the block is invalid

    // head is where the NEXT entry lands, so the newest sits one behind it.
    const uint8_t head = EEPROM.read(baseAddr + 4);
    const uint8_t slot = (uint8_t)((head + kCapacity - 1 - i) % kCapacity);

    const int a = entryAddr(slot);
    out.upSec  = readU32(a);
    out.code   = readU16(a + 4);
    out.source = EEPROM.read(a + 6);
    out.spare  = EEPROM.read(a + 7);
    return true;
}

void CubeFaultLog::clear() {
    writeU16(baseAddr, 0);
}
