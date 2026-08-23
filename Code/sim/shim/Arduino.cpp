#include "Arduino.h"
#include "SimHost.h"

SerialSim Serial;

unsigned long millis() { return sim::millisNow(); }

void delay(unsigned long ms) {
    // Sliced, with an SDL pump between slices — see the note in Arduino.h.
    const unsigned long kSlice = 2;
    unsigned long start = millis();
    sim::pumpEvents();
    while (millis() - start < ms) {
        unsigned long left = ms - (millis() - start);
        sim::sleepMs(left < kSlice ? left : kSlice);
        sim::pumpEvents();
    }
}

void delayMicroseconds(unsigned long us) {
    if (us >= 1000) sim::sleepMs(us / 1000);
}

// The machine's GPIO does nothing here. Reads report a benign level rather than
// a floating one, so any firmware that samples a pin gets a stable answer.
void pinMode(int, int)      {}
void digitalWrite(int, int) {}
int  digitalRead(int)       { return HIGH; }

long random(long max)           { return max > 0 ? (long)(std::rand() % max) : 0; }
long random(long min, long max) { return max > min ? min + random(max - min) : min; }
void randomSeed(unsigned long s){ std::srand((unsigned)s); }
