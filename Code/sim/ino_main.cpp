// Compiles the real sketch into the simulator.
//
// CubeSolver.ino is ordinary C++ — it carries its own forward declarations
// rather than relying on the Arduino IDE's prototype injection — so including
// it here is enough. This is what guarantees the simulator is exercising the
// shipping sketch and not a copy of it.
#include "CubeSolver.ino"
