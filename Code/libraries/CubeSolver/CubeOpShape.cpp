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
    "Back + Right",
    "Left + Down",
    "Up + Front",
};

// Which face each sensor reads on each pass, as indices into the display's
// face row (U R F D L B). Sensor 1 first, sensor 2 second.
//
// The physical scan order is [Back, Right], [Left, Down], [Up, Front]. This
// table exists ONLY to fill the panel's face chips as the scan runs. The solver
// does not use it and cannot: scanCube() records faces in scan order and works
// out which logical face each one is from its centre colour and its neighbour
// (scanFaceColor / scanLeftColor / setOrientation), so a wrong entry here
// misdraws the panel and changes nothing else.
//
// Which also means this table is the ONE place that tracks the scanner's
// physical orientation. It was [L,B] [U,F] [D,R] until the scanner was rotated
// 90 degrees in CAD to make the assembly fit; that change needed no firmware at
// all, and if the scanner moves again this is the only thing to update. See the
// note at the top of the scan loop in CubeSystem.cpp.
//
// If a pair ever comes up with its two colours swapped on screen, the sensors
// are the other way round for that pass — swap the entry, not the sequence.
const uint8_t CubeSystem::kScanPassFaces[CubeSystem::kScanPasses][2] = {
    { 5, 1 },   // Back  + Right
    { 4, 3 },   // Left  + Down
    { 0, 2 },   // Up    + Front
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
