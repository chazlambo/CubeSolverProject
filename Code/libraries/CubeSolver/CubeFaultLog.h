// CubeFaultLog.h
//
// The fault history: a ring of the last 24 faults, in EEPROM.
//
// WHAT THIS IS
// ------------
// CubeSystem::lastFault is one int in RAM — the machine remembers exactly one
// fault, and only until the power goes. This is the durable version: every
// path through the sketch's fail() appends one entry here, so "what has been
// going wrong with this machine" survives a reboot and is answerable from the
// panel instead of from memory. A ring rather than a growing list, because
// the question is always about the RECENT faults; entry 25 evicting entry 1
// is the log doing its job, not losing data.
//
// WHY A SOURCE BYTE
// -----------------
// The fault codes are small integers that collide across code spaces — the
// sketch keeps a separate text table per space for exactly this reason. A 25
// is an aborted move in the raw-move space and a rejected face in the scan
// space; a bare code in the log would be ambiguous the same way one shared
// text table would mislabel. One byte naming the space (Scan, Solve, Cal,
// Mode, Jog) keeps every stored code decodable. The Source enum is
// APPEND-ONLY: the values are in EEPROM, so reordering it would relabel
// every fault already recorded.
//
// WHY THE WRITE ORDER
// -------------------
// append() writes the entry bytes FIRST, the ring indices after, and the
// magic last (which only matters on first use). A power cut mid-append then
// garbles at most the one entry being written — the header still maps every
// other slot correctly. A header-first order could leave count pointing at a
// slot holding last year's bytes, presented as this year's fault.
//
// WHERE IT SITS IN EEPROM
// -----------------------
// Appended after the tuning block by initializeEEPROMLayout(), which hands
// out addresses sequentially — so nothing may ever be inserted above an
// existing block (that would silently shift a calibrated machine's data).

#ifndef CubeFaultLog_h
#define CubeFaultLog_h

#include <Arduino.h>
#include <EEPROM.h>

class CubeFaultLog {
public:
    // Bump on ANY change to the entry layout or the ring geometry.
    static const uint16_t kVersion = 1;

    // Arbitrary, but not 0x0000 or 0xFFFF (what erased and never-written
    // EEPROM read as), and not 0x7513 — each block gets its own constant, so
    // a block read at the wrong address can never look valid.
    static const uint16_t kMagic = 0x7F19;

    // 24 entries is a few weeks of ordinary trouble. Fixed here so the
    // block's footprint is knowable at layout time.
    static const uint8_t kCapacity = 24;

    // What one slot holds. upSec is seconds since that boot — the machine has
    // no RTC, so uptime is the only clock it can honestly report. Serialized
    // FIELD BY FIELD, never EEPROM.put(&entry): put would bake this build's
    // struct padding into the layout, which is exactly the quiet
    // host-vs-Teensy dependence the byte-at-a-time convention exists to avoid.
    struct Entry {
        uint32_t upSec;    // uptime at the fault, in seconds
        uint16_t code;     // the fault code, uninterpreted
        uint8_t  source;   // which code space — a Source value
        uint8_t  spare;    // reserved; written as 0
    };

    // Which code space a fault's code belongs to. APPEND-ONLY — see above.
    enum Source : uint8_t { Scan, Solve, Cal, Mode, Jog, SourceCount };
    static const char* const kSourceNames[SourceCount];

    // Header is magic, version, head, count. head is the slot the NEXT entry
    // will land in; count pins at kCapacity once the ring has wrapped.
    static const int kHeaderBytes = 2 + 2 + 1 + 1;
    static const int kEntryBytes  = 8;
    static const int kBlockBytes  = kHeaderBytes + kCapacity * kEntryBytes;

    // Stores the address and nothing else: it is called from
    // initializeEEPROMLayout() during another translation unit's static
    // initialization, where no hardware and no Serial exist yet.
    void begin(int address) { baseAddr = address; }

    // True if EEPROM holds a ring this build can read. False means untouched,
    // corrupt, or a different layout — the log simply reads as empty.
    bool isValid() const;

    // Entries currently stored: 0..kCapacity, and 0 whenever isValid() is not.
    uint8_t count() const;

    // Record one fault. Per append only one entry slot and the two ring-index
    // bytes actually change; the version and magic are re-written through
    // EEPROM.update(), which makes an unchanged byte free — so the wear cost
    // is ~10 bytes per fault, not the block.
    void append(uint32_t upSec, uint16_t code, uint8_t source);

    // Reads entry i, where i = 0 is the NEWEST. False if i is out of range
    // or the block is invalid.
    bool get(uint8_t i, Entry& out) const;

    // Invalidates the ring so it reads as empty. Only the magic is cleared —
    // the entries are left alone, which costs nothing and means a clear done
    // by accident is recoverable with a programmer.
    void clear();

private:
    int baseAddr = 0;

    int entryAddr(uint8_t slot) const {
        return baseAddr + kHeaderBytes + (int)slot * kEntryBytes;
    }

    uint16_t readU16(int a) const;
    void     writeU16(int a, uint16_t v);
    uint32_t readU32(int a) const;
    void     writeU32(int a, uint32_t v);
};

extern CubeFaultLog cubeFaultLog;

#endif // CubeFaultLog_h
