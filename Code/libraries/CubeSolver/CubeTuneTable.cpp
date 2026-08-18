// CubeTuneTable.cpp — the tuning table and its EEPROM load/save/reset.
//
// Moved verbatim from Test_Menu so the firmware and the bench sketch share
// ONE table (see the header for why). The rows reach their owners through
// the globals CubeHardwareConfig.h declares, plus the Cube object below.

#include "CubeTuneTable.h"
#include "CubeSystem.h"   // Cube's own tunables; also pulls in
                          // CubeHardwareConfig.h for the other owners

// Defined by the sketch, not the library. Every sketch that links this
// library MUST define the global — `CubeSystem Cube;`, that exact name — the
// same contract Code/sim/main.cpp imposes on the simulator build. There is
// no opting out: the CubeSolver library is Arduino 1.0 format (no
// library.properties), so the IDE compiles and links EVERY library .cpp into
// EVERY sketch that includes any of its headers, and an unresolved `Cube`
// fails the link. The bring-up sketches that predated this file named their
// object lowercase `cube` and were renamed when it landed.
extern CubeSystem Cube;

static const char* const kITNames[6] = { "40 ms", "80 ms", "160 ms",
                                         "320 ms", "640 ms", "1280 ms" };

// THE INDEX IS THE EEPROM ADDRESS. Values are stored by position, so
// reordering or inserting rows MUST come with a CubeTuning::kVersion bump —
// without one, the next boot hands every value after the change to the wrong
// owner. Appending to the end is free (the stored count self-invalidates).
//
// Ranges on the servo rows are the full 0-270 the horn can reach. Narrowing
// them here would be a guess at this machine's geometry, and a guess that was
// too tight would stop a real endpoint being reachable — which is worse than
// one that is too wide, because the confirm gate and one-degree steps already
// stand between a wheel and a jam.
const TuneParam kTune[] = {
    // --- Top servo, indices 0-2 ---
    { "Extend", "Swings in to grip the cube", "deg", 205, 0, 270, 1,
      TP_LIVE | TP_GATE,
      []() -> int32_t { return (int32_t)topServo.extended(); },
      [](int32_t v){ topServo.setExtended((unsigned)v); }, nullptr,
      [](int32_t v){ topServo.previewRaw((unsigned)v); } },
    { "Retract", "Parks clear of a turning face", "deg", 0, 0, 270, 1,
      TP_LIVE | TP_GATE,
      []() -> int32_t { return (int32_t)topServo.retracted(); },
      [](int32_t v){ topServo.setRetracted((unsigned)v); }, nullptr,
      [](int32_t v){ topServo.previewRaw((unsigned)v); } },
    { "Sweep delay", "Per step. Higher is gentler.", "ms", 15, 1, 100, 1,
      TP_PLAIN,
      []() -> int32_t { return topServo.sweepStepDelay(); },
      [](int32_t v){ topServo.setSweepStepDelay((int)v); }, nullptr },

    // --- Bottom servo, indices 3-7 ---
    { "Extend", "Swings in to grip the cube", "deg", 260, 0, 270, 1,
      TP_LIVE | TP_GATE,
      []() -> int32_t { return (int32_t)botServo.extended(); },
      [](int32_t v){ botServo.setExtended((unsigned)v); }, nullptr,
      [](int32_t v){ botServo.previewRaw((unsigned)v); } },
    { "Retract", "Parks clear of a turning face", "deg", 0, 0, 270, 1,
      TP_LIVE | TP_GATE,
      []() -> int32_t { return (int32_t)botServo.retracted(); },
      [](int32_t v){ botServo.setRetracted((unsigned)v); }, nullptr,
      [](int32_t v){ botServo.previewRaw((unsigned)v); } },
    { "Partial", "Holds the cube centred mid-scan", "deg", 195, 0, 270, 1,
      TP_LIVE | TP_GATE,
      []() -> int32_t { return (int32_t)botServo.partialTarget(); },
      [](int32_t v){ botServo.setPartial((unsigned)v); }, nullptr,
      [](int32_t v){ botServo.previewRaw((unsigned)v); } },
    { "Eject", "Lifts the cube out to be taken", "deg", 195, 0, 270, 1,
      TP_LIVE | TP_GATE,
      []() -> int32_t { return (int32_t)botServo.ejectTarget(); },
      [](int32_t v){ botServo.setEject((unsigned)v); }, nullptr,
      [](int32_t v){ botServo.previewRaw((unsigned)v); } },
    { "Sweep delay", "Per step. Higher is gentler.", "ms", 15, 1, 100, 1,
      TP_PLAIN,
      []() -> int32_t { return botServo.sweepStepDelay(); },
      [](int32_t v){ botServo.setSweepStepDelay((int)v); }, nullptr },

    // --- Ring, indices 8-13 ---
    { "Retract", "Fully clear of the cube", "steps", 0, 0, 2000, 5,
      TP_GATE,
      []() -> int32_t { return cubeMotors.getRingRetPos(); },
      [](int32_t v){ cubeMotors.setRingRetPos((int)v); }, nullptr },
    { "Partial", "Just off the cube", "steps", 200, 0, 2000, 5,
      TP_GATE,
      []() -> int32_t { return cubeMotors.getRingPartialPos(); },
      [](int32_t v){ cubeMotors.setRingPartialPos((int)v); }, nullptr },
    { "Middle", "Clears a face but stays close", "steps", 450, 0, 2000, 5,
      TP_GATE,
      []() -> int32_t { return cubeMotors.getRingHalfPos(); },
      [](int32_t v){ cubeMotors.setRingHalfPos((int)v); }, nullptr },
    { "Extend", "Closed on the cube", "steps", 800, 0, 2000, 5,
      TP_GATE,
      []() -> int32_t { return cubeMotors.getRingExtPos(); },
      [](int32_t v){ cubeMotors.setRingExtPos((int)v); }, nullptr },
    { "Speed", "How fast the ring travels", "sps", 800, 50, 5000, 25,
      TP_PLAIN,
      []() -> int32_t { return cubeMotors.getRingSpeed(); },
      [](int32_t v){ cubeMotors.setRingSpeed((int)v); }, nullptr },
    { "Accel", "How hard it starts and stops", "sps2", 400, 50, 5000, 25,
      TP_PLAIN,
      []() -> int32_t { return cubeMotors.getRingAccel(); },
      [](int32_t v){ cubeMotors.setRingAccel((int)v); }, nullptr },

    // --- Face motors, indices 14-17 ---
    { "Step speed", "Faster solves, more missed steps", "sps", 1000, 50, 5000, 25,
      TP_PLAIN,
      []() -> int32_t { return cubeMotors.getStepSpeed(); },
      [](int32_t v){ cubeMotors.setStepSpeed((int)v); }, nullptr },
    { "Step delay", "Settle time after a face turn", "ms", 50, 0, 500, 5,
      TP_PLAIN,
      []() -> int32_t { return cubeMotors.getStepDelay(); },
      [](int32_t v){ cubeMotors.setStepDelay((int)v); }, nullptr },
    { "Rotate delay", "Settle time after a whole turn", "ms", 60, 0, 500, 5,
      TP_PLAIN,
      []() -> int32_t { return cubeMotors.getRotStepDelay(); },
      [](int32_t v){ cubeMotors.setRotStepDelay((int)v); }, nullptr },
    { "Servo delay", "Wait after every servo move", "ms", 200, 0, 1000, 10,
      TP_PLAIN,
      []() -> int32_t { return Cube.servoDelay; },
      [](int32_t v){ Cube.servoDelay = (int)v; }, nullptr },

    // --- Alignment, indices 18-21 ---
    { "Tolerance", "Counts a motor may sit off centre", "cts", 20, 1, 200, 1,
      TP_PLAIN,
      []() -> int32_t { return Cube.motorAlignmentTol; },
      [](int32_t v){ Cube.motorAlignmentTol = (int)v; }, nullptr },
    { "Align timeout", "Give up realigning after this", "ms", 500, 50, 5000, 50,
      TP_PLAIN,
      []() -> int32_t { return (int32_t)Cube.alignTimeout; },
      [](int32_t v){ Cube.alignTimeout = (unsigned long)v; }, nullptr },
    { "Home timeout", "Give up homing after this", "ms", 1000, 50, 10000, 50,
      TP_PLAIN,
      []() -> int32_t { return (int32_t)Cube.homeTimeout; },
      [](int32_t v){ Cube.homeTimeout = (unsigned long)v; }, nullptr },
    { "Debug log", "Print align error every move", "", 0, 0, 1, 1,
      TP_BOOL,
      []() -> int32_t { return Cube.debugAlignLog ? 1 : 0; },
      [](int32_t v){ Cube.debugAlignLog = (v != 0); }, nullptr },

    // --- Color, indices 22-27 ---
    // Every one of these writes BOTH boards. They are properties of how a
    // sticker is judged, not of one piece of hardware, and letting the two
    // boards drift apart would make a scan depend on which half of the cube a
    // face was read from.
    { "Scans averaged", "More is slower and less noisy", "", 1, 1, 10, 1,
      TP_PLAIN,
      []() -> int32_t { return colorSensor1.numScans; },
      [](int32_t v){ colorSensor1.numScans = (int)v; colorSensor2.numScans = (int)v; }, nullptr },
    { "Integration", "Longer sees dimmer stickers", "", 2, 0, 5, 1,
      TP_ENUM,
      []() -> int32_t { return colorSensor1.getIntegrationIndex(); },
      [](int32_t v){ colorSensor1.setIntegrationIndex((int)v);
                     colorSensor2.setIntegrationIndex((int)v); }, kITNames },
    { "Color tol", "How far off a color may read", "", 15, 1, 100, 1,
      TP_HUND,
      []() -> int32_t { return (int32_t)(colorSensor1.colorTol * 100.0f + 0.5f); },
      [](int32_t v){ colorSensor1.colorTol = v / 100.0f;
                     colorSensor2.colorTol = v / 100.0f; }, nullptr },
    { "Margin frac", "How clear the winner must be", "", 35, 1, 100, 1,
      TP_HUND,
      []() -> int32_t { return (int32_t)(colorSensor1.marginFraction * 100.0f + 0.5f); },
      [](int32_t v){ colorSensor1.marginFraction = v / 100.0f;
                     colorSensor2.marginFraction = v / 100.0f; }, nullptr },
    { "Distance frac", "Absolute distance allowed", "", 200, 10, 500, 5,
      TP_HUND,
      []() -> int32_t { return (int32_t)(colorSensor1.distanceFraction * 100.0f + 0.5f); },
      [](int32_t v){ colorSensor1.distanceFraction = v / 100.0f;
                     colorSensor2.distanceFraction = v / 100.0f; }, nullptr },
    { "Min separation", "Below this a sensor is unusable", "", 2, 1, 50, 1,
      TP_HUND,
      []() -> int32_t { return (int32_t)(colorSensor1.minUsableSeparation * 100.0f + 0.5f); },
      [](int32_t v){ colorSensor1.minUsableSeparation = v / 100.0f;
                     colorSensor2.minUsableSeparation = v / 100.0f; }, nullptr },
};

