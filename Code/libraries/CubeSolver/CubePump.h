// CubePump.h
//
// Cooperative waiting, in its own header so the low-level drivers (CubeServo,
// ColorSensor, CubeMotors) can use it without including CubeHardwareConfig.h —
// which already includes them, and would be circular.
//
// Most of the time this machine spends "busy" is not work, it is delay().
// scanCube() blocks for roughly 25-40 s: ~5.4 s of color-sensor integration
// waits, and most of the rest servo sweeps stepping one degree every 15 ms.
// Nothing services the display or latches button presses during any of it, so
// the screen freezes on "Scanning cube..." and any press in that window is lost
// (the encoder read is an instantaneous level check, not a latched edge).
//
// pumpDelay() is a drop-in replacement for delay() that runs a registered
// callback while it waits. Substituting it for delay() recovers most of the
// responsiveness without restructuring any control flow.

#ifndef CubePump_h
#define CubePump_h

#include <Arduino.h>

// Return false to request an abort. pumpDelay() then returns false immediately
// so the caller can unwind.
typedef bool (*PumpFn)();

// nullptr => pumpDelay() behaves exactly like delay(), so bring-up sketches
// that never register a pump keep working unchanged.
extern PumpFn systemPump;

// Wait `ms`, servicing the pump. Returns false if an abort was requested.
bool pumpDelay(unsigned long ms);

// Run one pump iteration without waiting. Safe to call anywhere.
bool pumpOnce();

// Set while a motor is mid-move, to keep the DISPLAY out of the step loop.
//
// The pump does two jobs: it refreshes the panel, and it watches for the abort
// chord. Only the second one may happen while steppers are running. Refreshing
// the panel means an LVGL render and an SPI flush — tens of milliseconds during
// which nothing calls run(), so no steps come out, and a constant-speed
// MultiStepper move simply loses that time. That is torque lost mid-turn, and
// it is what makes a face jam.
//
// The abort watch is safe to keep: it is one short I2C read, already throttled
// to 25 ms, costing about a step at full speed.
//
// The display does not suffer. executeSolve() calls displayProgress() before
// every move, so the panel still repaints once per move — which is all a
// progress bar over a 20-move solution can usefully show anyway.
//
// Callers must SAVE AND RESTORE it rather than clearing it, so nested moves
// cannot hand the display back early. Same discipline as pumpAbortSuppressed.
extern bool pumpMotionOnly;

#endif // CubePump_h
