// CubeTuning.h
//
// Persistence for tuned values: defaults in the source, overrides in EEPROM.
//
// WHAT THIS IS NOT
// ----------------
// It does not own the values. Every tunable already lives somewhere that has a
// reason to own it — a servo's endpoints belong to the CubeServo, the alignment
// tolerance belongs to the CubeSystem — and moving them here would mean every
// read going through a lookup for no gain.
//
// So this stores an ARRAY OF INT32 by index and nothing else. The caller keeps
// a table of accessors alongside it, pushes loaded values into their owners at
// boot, and pulls them back out to save. The table is the interesting part and
// it lives with the screen that edits it; this is just the drawer.
//
// WHY A VERSION STAMP
// -------------------
// Values are stored BY POSITION, so inserting a parameter in the middle would
// otherwise hand every value after it to the wrong owner — an alignment
// tolerance read as a servo angle, silently, on the next boot. The stamp makes
// that case fall back to defaults instead. Bump kVersion whenever the table's
// order or length changes. Losing someone's tuning is a bad day; driving a
// servo to a step count is a broken machine.
//
// WHERE IT SITS IN EEPROM
// -----------------------
// After every calibration block, and nothing may ever be inserted above it.
// initializeEEPROMLayout() hands out addresses sequentially, so an insertion
// shifts every address after it — which would quietly reinterpret an existing
// machine's motor and color calibration as garbage. Blocks added since (the
// fault log, then the stats counters) are APPENDED after this one, for the
// same reason.

#ifndef CubeTuning_h
#define CubeTuning_h

#include <Arduino.h>
#include <EEPROM.h>

class CubeTuning {
public:
    // Bump on ANY change to the order or length of the caller's table.
    static const uint16_t kVersion = 1;

    // Arbitrary, but not 0x0000 or 0xFFFF: those are what erased and
    // never-written EEPROM read as, and a stamp that matches blank memory
    // would declare garbage valid.
    static const uint16_t kMagic = 0x7513;

    // Upper bound on the table, so the block's footprint is knowable at layout
    // time rather than after the table is written. Raising it moves nothing —
    // the block is last — but it does cost EEPROM whether used or not.
    static const uint8_t kMaxParams = 48;

    // Header is magic, version, count. The count is stored so a table that
    // SHRANK is caught too; the version alone would not notice.
    static const int kHeaderBytes = 2 + 2 + 1;
    static const int kBlockBytes  = kHeaderBytes + kMaxParams * 4;

    void begin(int address) { baseAddr = address; }

    // True if EEPROM holds a block this build can read. False means untouched,
    // corrupt, or written by a different table — the caller should apply its
    // defaults and NOT save until the operator changes something.
    bool isValid(uint8_t count) const;

    // Reads value `i`. Only meaningful when isValid() agreed.
    int32_t get(uint8_t i) const;

    // Writes the whole block, header included. Uses EEPROM.update() throughout,
    // so re-saving an unchanged value costs no write.
    void save(const int32_t* values, uint8_t count);

    // Invalidates the block so the next boot falls back to defaults. Only the
    // magic is cleared — the values are left alone, which costs nothing and
    // means a reset done by accident is recoverable with a programmer.
    void clear();

private:
    int baseAddr = 0;

    uint16_t readU16(int a) const;
    void     writeU16(int a, uint16_t v);
};

extern CubeTuning cubeTuning;

#endif // CubeTuning_h
