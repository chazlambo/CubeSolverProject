#include "EEPROM.h"

EEPROMSim EEPROM;

namespace {
const char* kPath = "sim-eeprom.bin";
uint8_t     g_cell[EEPROMSim::kSize];
bool        g_loaded = false;
bool        g_dirty  = false;

void loadOnce() {
    if (g_loaded) return;
    g_loaded = true;
    // 0xFF is what erased flash reads as, which is what the firmware's
    // "uninitialised" checks (currentPos > 270, calibration flag mismatches)
    // are written against.
    std::memset(g_cell, 0xFF, sizeof(g_cell));
    if (FILE* f = std::fopen(kPath, "rb")) {
        std::fread(g_cell, 1, sizeof(g_cell), f);
        std::fclose(f);
    }
}

void flush() {
    if (!g_dirty) return;
    if (FILE* f = std::fopen(kPath, "wb")) {
        std::fwrite(g_cell, 1, sizeof(g_cell), f);
        std::fclose(f);
        g_dirty = false;
    }
}
}  // namespace

uint8_t EEPROMSim::read(int addr) {
    loadOnce();
    if (addr < 0 || addr >= kSize) return 0xFF;
    return g_cell[addr];
}

void EEPROMSim::write(int addr, uint8_t value) {
    loadOnce();
    if (addr < 0 || addr >= kSize) return;
    g_cell[addr] = value;
    g_dirty = true;
    // Written through immediately. The simulator is usually killed with the
    // window close button or Ctrl-C, so anything buffered until exit would be
    // lost most of the time.
    flush();
}

void EEPROMSim::update(int addr, uint8_t value) {
    if (read(addr) != value) write(addr, value);
}
