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
    // Forwarded, not duplicated: CubeDisplay owns the chip palette and its
    // order, so it owns what a colour letter means.
    return CubeDisplay::chipIndexForColor(c);
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

// How the solved cube has to be loaded for any of the above to be true.
//
// calibrateColorSensors() does not look at what it is seeing — it ASSERTS that
// rotation 1 is showing it Red and Green, rotation 2 Blue and Red, and so on,
// and writes whatever the sensors return under those names. Load the cube
// turned the wrong way and it learns wrong colours and saves them to EEPROM,
// with nothing to notice. So the orientation is not advice, it is part of the
// procedure, and this is it.
//
// NOTE the tables above are COLOUR labels, not positions. Nothing in
// calibrateColorSensors() names a face except in its comments, so the colour
// scanner being rotated 90 degrees (see scanCube()) did not require the tables
// to change — it required the CUBE to be loaded 90 degrees round to match. That
// is the whole of the fix, and this constant is where it lives.
//
// Derivation, which needs no assumption about rotation direction:
//
//   The sensors moved one 90-degree step: sensor 1 Left->Back, sensor 2
//   Back->Right. The old procedure wanted Red at Left and Green at Back, so the
//   cube turns the same step: Red to Back, Green to Right. Opposites then pin
//   two more — Orange to Front, Blue to Left.
//
//   Up and Down are the remaining pair. Step 1 never sees them: every rotation
//   there is a ROTZ, which leaves Up and Down alone. They are only read in
//   step 3, after the re-grip in step 2. Across that re-grip Left and Right are
//   unchanged (Red and Orange both before and after), so it turns about the
//   Left-Right axis — which is exactly what rotOrientX describes, and it has
//   new Front = old Up. Step 3 opens with White at Front, so Up is White.
//
// Every opposite pair checks out (W/Y, G/B, O/R), so this is a real cube.
//
// The Up/Down half is the one part resting on the machine's ROTX matching
// rotOrientX rather than being its mirror. The Left/Right invariance across the
// re-grip is good evidence that it does, but if a calibration ever comes out
// with White and Yellow swapped, this is the line to turn over.
const char CubeSystem::kCalStartFacelets[55] =
    "WWWWWWWWW"   // U  White
    "GGGGGGGGG"   // R  Green
    "OOOOOOOOO"   // F  Orange
    "YYYYYYYYY"   // D  Yellow
    "BBBBBBBBB"   // L  Blue
    "RRRRRRRRR";  // B  Red

const char* const CubeSystem::kCalStartText = "White up, Orange front, Blue left";