// A row added or removed without touching the frozen count in the header is
// an EEPROM layout change trying to sneak past — refuse to build.
static_assert(sizeof(kTune) / sizeof(kTune[0]) == kTuneCount,
              "kTune row count changed: update kTuneCount AND bump "
              "CubeTuning::kVersion if any existing index moved");

const TuneSection kSecTopServo = { "Top Servo",    0,  3 };
const TuneSection kSecBotServo = { "Bottom Servo", 3,  5 };
const TuneSection kSecRing     = { "Ring",         8,  6 };
const TuneSection kSecFaces    = { "Face Motors", 14,  4 };
const TuneSection kSecAlign    = { "Alignment",   18,  4 };
const TuneSection kSecColor    = { "Color",       22,  6 };

// Write every value to EEPROM. One block, so a single changed parameter costs
// the same as all of them — and EEPROM.update() means the unchanged ones cost
// no write at all.
void tuneSaveAll() {
    int32_t vals[kTuneCount];
    for (uint8_t i = 0; i < kTuneCount; ++i) vals[i] = kTune[i].get();
    cubeTuning.save(vals, kTuneCount);
}

// Apply stored values at boot, or the defaults if there is nothing to apply.
//
// set() only, never preview() — this runs before anyone is watching the
// machine, and previewing here is what used to slam the bottom gripper,
// unswept, to the eject height on every single boot (see TuneParam::preview).
//
// Called unconditionally, defaults included, so that a value's owner and the
// table cannot disagree about what the machine is set to. The bottom servo's
// partial and eject positions become PINNED as a result — they no longer track
// the extend position the way an untuned servo's do. That is the point of
// making them parameters, but it is a behaviour change and worth knowing.
void tuneLoadAll() {
    const bool stored = cubeTuning.isValid(kTuneCount);
    for (uint8_t i = 0; i < kTuneCount; ++i) {
        kTune[i].set(stored ? cubeTuning.get(i) : kTune[i].def);
    }
    Serial.print(F("Tuning: "));
    Serial.println(stored ? F("loaded from EEPROM") : F("defaults"));

    // Worth printing rather than assuming. The tuning block is appended to a
    // layout that already holds two full color-sensor calibrations, and running
    // off the end of EEPROM would not announce itself — writes past the end are
    // simply dropped, so the symptom would be tuning that never saves.
    Serial.print(F("EEPROM: "));
    Serial.print(eepromBytesUsed);
    Serial.print(F(" of "));
    Serial.print((unsigned)EEPROM.length());
    Serial.println(F(" bytes used"));
    if ((unsigned)eepromBytesUsed > EEPROM.length()) {
        Serial.println(F("ERROR: EEPROM layout overflows. Tuning will not save."));
    }
}

void tuneResetAll() {
    cubeTuning.clear();
    for (uint8_t i = 0; i < kTuneCount; ++i) kTune[i].set(kTune[i].def);
    Serial.println(F("Tuning: reset to defaults"));
}
