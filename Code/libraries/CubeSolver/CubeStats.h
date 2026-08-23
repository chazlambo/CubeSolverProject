// CubeStats.h
//
// The lifetime counters behind the Stats screen: solves, times, faults.
//
// WHAT THIS IS
// ------------
// A handful of uint32 counters in EEPROM — how many solves this machine has
// run, its best and most recent times, how long it has been powered on, how
// often it has stopped on a fault. Advisory numbers, not calibration: nothing
// the machine does depends on them, so losing them is a shame, never a broken
// machine.
//
// WHY ITS OWN BLOCK
// -----------------
// Not a corner of the tuning block, although both are int32-by-index drawers.
// Solve counts change every run and tuning changes almost never, so sharing a
// block would rewrite the tuning bytes on every solve for nothing — and would
// couple the wear rate of the values that MATTER to the values that are only
// interesting.
//
// WHY THE COUNT CHECK IS ONE-SIDED
// --------------------------------
// Fields are stored BY POSITION and the Field enum is APPEND-ONLY, like the
// fault log's Source. CubeTuning demands an exact count match, because a
// tuning table that changed shape must fall back to defaults — a stale value
// in the wrong owner drives hardware. Here a stored count SMALLER than
// today's FieldCount is accepted and the missing fields read as zero: zero is
// exactly what a fresh counter holds, and discarding years of solve history
// because one counter was appended would be the worse trade. kVersion still
// exists for the change appending cannot express — a reorder.
//
// WHERE IT SITS IN EEPROM
// -----------------------
// Appended after the fault log by initializeEEPROMLayout(), which hands out
// addresses sequentially — so nothing may ever be inserted above an existing
// block (that would silently shift a calibrated machine's data).

#ifndef CubeStats_h
#define CubeStats_h

#include <Arduino.h>
#include <EEPROM.h>

class CubeStats {
public:
    // Bump on ANY reorder of the Field enum. Appending a field needs no bump
    // — see the one-sided count check above.
    static const uint16_t kVersion = 1;

    // Arbitrary, but not 0x0000 or 0xFFFF (what erased and never-written
    // EEPROM read as), and not another block's constant — each block gets its
    // own, so a block read at the wrong address can never look valid.
    static const uint16_t kMagic = 0x5CA7;

    // Upper bound on the field count, so the block's footprint is knowable at
    // layout time. Five spare slots of room to append before the layout has
    // to grow.
    static const uint8_t kMaxFields = 12;

    // The counters, in stored order. APPEND-ONLY — the values are in EEPROM.
    enum Field : uint8_t {
        Solves,         // completed machine-paced solves, Demo runs included
        UntimedSolves,  // completed Step Solves — human-paced, kept out of the times
        BestMs,         // fastest timed solve; 0 = none recorded yet
        TotalMs,        // sum of all timed solves, for the average. u32 ms
                        // wraps after ~1193 cumulative hours of solving —
                        // ~140k solves at 30 s each; accepted for advisory
                        // data rather than spending 4 bytes on a u64 split
        LastMs,         // the most recent timed solve
        RunSec,         // powered-on seconds, accumulated across boots
        Faults,         // fault screens shown, aborts included
        FieldCount
    };

    // Header is magic, version, count — the CubeTuning shape.
    static const int kHeaderBytes = 2 + 2 + 1;
    static const int kBlockBytes  = kHeaderBytes + kMaxFields * 4;

    // Stores the address and nothing else: it is called from
    // initializeEEPROMLayout() during another translation unit's static
    // initialization, where no hardware and no Serial exist yet.
    void begin(int address) { baseAddr = address; }

    // True if EEPROM holds a block this build can read. False means
    // untouched, corrupt, or reordered by a version bump — the stats simply
    // read as zeros, and nothing is saved until the first solve or fault.
    bool isValid() const;

    // Reads field `f`. Only meaningful when isValid() agreed. A field this
    // build knows but the stored block predates reads as 0.
    uint32_t get(uint8_t f) const;

    // Writes the whole block, header included. EEPROM.update() throughout, so
    // the counters that did not change this commit cost no write.
    void save(const uint32_t* values, uint8_t count);

    // Invalidates the block so it reads as zeros. Only the magic is cleared —
    // the values are left alone, which costs nothing and means a reset done
    // by accident is recoverable with a programmer.
    void clear();

private:
    int baseAddr = 0;

    uint16_t readU16(int a) const;
    void     writeU16(int a, uint16_t v);
};

extern CubeStats cubeStats;

#endif // CubeStats_h
