// =============================================================================
//  Desktop simulator entry point
// =============================================================================
//
//  Stands in for the Arduino runtime: call setup() once, then loop() forever.
//  Both come from the real CubeSolver.ino, which is compiled into this binary
//  by ino_main.cpp.
// =============================================================================

#include "SimHost.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

// From the sketch.
void setup();
void loop();

// From CubeSystemSim.cpp — applies the C and K hotkeys between passes of the
// sketch's loop, so machine state can be flipped without waiting out a scan.
class CubeSystem;
extern CubeSystem Cube;
void simApplyHotkeys(CubeSystem& cube);

static void printHelp() {
    std::printf(
        "\n"
        "  CubeSolver menu simulator\n"
        "  -------------------------\n"
        "  The real firmware, with the panel and the wheel replaced.\n"
        "\n"
        "  MACHINE CONTROLS\n"
        "    up / down arrow      turn the wheel\n"
        "    mouse wheel          turn the wheel\n"
        "    W / S                the discrete UP / DOWN buttons\n"
        "    Return or Space      SELECT\n"
        "    left arrow / Backsp  LEFT  (back)\n"
        "    right arrow          RIGHT (also enters, same as the firmware)\n"
        "    Return + left, 1.5s  abort the running operation\n"
        "\n"
        "  SIMULATOR CONTROLS\n"
        "    F                    arm a fault: the next operation fails\n"
        "    C                    toggle whether a cube is loaded and scanned\n"
        "    K                    toggle the calibration flags\n"
        "    M                    print the LVGL heap report\n"
        "    P                    screenshot to sim-shot-NN.bmp\n"
        "    Esc / close window   quit\n"
        "\n"
        "  EEPROM persists in ./sim-eeprom.bin - delete it for a virgin board.\n"
        "\n");
}

int main(int argc, char** argv) {
    int scale = 3;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = std::atoi(argv[++i]);
        } else if (std::strncmp(argv[i], "--scale=", 8) == 0) {
            scale = std::atoi(argv[i] + 8);
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printHelp();
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s (try --help)\n", argv[i]);
            return 2;
        }
    }

    sim::setScale(scale);
    printHelp();

    // SDL comes up inside ILI9341Driver::begin(), which setup() reaches through
    // CubeSystem::begin() -> cubeDisplay.begin(). Nothing here creates a window.
    setup();

    while (!sim::quitRequested()) {
        sim::pumpEvents();
        simApplyHotkeys(Cube);
        loop();

        // The firmware's loop() is free-running on a 600 MHz Teensy. Spinning
        // it flat out here would peg a desktop core to no purpose — LVGL only
        // redraws every LV_DEF_REFR_PERIOD (33 ms) anyway.
        sim::sleepMs(2);
    }

    sim::shutdown();
    std::printf("[sim] closed\n");
    return 0;
}
