// CubeOpShape.cpp — the shape of the machine's long operations.
//
// Split out of CubeSystem.cpp on purpose. The desktop simulator replaces
// CubeSystem.cpp wholesale with its own fake implementations, but it still
// compiles this file, so the scan pass labels and the calibration colour order
// are defined exactly once for both builds. Putting them in CubeSystem.cpp
// would have forced the simulator to keep a second copy, which is precisely the
// kind of duplicate that drifts and then makes the simulator lie.
#include "CubeSystem.h"

const char* const CubeSystem::kScanPassLabels[CubeSystem::kScanPasses] = {
    "Left + Back",
    "Up + Front",
    "Down + Right",
};

// Scan order is [L, B], [U, F], [D, R] — see scanCube(). As face-row indices,
// where the row reads U R F D L B: L=4 B=5, U=0 F=2, D=3 R=1.
const uint8_t CubeSystem::kScanPassFaces[CubeSystem::kScanPasses][2] = {
    { 4, 5 },
    { 0, 2 },
    { 3, 1 },
};

int8_t CubeSystem::chipIndexForColor(char c) {
    switch (c) {
    case 'W': return 0;
    case 'Y': return 1;
    case 'R': return 2;
    case 'O': return 3;
    case 'G': return 4;
    case 'B': return 5;
    default:  return -1;    // 'U' — the sensors' "unknown"
    }
}

// From calibrateColorSensors(): faceColors then topFaces, mapped to chip
// indices. Between them each board sees all six colours.
//   side: {R,G} {B,R} {O,B} {G,O}
//   top:  {Y,O} {R,Y} {W,R} {O,W}
const uint8_t CubeSystem::kCalSideColors[CubeSystem::kCalSideRots][2] = {
    { 2, 4 },   // Red   / Green
    { 5, 2 },   // Blue  / Red
    { 3, 5 },   // Orange/ Blue
    { 4, 3 },   // Green / Orange
};

const uint8_t CubeSystem::kCalTopColors[CubeSystem::kCalTopRots][2] = {
    { 1, 3 },   // Yellow/ Orange
    { 2, 1 },   // Red   / Yellow
    { 0, 2 },   // White / Red
    { 3, 0 },   // Orange/ White
};
