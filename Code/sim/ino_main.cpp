// Compiles a sketch into the simulator.
//
// The sketches are ordinary C++ — they carry their own forward declarations
// rather than relying on the Arduino IDE's prototype injection — so including
// one here is enough. This is what guarantees the simulator exercises the
// shipping sketch and not a copy of it.
//
// Which sketch is a build option, so the menu test bench can be driven on a PC
// too. See SIM_SKETCH in CMakeLists.txt.
#ifndef SIM_SKETCH_FILE
#define SIM_SKETCH_FILE "CubeSolver.ino"
#endif

#include SIM_SKETCH_FILE
