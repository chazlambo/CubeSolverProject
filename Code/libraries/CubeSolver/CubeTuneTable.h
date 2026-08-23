// CubeTuneTable.h
//
// The tuning table: one row per tunable value, defaults included, shared by
// every sketch that tunes or runs the machine.
//
// WHAT THIS IS
// ------------
// The "table of accessors" CubeTuning.h says its caller keeps. CubeTuning is
// the drawer — an array of int32 in EEPROM and nothing else — and this is the
// contents list: each row names a value, carries its default, range and step,
// and reaches the live value through a get/set pair on whatever object owns
// it. The interactive editor that walks these rows stays in each sketch; it
// is UI, not layout, and the two sketches' screens are allowed to differ.
//
// WHY ACCESSORS, NOT GLOBALS
// --------------------------
// Every value is reached through a get/set pair rather than by writing the
// config globals. Those are read once at construction — CubeServo copies
// topExtPos and never looks at it again — so an editor that wrote them would
// show numbers changing and move nothing at all.
//
// WHY THE ORDER IS FROZEN
// -----------------------
// The array index IS the EEPROM slot. Screens are windows onto the one flat
// table (first, count), so adding a parameter to a section in the middle
// shifts every index after it and MUST come with a CubeTuning::kVersion bump
// — otherwise the next boot hands an alignment tolerance to a servo.
// Appending to the end is free.
//
// WHY ONE SHARED COPY
// -------------------
// Test_Menu tunes the machine on the bench; the firmware runs it. Give each
// sketch its own copy of this table and they can drift by a single row —
// which is exactly the values-to-the-wrong-owner corruption the version
// stamp exists to catch, except drift between copies would not bump the
// version. A machine tuned from the bench sketch must keep that tuning under
// the firmware, and one shared table is the only arrangement that can
// promise it.

#ifndef CubeTuneTable_h
#define CubeTuneTable_h

#include <stdint.h>

struct TuneParam {
    const char*        name;
    const char*        help;     // one line, shown while the row is selected
    const char*        units;
    int32_t            def;
    int32_t            lo, hi, step;
    uint8_t            flags;
    int32_t          (*get)();
    void             (*set)(int32_t);
    const char* const* names;    // TP_ENUM only

    // Moves the hardware to show the value being edited. SEPARATE from set()
    // on purpose: tuneLoadAll() and tuneResetAll() call set() for every row,
    // and when the servo rows' setters embedded the preview, every boot ended
    // in an unswept servo.write() slam to the eject height — full slew, no
    // sweep, possibly with a cube in the bay — and Reset Defaults moved both
    // horns while its own screen promised it would not. set() stores; the
    // EDITORS call preview() after it, which is the only place a human is
    // watching the part move. Null for rows that move nothing.
    void             (*preview)(int32_t);
};

static const uint8_t TP_PLAIN = 0x00;
static const uint8_t TP_LIVE  = 0x01;   // moves hardware as the wheel turns
static const uint8_t TP_GATE  = 0x02;   // confirm before entering edit
static const uint8_t TP_HUND  = 0x04;   // stored in hundredths, shown as 0.15
static const uint8_t TP_BOOL  = 0x08;   // Off / On
static const uint8_t TP_ENUM  = 0x10;   // index into names[]

// Frozen here, not computed from the table: the count is part of the EEPROM
// contract (CubeTuning stores it and rejects a block whose count disagrees),
// and a table edit that changes it should fail the static_assert in
// CubeTuneTable.cpp loudly rather than shift the layout quietly.
constexpr uint8_t kTuneCount = 29;

extern const TuneParam kTune[];

// A screen is a window onto the flat table.
struct TuneSection { const char* title; uint8_t first, count; };
extern const TuneSection kSecTopServo;
extern const TuneSection kSecBotServo;
extern const TuneSection kSecRing;
extern const TuneSection kSecFaces;
extern const TuneSection kSecAlign;
extern const TuneSection kSecColor;

// Boot, save, reset. tuneLoadAll() runs once at startup, AFTER
// CubeSystem::begin() — the owners it pushes into have to exist and have
// finished their own begin() first; Test_Menu's begin-then-load order is the
// proven one. tuneSaveAll() writes the whole block; call it when leaving an
// editor, never per detent. tuneResetAll() invalidates the block and
// re-applies compiled defaults to the owners.
void tuneLoadAll();
void tuneSaveAll();
void tuneResetAll();

#endif // CubeTuneTable_h
