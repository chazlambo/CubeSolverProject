// =============================================================================
//  SimHost — the SDL side of the desktop simulator
// =============================================================================
//
//  Owns the window, the RGB565 panel buffer, and the keyboard/wheel state that
//  the Adafruit_seesaw shim reports as button presses.
//
//  Nothing in the firmware knows this file exists. It is reached through the
//  shims that stand in for the parts of the machine the PC does not have —
//  ILI9341_T4 (the panel), Adafruit_seesaw (the wheel), Arduino.cpp's
//  millis()/delay() — and by the simulator's own main.cpp and CubeSystemSim.cpp.
// =============================================================================

#ifndef SimHost_h
#define SimHost_h

#include <stdint.h>

namespace sim {

// Must match CubeDisplay's LX/LY. The panel is the thing being simulated; if
// these disagree the blit geometry is wrong and it will be obvious.
const int kPanelW = 320;
const int kPanelH = 240;

// Integer upscale for the window. 320x240 is unreadable on a modern monitor,
// and nearest-neighbour scaling keeps pixel boundaries crisp so layout work is
// judged on real pixels rather than on a blur.
void setScale(int scale);

bool init();          // called from ILI9341Driver::begin()
void shutdown();

// Drain SDL's event queue.
//
// Called from the main loop, from CubeSystem::pumpTick(), AND from the shim's
// delay(). That last one matters: several firmware paths busy-wait on a button
// with delay(10) between reads and never return to loop(), so without pumping
// there the window would freeze and the key press could never arrive.
void pumpEvents();
bool quitRequested();

// One RGB565 region, in the same form LVGL hands to a flush callback.
void blit(const uint16_t* px, int x1, int x2, int y1, int y2);
void present();

// --- input state, read by the seesaw shim ---
bool    keySelect();
bool    keyLeft();
bool    keyRight();
bool    keyUp();
bool    keyDown();
int32_t encoderPosition();

// --- sim-only controls, read by CubeSystemSim ---
bool consumeFaultInjection();   // F: make the next operation fail
bool consumeCubeToggle();       // C: flip "a cube is loaded and scanned"
bool consumeCalToggle();        // K: flip the calibration flags

unsigned long millisNow();
void          sleepMs(unsigned long ms);

}  // namespace sim

#endif // SimHost_h
