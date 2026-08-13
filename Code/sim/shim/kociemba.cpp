#include "kociemba.h"
#include "Arduino.h"

namespace kociemba {

void set_memory(void*, void*) {}

const char* solve(const char*, int, int, int) {
    Serial.println("[sim] kociemba::solve() is stubbed - Tier 1 does not solve cubes");
    return nullptr;      // reads to the caller as "no solution", never a wrong one
}

}
