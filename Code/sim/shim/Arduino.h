// =============================================================================
//  Arduino.h — desktop shim
// =============================================================================
//
//  Just enough of the Arduino core for the CubeSolver library to build and run
//  on a PC. Only the pieces the firmware actually uses are here; this is not a
//  general-purpose Arduino emulation.
// =============================================================================

#ifndef ARDUINO_H_SIM
#define ARDUINO_H_SIM

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using std::abs;

#define F(x) (x)

#define HIGH 1
#define LOW  0
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2

// ---------------------------------------------------------------------------
//  String
// ---------------------------------------------------------------------------
//  Backed by std::string. The firmware uses a small slice of Arduino's String
//  API — length, substring, endsWith, indexOf, charAt — plus construction,
//  assignment and concatenation. That slice is what is implemented.
class String {
public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(int v)          { char b[24]; std::snprintf(b, sizeof(b), "%d", v);  s_ = b; }
    String(unsigned v)     { char b[24]; std::snprintf(b, sizeof(b), "%u", v);  s_ = b; }
    String(long v)         { char b[32]; std::snprintf(b, sizeof(b), "%ld", v); s_ = b; }
    String(unsigned long v){ char b[32]; std::snprintf(b, sizeof(b), "%lu", v); s_ = b; }

    unsigned    length() const { return (unsigned)s_.size(); }
    const char* c_str()  const { return s_.c_str(); }
    char        charAt(unsigned i) const { return i < s_.size() ? s_[i] : '\0'; }
    char        operator[](unsigned i) const { return charAt(i); }

    String substring(unsigned from) const {
        if (from >= s_.size()) return String();
        return String(s_.substr(from));
    }
    String substring(unsigned from, unsigned to) const {
        if (from >= s_.size() || to <= from) return String();
        if (to > s_.size()) to = (unsigned)s_.size();
        return String(s_.substr(from, to - from));
    }

    int indexOf(char c) const {
        std::string::size_type p = s_.find(c);
        return (p == std::string::npos) ? -1 : (int)p;
    }
    int indexOf(char c, unsigned from) const {
        std::string::size_type p = s_.find(c, from);
        return (p == std::string::npos) ? -1 : (int)p;
    }
    int indexOf(const char* t) const {
        std::string::size_type p = s_.find(t);
        return (p == std::string::npos) ? -1 : (int)p;
    }

    bool endsWith(const String& t) const {
        return s_.size() >= t.s_.size() &&
               s_.compare(s_.size() - t.s_.size(), t.s_.size(), t.s_) == 0;
    }
    bool startsWith(const String& t) const {
        return s_.size() >= t.s_.size() && s_.compare(0, t.s_.size(), t.s_) == 0;
    }
    bool equals(const String& o) const { return s_ == o.s_; }
    int  toInt() const { return std::atoi(s_.c_str()); }

    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* o)   { s_ += (o ? o : ""); return *this; }
    String& operator+=(char c)          { s_ += c; return *this; }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator==(const char* o)   const { return s_ == (o ? o : ""); }
    bool operator!=(const char* o)   const { return s_ != (o ? o : ""); }

    const std::string& std_str() const { return s_; }

private:
    std::string s_;
};

inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b)   { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b)   { String r(a); r += b; return r; }
inline String operator+(const String& a, char b)          { String r(a); r += b; return r; }

// ---------------------------------------------------------------------------
//  Serial -> stdout
// ---------------------------------------------------------------------------
class SerialSim {
public:
    void begin(unsigned long) {}
    void flush() { std::fflush(stdout); }
    int  available() { return 0; }
    int  read()      { return -1; }
    int  peek()      { return -1; }
    operator bool() const { return true; }

    void print(const char* s)      { std::printf("%s", s ? s : ""); }
    void print(const String& s)    { std::printf("%s", s.c_str()); }
    void print(char c)             { std::printf("%c", c); }
    void print(int v)              { std::printf("%d", v); }
    void print(int v, int)         { std::printf("%d", v); }
    void print(unsigned v)         { std::printf("%u", v); }
    void print(long v)             { std::printf("%ld", v); }
    void print(unsigned long v)    { std::printf("%lu", v); }
    void print(unsigned char v)    { std::printf("%u", (unsigned)v); }
    void print(double v)           { std::printf("%g", v); }
    void print(double v, int d)    { std::printf("%.*f", d, v); }

    void println()                 { std::printf("\n"); }
    void println(const char* s)    { std::printf("%s\n", s ? s : ""); }
    void println(const String& s)  { std::printf("%s\n", s.c_str()); }
    void println(char c)           { std::printf("%c\n", c); }
    void println(int v)            { std::printf("%d\n", v); }
    void println(int v, int)       { std::printf("%d\n", v); }
    void println(unsigned v)       { std::printf("%u\n", v); }
    void println(long v)           { std::printf("%ld\n", v); }
    void println(unsigned long v)  { std::printf("%lu\n", v); }
    void println(unsigned char v)  { std::printf("%u\n", (unsigned)v); }
    void println(double v)         { std::printf("%g\n", v); }
    void println(double v, int d)  { std::printf("%.*f\n", d, v); }

    void printf(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap);
    }
};
extern SerialSim Serial;

// ---------------------------------------------------------------------------
//  Core functions
// ---------------------------------------------------------------------------
unsigned long millis();
unsigned long micros();

// delay() pumps the SDL event queue.
//
// Several firmware paths busy-wait on a button with delay(10) between reads and
// never return to loop() — CubeDisplay::waitForSelect() is the obvious one. If
// delay() merely slept, the window would stop responding inside those loops and
// the key press being waited for could never arrive, so the simulator would
// appear to hang exactly where the machine is waiting for you.
void delay(unsigned long ms);
void delayMicroseconds(unsigned long us);

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int  digitalRead(int pin);
int  analogRead(int pin);

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    if (inMax == inMin) return outMin;
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
inline long constrain(long x, long lo, long hi) { return x < lo ? lo : (x > hi ? hi : x); }

#endif // ARDUINO_H_SIM
