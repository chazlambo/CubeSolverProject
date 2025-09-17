#include "VirtualCube.h"

VirtualCube::VirtualCube()
{
  this->resetCube();
}

bool VirtualCube::isReady(){
    return cubeReady;
}

void VirtualCube::resetCube() {
  this->resetOrientation();
  this->resetColor();
  this->resetCubeArray();
  this->resetUnorientedCubeArray();
}

void VirtualCube::resetCubeArray() {
  // Sets each character in the cube array to garbage char X
  char resetArray[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
  for (int i = 0; i < 54; i++) {
    cubeArray[i] = 'X';
    colorCubeArray[i] = 'X';
  }

  cubeReady = 0;  // Sets the ready toggle to false
}

void VirtualCube::resetUnorientedCubeArray() {
  // Sets each character in the cube array to garbage char X
  char resetArray[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
  for (int i = 0; i < 54; i++) {
    unorientedCubeArray[i] = 'X';
  }

  // Set status to unbuilt
  cubeBuilt = 0;
}

void VirtualCube::resetOrientation() {
  // Sets each character in orientation array to default orientation
  char resetArray[] = "WBRYGO"; //URFDLB
  for (int i = 0; i < 6; i++) {
    orientation[i] = resetArray[i];
  }

  cubeOriented = 0; // Sets the oriented boolean to false

  this->resetCubeArray();
}

void VirtualCube::resetColor() {
  // Undefine faces
  setColorArray('W', "QQQQWQQQQ", 'G'); // Up
  setColorArray('B', "QQQQBQQQQ", 'R'); // Right
  setColorArray('R', "QQQQRQQQQ", 'G'); // Front
  setColorArray('Y', "QQQQYQQQQ", 'G'); // Down
  setColorArray('G', "QQQQGQQQQ", 'O'); // Left
  setColorArray('O', "QQQQOQQQQ", 'B'); // Back

  // Reset Set Variables.
  redSet = 0;
  orangeSet = 0;
  yellowSet = 0;
  greenSet = 0;
  blueSet = 0;
  whiteSet = 0;
  setCount = 0;

  // Set color status to unknown
  cubeColorStatus = 0;

  // Set built status to unbuilt
  cubeBuilt = 0;

  this->resetCubeArray();
}

void VirtualCube::setSolved()
{
  // Sets the cube's color faces to solved
  setColorArray('W', "WWWWWWWWW", 'G'); // Up
  setColorArray('B', "BBBBBBBBB", 'R'); // Right
  setColorArray('R', "RRRRRRRRR", 'G'); // Front
  setColorArray('Y', "YYYYYYYYY", 'G'); // Down
  setColorArray('G', "GGGGGGGGG", 'O'); // Left
  setColorArray('O', "OOOOOOOOO", 'B'); // Back

  // Set color status to solved
  cubeColorStatus = 1;
}

int VirtualCube::setColorArray(char color, char newFace[], char left)
{
  // Function to set an entire face of the cube
  // Inputs:  color - must be a capitalized valid color e.g. 'R'
  //          newFace - must be correct sized string with correct middle color e.g. "YBBWRWGRW"
  // Outputs: 0 - Ran successfully
  //          1 - Error: Middle square of newFace doesn't match color
  //          2 - Error: newFace string contained invalid characters
  //          3 - Error: Color input doesn't match a side (check uppercase)
  //          4 - Error: Left side input is not a valid color
  //          5 - Error: Left side input is impossible
  //          6 - newFace input is empty

  // Preemptive setCount update:
  setCount = redSet + orangeSet + yellowSet + greenSet + blueSet + whiteSet;
  
  // Check if newFace string is empty
  if(isblank(newFace[0])) {
    return 6;
  }

  // Check if middle square of newFace matches color
  if (newFace[4] != color) {
    return 1; // Middle square doesn't match
  }

  // Check if newFace input has any unsupported chars
  for (int i = 0; i < 9; i++) {
    if (newFace[i] != 'R' && newFace[i] != 'O' && newFace[i] != 'Y' && newFace[i] != 'G' && newFace[i] != 'B' && newFace[i] != 'W') {
      return 2; // Invalid new face input
    }
  }

  // Check if left input has any unsupported chars
  for (int i = 0; i < 9; i++) {
    if (left != 'R' && left != 'O' && left != 'Y' && left != 'G' && left != 'B' && left != 'W') {
      return 4; // Invalid new face input
    }
  }

  // Set Face
  switch (color) {
    // White
    case 'W':
      for (int i = 0; i < 9; i++) { // Assigns newFace to color array
        whiteSide[i] = newFace[i];
      }

      switch (left) { // Rotates color array for default orientation
        case 'G':
          break;
        case 'R':
          rotateColorArray(whiteSide, 1);
          break;
        case 'B':
          rotateColorArray(whiteSide, 2);
          break;
        case 'O':
          rotateColorArray(whiteSide, 3);
          break;
        case 'W':
          return 5;
        case 'Y':
          return 5;
        default:
          return 4;
      }
      whiteSet = 1; // Mark face as set
      break;

    // Blue
    case 'B':
      for (int i = 0; i < 9; i++) { // Assigns newFace to array
        blueSide[i] = newFace[i];
      }

      switch (left) { // Rotates color array for default orientation
        case 'R':
          break;
        case 'Y':
          rotateColorArray(blueSide, 1);
          break;
        case 'O':
          rotateColorArray(blueSide, 2);
          break;
        case 'W':
          rotateColorArray(blueSide, 3);
          break;
        case 'B':
          return 5;
        case 'G':
          return 5;
        default:
          return 4;
      }
      blueSet = 1;  // Mark face as set
      break;

    // Red
    case 'R':
      for (int i = 0; i < 9; i++) { // Loops through each character in the array
        redSide[i] = newFace[i];    // Assigns new value
      }
      switch (left) { // Rotates color array for default orientation
        case 'G':
          break;
        case 'Y':
          rotateColorArray(redSide, 1);
          break;
        case 'B':
          rotateColorArray(redSide, 2);
          break;
        case 'W':
          rotateColorArray(redSide, 3);
          break;
        case 'R':
          return 5;
        case 'O':
          return 5;
        default:
          return 4;
      }
      redSet = 1;
      break;

    // Yellow
    case 'Y':
      for (int i = 0; i < 9; i++) {
        yellowSide[i] = newFace[i];
      }
      switch (left) { // Rotates color array for default orientation
        case 'G':
          break;
        case 'O':
          rotateColorArray(yellowSide, 1);
          break;
        case 'B':
          rotateColorArray(yellowSide, 2);
          break;
        case 'R':
          rotateColorArray(yellowSide, 3);
          break;
        case 'Y':
          return 5;
        case 'W':
          return 5;
        default:
          return 4;
      }
      yellowSet = 1;
      break;

    // Green
    case 'G':
      for (int i = 0; i < 9; i++) {
        greenSide[i] = newFace[i];
      }
      switch (left) { // Rotates color array for default orientation
        case 'O':
          break;
        case 'Y':
          rotateColorArray(greenSide, 1);
          break;
        case 'R':
          rotateColorArray(greenSide, 2);
          break;
        case 'W':
          rotateColorArray(greenSide, 3);
          break;
        case 'G':
          return 5;
        case 'B':
          return 5;
        default:
          return 4;
      }
      greenSet = 1;
      break;

    // Orange
    case 'O':
      for (int i = 0; i < 9; i++) {
        orangeSide[i] = newFace[i];
      }
      switch (left) { // Rotates color array for default orientation
        case 'B':
          break;
        case 'Y':
          rotateColorArray(orangeSide, 1);
          break;
        case 'G':
          rotateColorArray(orangeSide, 2);
          break;
        case 'W':
          rotateColorArray(orangeSide, 3);
          break;
        case 'O':
          return 5;
        case 'R':
          return 5;
        default:
          return 4;
      }
      orangeSet = 1;
      break;


    default:
      return 3; // Error: Color does not correspond to a side. Invalid input

  }

  // Update setCount
  setCount = redSet + orangeSet + yellowSet + greenSet + blueSet + whiteSet;
  if (setCount == 6) {
    cubeColorStatus = 2;
    buildUnorientedCubeArray();
  }
  return 0;
}

int VirtualCube::copyColorArray(char color, char copyArray[]) {
  // Copies current face from specified color to new input array
  // Inputs:  color - must be a capitalized valid color e.g. 'R'
  //          copyArray - Array that the colorArray will be copied to
  // Outputs: 0 - Ran successfully
  //          1 - Error: color input does not correspond to a side. Invalid input

  // Check if color is a valid input
  if (color != 'R' && color != 'O' && color != 'Y' && color != 'G' && color != 'B' && color != 'W') {
    return 1; // Invalid color input
  }

  switch (color) {
    case 'R':
      for (int i = 0; i < 9; i++) {
        copyArray[i] = redSide[i];
      }
      break;

    case 'O':
      for (int i = 0; i < 9; i++) {
        copyArray[i] = orangeSide[i];
      }
      break;

    case 'Y':
      for (int i = 0; i < 9; i++) {
        copyArray[i] = yellowSide[i];
      }
      break;

    case 'G':
      for (int i = 0; i < 9; i++) {
        copyArray[i] = greenSide[i];
      }
      break;

    case 'B':
      for (int i = 0; i < 9; i++) {
        copyArray[i] = blueSide[i];
      }
      break;

    case 'W':
      for (int i = 0; i < 9; i++) {
        copyArray[i] = whiteSide[i];
      }
      break;
  }

  return 0;
}

void VirtualCube::rotateColorArray(char rotArray[], int turns) {
  // Rotates a color array 90 degrees CCW #turns times
  // Inputs:  turns - int for how many times you want it to turn 90deg
  //          rotArray - Array that you want rotated

  char tempArr[9];  // Placeholder array

  for (int i = 0; i < turns; i++) { // Repeat for however many turns
    // Rotate matrix 90deg CCW
    tempArr[0] = rotArray[2];
    tempArr[1] = rotArray[5];
    tempArr[2] = rotArray[8];
    tempArr[3] = rotArray[1];
    tempArr[4] = rotArray[4];
    tempArr[5] = rotArray[7];
    tempArr[6] = rotArray[0];
    tempArr[7] = rotArray[3];
    tempArr[8] = rotArray[6];

    // Assign placeholder to actual array
    for (int i = 0; i < 9; i++) {
      rotArray[i] = tempArr[i];
    }
  }
}

int VirtualCube::setFaceSquare(char color, int squarePos, char newColor)
{
  // Function to set the color of an individual square on a specified face
  // Inputs:  color - must be a capitalized valid color e.g. 'R'
  //          squarePos - must be 0-8, cannot be 4 because you can't set middle color
  //          newColor - must be a capitalized valid color e.g. 'R'
  // Outputs: 0 - Ran successfully
  //          1 - Error: color input does not correspond to a side. Invalid input
  //          2 - Error: squarePos value is not valid for this operation
  //          3 - Error: newColor input is not a valid color (check uppercase)
  //          4 - Error: Cube faces must be set before you can edit individual squares (can start with sovled cube)

  if (setCount != 6) {
    return 4;
  }

  // Check if color is a valid input
  if (color != 'R' && color != 'O' && color != 'Y' && color != 'G' && color != 'B' && color != 'W') {
    return 1; // Invalid color input
  }

  // Check if position is valid
  if (squarePos < 0 || squarePos > 9 || squarePos == 4) {
    return 2;
  }

  // Check if newColor is a valid input
  if (newColor != 'R' && newColor != 'O' && newColor != 'Y' && newColor != 'G' && newColor != 'B' && newColor != 'W') {
    return 3; // Invalid newColor input
  }

  // Set status to unbuilt
  cubeBuilt = 0;

  switch (color) {
    case 'R':
      redSide[squarePos] = newColor;
      break;

    case 'O':
      orangeSide[squarePos] = newColor;
      break;

    case 'Y':
      yellowSide[squarePos] = newColor;
      break;

    case 'G':
      greenSide[squarePos] = newColor;
      break;

    case 'B':
      blueSide[squarePos] = newColor;
      break;

    case 'W':
      whiteSide[squarePos] = newColor;
      break;

    default:
      return 1;

  }

  buildUnorientedCubeArray();
  return 0;
}

int VirtualCube::setOrientation(char leftColor, char backColor)
{
  // Function to set the orientation of the cube based on the left and back sides.
  // Inputs:  leftColor - must be a capitalized valid color e.g. 'R'
  //          backColor - must be a capitalized valid color e.g. 'W' (NOT MATCHING)
  // Outputs: 0 - Ran successfully
  //          1 - Error: leftColor input does not correspond to a side. Invalid input
  //          2 - Error: backColor input does not correspond to a side. Invalid input
  //          3 - Error: Impossible orientation detected. No solution possible.
  //          4 - Error: leftColor cannot be the same as backColor
  char up;
  char right;
  char front;
  char down;
  char left;
  char back;

  if (leftColor == backColor) {
    return 4;
  }

  // Determine left and right faces
  switch (leftColor) {
    case 'R':
      left = 'R';
      right = 'O';
      break;

    case 'O':
      left = 'O';
      right = 'R';
      break;

    case 'Y':
      left = 'Y';
      right = 'W';
      break;

    case 'G':
      left = 'G';
      right = 'B';
      break;

    case 'B':
      left = 'B';
      right = 'G';
      break;

    case 'W':
      left = 'W';
      right = 'Y';
      break;

    default:
      return 1; // Invalid leftColor
  }

  // Determine front and back faces
  switch (backColor) {
    case 'R':
      back = 'R';
      front = 'O';
      break;

    case 'O':
      back = 'O';
      front = 'R';
      break;

    case 'Y':
      back = 'Y';
      front = 'W';
      break;

    case 'G':
      back = 'G';
      front = 'B';
      break;

    case 'B':
      back = 'B';
      front = 'G';
      break;

    case 'W':
      back = 'W';
      front = 'Y';
      break;

    default:
      return 2; // Invalid backColor
  }

  // Determine top and bottom faces
  switch (leftColor) {
    case 'R':
      switch (backColor) {
        case 'G':
          up = 'W';
          down = 'Y';
          break;
        case 'W':
          up = 'B';
          down = 'G';
          break;
        case 'B':
          up = 'Y';
          down = 'W';
          break;
        case 'Y':
          up = 'G';
          down = 'B';
          break;
        default:
          return 3; // No valid solution
      }
      break;

    case 'O':
      switch (backColor) {
        case 'B':
          up = 'W';
          down = 'Y';
          break;
        case 'W':
          up = 'G';
          down = 'B';
          break;
        case 'G':
          up = 'Y';
          down = 'W';
          break;
        case 'Y':
          up = 'B';
          down = 'G';
          break;
        default:
          return 3;
      }
      break;

    case 'Y':
      switch (backColor) {
        case 'R':
          up = 'B';
          down = 'G';
          break;
        case 'B':
          up = 'O';
          down = 'R';
          break;
        case 'O':
          up = 'G';
          down = 'B';
          break;
        case 'G':
          up = 'R';
          down = 'O';
          break;
        default:
          return 3; // No valid solution
      }
      break;

    case 'G':
      switch (backColor) {
        case 'O':
          up = 'W';
          down = 'Y';
          break;
        case 'W':
          up = 'R';
          down = 'O';
          break;
        case 'R':
          up = 'Y';
          down = 'W';
          break;
        case 'Y':
          up = 'O';
          down = 'R';
          break;
        default:
          return 3; // No valid solution
      }
      break;

    case 'B':
      switch (backColor) {
        case 'R':
          up = 'W';
          down = 'Y';
          break;
        case 'W':
          up = 'O';
          down = 'R';
          break;
        case 'O':
          up = 'Y';
          down = 'W';
          break;
        case 'Y':
          up = 'R';
          down = 'O';
          break;
        default:
          return 3; // No valid solution
      }
      break;

    case 'W':
      switch (backColor) {
        case 'R':
          up = 'G';
          down = 'B';
          break;
        case 'G':
          up = 'O';
          down = 'R';
          break;
        case 'O':
          up = 'B';
          down = 'G';
          break;
        case 'B':
          up = 'R';
          down = 'O';
          break;
        default:
          return 3; // No valid solution
      }
      break;
  }

  orientation[0] = up;
  orientation[1] = right;
  orientation[2] = front;
  orientation[3] = down;
  orientation[4] = left;
  orientation[5] = back;

  cubeOriented = 1;
  return 0;
}

int VirtualCube::buildUnorientedCubeArray() {
  // Uses the set colors to build a cube in the default orientation
  // Outputs: 0 - Ran successfully
  //          1 - Error: Color status is unknown
  //          2 - Error: Invalid number of red squares
  //          3 - Error: Invalid number of orange squares
  //          4 - Error: Invalid number of yellow squares
  //          5 - Error: Invalid number of green squares
  //          6 - Error: Invalid number of blue squares
  //          7 - Error: Invalid number of white squares

  // DEBUG: Throws error for no reason?
  if (!cubeColorStatus) { // If the color status is unknown
    return 1;           // Give error and end
  }


  //URFDLB
  // Make temporary arrays for easier ordering
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Copy color arrays into temporary location arrays to put into correct order
  copyColorArray(defaultOrientation[0], uArray);
  copyColorArray(defaultOrientation[1], rArray);
  copyColorArray(defaultOrientation[2], fArray);
  copyColorArray(defaultOrientation[3], dArray);
  copyColorArray(defaultOrientation[4], lArray);
  copyColorArray(defaultOrientation[5], bArray);


  // Put color arrays into temporary full cube array in order
  char tempArray[54];
  for (int i = 0; i < 9; i++) { 
    tempArray[i] = uArray[i];
    tempArray[i + 9] = rArray[i];
    tempArray[i + 18] = fArray[i];
    tempArray[i + 27] = dArray[i];
    tempArray[i + 36] = lArray[i];
    tempArray[i + 45] = bArray[i];
  }

  int colorCount[] = {0, 0, 0, 0, 0, 0};  // Integer array for tracking number of colors

  // Check through entire array for invalid color count
  for (int i = 0; i < 54; i++) {
    if (tempArray[i] == 'R') { // Check each spot for each color
      colorCount[0]++;         // If the color is found then update integer array
    }
    if (tempArray[i] == 'O') {
      colorCount[1]++;
    }
    if (tempArray[i] == 'Y') {
      colorCount[2]++;
    }
    if (tempArray[i] == 'G') {
      colorCount[3]++;
    }
    if (tempArray[i] == 'B') {
      colorCount[4]++;
    }
    if (tempArray[i] == 'W') {
      colorCount[5]++;
    }
  }

  // Loop to check if every number has the right count (9)
  for (int i = 0; i < 6; i++) {
    if (colorCount[i] != 9) { // If not 9
      return i + 2;           // Error codes start at 2 for red and count up
    }
  }

  // Write temporary array to actual array
  for (int i = 0; i < 54; i++) {
    unorientedCubeArray[i] = tempArray[i];
  }

  cubeBuilt = 1;  // Write boolean to build cube
  return 0;
}

int VirtualCube::buildCubeArray() {
  // Uses the built unoriented cube and the set orientation to construct a string that can be fed to the kociemba solver
  // Outputs: 0 - Ran successfully
  //          1 - Error: Orientation not set
  //          2 - Cube not built
  //          3 - Could not find orientation

  if (!cubeOriented) { // Check if orientation has been set
    return 1;
  }

  if (!cubeBuilt) { // Check if unoriented cube has been built
    return 2;
  }

  // Build temp array
  char tempCubeArr[54];
  for (int i = 0; i < 54; i++) {
    tempCubeArr[i] = unorientedCubeArray[i];
  }

  // Rotate temporary cube until it is in correct orientation
  if (rotOrientAll(tempCubeArr)) {
    return 3; // If in error then exit program.
  }

  // Write to color cube array
  for (int i = 0; i < 54; i++) {
    colorCubeArray[i] = tempCubeArr[i];
    cubeArray[i] = tempCubeArr[i];
  }

  // Change all B's so there's no confusion from overlapping usage
  //('B'= Blue -> 'P' = Placeholer -> 'B' = Bottom)
  for (int i = 0; i < 54; i++) {// This is dumb but easier than fixing the root problem
    if (cubeArray[i] == 'B') {
      cubeArray[i] = 'P';
    }
  }

  char tempOrient[6];
  for (int i = 0; i < 6; i++) { // Another stupid fix so that the orientation can match the tempArray P's
    tempOrient[i] = orientation[i];
    if (tempOrient[i] == 'B') {
      tempOrient[i] = 'P';
    }
  }

  // Swap square values from colors to directional faces
  for (int i = 0; i < 54; i++) {
    if (cubeArray[i] == tempOrient[0]) {
      cubeArray[i] = 'U';
    }
    else if (cubeArray[i] == tempOrient[1]) {
      cubeArray[i] = 'R';
    }
    else if (cubeArray[i] == tempOrient[2]) {
      cubeArray[i] = 'F';
    }
    else if (cubeArray[i] == tempOrient[3]) {
      cubeArray[i] = 'D';
    }
    else if (cubeArray[i] == tempOrient[4]) {
      cubeArray[i] = 'L';
    }
    else if (cubeArray[i] == tempOrient[5]) {
      cubeArray[i] = 'B';
    }
  }


  cubeReady = 1;
  return 0;
}

int VirtualCube::rebuildFromCubeArray() {
  // Takes a cube array and updates the other class variables
  // Outputs: 0 - Ran successfully
  //          1 - Cube array isn't available (not built?)
  //          2X - Failed to write color array, X is error of set function
  //          3X - Failed to build unoriented cube, X is error of build function
  //          4X - Failed to set orientation, X is error of set function
  //          5X - Failed to build final cube, X is error of build function
  
  if (!cubeReady) { // Check if cube is in ready state
    return 1;
  }

  // Make temporary arrays
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Make temporary orientation
  char tempOrient[6];
  for (int i = 0; i< 6; i++) {
    tempOrient[i] = orientation[i];
  }

  // Convert cube array back to color cube array
  char tempColorArray[54];
  for(int i = 0; i<54; i++) {
    tempColorArray[i] = cubeArray[i];

    if(cubeArray[i] == 'B') {
      tempColorArray[i] = tempOrient[5];
    }
    else if(cubeArray[i] == 'U') {
      tempColorArray[i] = tempOrient[0];
    }
    else if(cubeArray[i] == 'R') {
      tempColorArray[i] = tempOrient[1];
    }
    else if(cubeArray[i] == 'F') {
      tempColorArray[i] = tempOrient[2];
    }
    else if(cubeArray[i] == 'D') {
      tempColorArray[i] = tempOrient[3];
    }
    else if(cubeArray[i] == 'L') {
      tempColorArray[i] = tempOrient[4];
    }
    else {
      tempColorArray[i] = 'Q';
    }
  }
  
  // Assign the temporary color cube array to the individual faces
  for (int i = 0; i < 9; i++) {
    uArray[i] = tempColorArray[i];
    rArray[i] = tempColorArray[i + 9];
    fArray[i] = tempColorArray[i + 18];
    dArray[i] = tempColorArray[i + 27];
    lArray[i] = tempColorArray[i + 36];
    bArray[i] = tempColorArray[i + 45];
  }

  // DEBUG REMOVE LATER
  for(int i = 0; i<54; i++) {
    colorCubeArray[i] = tempColorArray[i];
  }
  

  // DEBUG UNCOMMENT OUT LATER
  // The if statements here are for error handling and will print out a unique rebuild error combined with the function error
  //resetCube();
  //if(int i = setOrientation(tempOrient[4], tempOrient[5])){return 4*10 + i;}
  // if(int i = setColorArray(orientation[0], uArray, orientation[4])){return 2*100+i*10+1;}
  // if(int i = setColorArray(orientation[1], rArray, orientation[2])){return 2*100+i*10+2;}
  // if(int i = setColorArray(orientation[2], fArray, orientation[4])){return 2*100+i*10+3;}
  // if(int i = setColorArray(orientation[3], dArray, orientation[4])){return 2*100+i*10+4;}
  // if(int i = setColorArray(orientation[4], lArray, orientation[5])){return 2*100+i*10+5;}
  // if(int i = setColorArray(orientation[5], bArray, orientation[1])){return 2*100+i*10+6;}
  // if(int i = buildUnorientedCubeArray()){return 3*10 + i;}
  // if(int i = buildCubeArray()){return 5*10 + i;}

  return 0;
}

int VirtualCube::splitSolveString(String input, char delimiter, String output[]){
  // Splits solve string into array of substrings
  // Inputs:
  //  1 - input: solve string to be split apart
  //  2 - delimiter: character used to separate moves in solve string
  //  3 - output: Placeholder to be poplulated by move substrings
  // Outputs:
  //  tokenCount: The number of moves in solution

  int tokenIndex = 0;
  int tokenCount = 0;
  while (tokenIndex >= 0) {
    tokenIndex = input.indexOf(delimiter);
    if (tokenIndex >= 0) {
      output[tokenCount] = input.substring(0, tokenIndex);
      input = input.substring(tokenIndex + 1);
      tokenCount++;
    }
  }
  if (input.length() > 0) {
    output[tokenCount] = input;
    tokenCount++;
  }
  return tokenCount;
}

int VirtualCube::solveCube(String moves[], int maxMoves){
  // Uses Kociemba algorithm to get moves to solve cube
  // Inputs:
  //  1 - String moves[]: String that will be populated with moves to solve
  //  2 - int maxMoves: Maximum number of moves allowed before exiting and failing
  // Outputs:
  // >=0 - Number of moves
  //  -1 - Cube is not ready
  //  -2 - Solution not found

    // Solve using Kociemba
    const char* sol;
    sol = kociemba::solve(cubeArray);
    if (sol == nullptr) {
      return 2;
    }

    // Break solution string into substrings of moves
    int moveCount = splitSolveString(sol, ' ', moves);


    if (!cubeReady) return 1;  // Make sure the cube has been built


    return moveCount;  // Success
}

int VirtualCube::rotOrientAll(char tempCubeArray[]) {
  // Takes an unoriented and rotates it until it matches the correct orientation
  // Outputs: 0 - Found Solution
  //          1 - Failed to find solution

  // Build temporary orientation
  char tempOrientation[6];
  for (int i = 0; i < 6; i++) {
    tempOrientation[i] = defaultOrientation[i];
  }

  // Rotate unoriented cube until orientation matches
  for (int xrot = 0; xrot < 4; xrot++) {      // X-axis rotations
    for (int yrot = 0; yrot < 4; yrot++) {    // Y-axis rotations
      for (int zrot = 0; zrot < 4; zrot++) {  // Z-axis rotations
        // Check if orientation matches
        int matchCount = 0;
        for (int i = 0; i < 6; i++) {
          if (tempOrientation[i] == orientation[i]) {
            matchCount++;
          }
        }
        if (matchCount == 6) {
          return 0;
        }

        // If it doesn't match, rotate in X axis
        rotOrientX(tempOrientation, tempCubeArray);
      }
      // Rotate in Y-Axis
      rotOrientY(tempOrientation, tempCubeArray);
    }
    // Rotate in Z-Axis
    rotOrientZ(tempOrientation, tempCubeArray);
  }

  return 1;
}

void VirtualCube::rotOrientX(char orientArr[], char cubeArr[]) {
  // New orientation array:
  // U->F, R->R, F->D, D->B, L->L, B->U
  // n=new, o = old
  // nU=oB, nR=oR, nF=oU, nD=oF, nL=oL, nB=oD
  //  0=5,   1=1,   2=0,   3=2,   4=4,   5=3

  // Initialize character array for old orientation
  char oldOrient[6];  // URFDLB
  for (int i = 0; i < 6; i++) {
    oldOrient[i] = orientArr[i];
  }

  // Update orientation to new rotated one
  orientArr[0] = oldOrient[5];
  orientArr[1] = oldOrient[1];
  orientArr[2] = oldOrient[0];
  orientArr[3] = oldOrient[2];
  orientArr[4] = oldOrient[4];
  orientArr[5] = oldOrient[3];

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArr[i];
    rArrayOld[i] = cubeArr[i + 9];
    fArrayOld[i] = cubeArr[i + 18];
    dArrayOld[i] = cubeArr[i + 27];
    lArrayOld[i] = cubeArr[i + 36];
    bArrayOld[i] = cubeArr[i + 45];
  }

  // Make temporary arrays for new Orientation
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Assign new swapped array positions
  for (int i = 0; i < 9; i++) {
    uArray[i] = bArrayOld[i];
    rArray[i] = rArrayOld[i];
    fArray[i] = uArrayOld[i];
    dArray[i] = fArrayOld[i];
    lArray[i] = lArrayOld[i];
    bArray[i] = dArrayOld[i];
  }

  // Rotate Arrays as necessary
  // U->180, R->90, F->0, D->0, L->270, B->180
  rotateColorArray(uArray, 2);
  rotateColorArray(rArray, 1);
  rotateColorArray(lArray, 3);
  rotateColorArray(bArray, 2);

  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArr[i] = uArray[i];
    cubeArr[i + 9] = rArray[i];
    cubeArr[i + 18] = fArray[i];
    cubeArr[i + 27] = dArray[i];
    cubeArr[i + 36] = lArray[i];
    cubeArr[i + 45] = bArray[i];
  }

}

void VirtualCube::rotOrientY(char orientArr[], char cubeArr[]) {
  // New orientation array:
  // U->L, R->U, F->F, D->R, L->D, B->B
  // n=new, o = old
  // nU=oR, nR=oD, nF=oF, nD=oL, nL=oU, nB=oB
  //  0=1,   1=3,   2=2,   3=4,   4=0,   5=5

  // Initialize character array for old orientation
  char oldOrient[6];  // URFDLB
  for (int i = 0; i < 6; i++) {
    oldOrient[i] = orientArr[i];
  }

  // Update orientation to new rotated one
  orientArr[0] = oldOrient[1];
  orientArr[1] = oldOrient[3];
  orientArr[2] = oldOrient[2];
  orientArr[3] = oldOrient[4];
  orientArr[4] = oldOrient[0];
  orientArr[5] = oldOrient[5];

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArr[i];
    rArrayOld[i] = cubeArr[i + 9];
    fArrayOld[i] = cubeArr[i + 18];
    dArrayOld[i] = cubeArr[i + 27];
    lArrayOld[i] = cubeArr[i + 36];
    bArrayOld[i] = cubeArr[i + 45];
  }

  // Make temporary arrays for new Orientation
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Assign new swapped array positions
  for (int i = 0; i < 9; i++) {
    uArray[i] = rArrayOld[i];
    rArray[i] = dArrayOld[i];
    fArray[i] = fArrayOld[i];
    dArray[i] = lArrayOld[i];
    lArray[i] = uArrayOld[i];
    bArray[i] = bArrayOld[i];
  }

  // Rotate Arrays as necessary
  // U->90, R->90, F->90, D->90, L->90, B->270
  rotateColorArray(uArray, 1);
  rotateColorArray(rArray, 1);
  rotateColorArray(fArray, 1);
  rotateColorArray(dArray, 1);
  rotateColorArray(lArray, 1);
  rotateColorArray(bArray, 3);

  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArr[i] = uArray[i];
    cubeArr[i + 9] = rArray[i];
    cubeArr[i + 18] = fArray[i];
    cubeArr[i + 27] = dArray[i];
    cubeArr[i + 36] = lArray[i];
    cubeArr[i + 45] = bArray[i];
  }

}

void VirtualCube::rotOrientZ(char orientArr[], char cubeArr[]) {
  // New orientation array:
  // U->U, R->B, F->R, D->D, L->F, B->L
  // n=new, o = old
  // nU=oU, nR=oF, nF=oL, nD=oD, nL=oB, nB=oR
  //  0=0,   1=2,   2=4,   3=3,   4=5,   5=1

  // Initialize character array for old orientation
  char oldOrient[6];  // URFDLB
  for (int i = 0; i < 6; i++) {
    oldOrient[i] = orientArr[i];
  }

  // Update orientation to new rotated one
  orientArr[0] = oldOrient[0];
  orientArr[1] = oldOrient[2];
  orientArr[2] = oldOrient[4];
  orientArr[3] = oldOrient[3];
  orientArr[4] = oldOrient[5];
  orientArr[5] = oldOrient[1];

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArr[i];
    rArrayOld[i] = cubeArr[i + 9];
    fArrayOld[i] = cubeArr[i + 18];
    dArrayOld[i] = cubeArr[i + 27];
    lArrayOld[i] = cubeArr[i + 36];
    bArrayOld[i] = cubeArr[i + 45];
  }

  // Make temporary arrays for new Orientation
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Assign new swapped array positions
  // nU=oU, nR=oF, nF=oL, nD=oD, nL=oB, nB=oR
  for (int i = 0; i < 9; i++) {
    uArray[i] = uArrayOld[i];
    rArray[i] = fArrayOld[i];
    fArray[i] = lArrayOld[i];
    dArray[i] = dArrayOld[i];
    lArray[i] = bArrayOld[i];
    bArray[i] = rArrayOld[i];
  }

  // Rotate Arrays as necessary
  // U->90, R->0, F->0, D->270, L->0, B->0
  rotateColorArray(uArray, 1);
  rotateColorArray(dArray, 3);

  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArr[i] = uArray[i];
    cubeArr[i + 9] = rArray[i];
    cubeArr[i + 18] = fArray[i];
    cubeArr[i + 27] = dArray[i];
    cubeArr[i + 36] = lArray[i];
    cubeArr[i + 45] = bArray[i];
  }
}

int VirtualCube::rotU(int turns) {
  // Rotates Cube U or U' based on sign of turns input
  // Outputs: 0 - Ran successfully
  //          1 - Cube is not in a ready state

  if (!cubeReady) { // Check if cube is in ready state
    return 1;
  }

  if (turns == 0) { // exit program if not no turns
    return 0;
  }

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArray[i];
    rArrayOld[i] = cubeArray[i + 9];
    fArrayOld[i] = cubeArray[i + 18];
    dArrayOld[i] = cubeArray[i + 27];
    lArrayOld[i] = cubeArray[i + 36];
    bArrayOld[i] = cubeArray[i + 45];
  }

  // Make temporary arrays
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Fill new temporary arrays
  for (int i = 0; i < 9; i++) {
    uArray[i] = uArrayOld[i];
    rArray[i] = rArrayOld[i];
    fArray[i] = fArrayOld[i];
    dArray[i] = dArrayOld[i];
    lArray[i] = lArrayOld[i];
    bArray[i] = bArrayOld[i];
  }

  int dir = turns / abs(turns);
  for (int turnsLeft = turns; turnsLeft != 0; turnsLeft -= dir) { // Starts at the # of turns and either counts up or counts down to 0 based on direction
    if (dir > 0) { // Clockwise
      // Update Top Face
      rotateColorArray(uArray, 3);

      //Update Right Face
      rArray[0] = bArrayOld[0];
      rArray[1] = bArrayOld[1];
      rArray[2] = bArrayOld[2];

      // Update Front Face
      fArray[0] = rArrayOld[0];
      fArray[1] = rArrayOld[1];
      fArray[2] = rArrayOld[2];

      //Update Left Face
      lArray[0] = fArrayOld[0];
      lArray[1] = fArrayOld[1];
      lArray[2] = fArrayOld[2];

      // Update Back Face
      bArray[0] = lArrayOld[0];
      bArray[1] = lArrayOld[1];
      bArray[2] = lArrayOld[2];

    }
    else if (dir < 0) { // Counterclockwise
      // Update Top Face
      rotateColorArray(uArray, 1);

      //Update Right Face
      rArray[0] = fArrayOld[0];
      rArray[1] = fArrayOld[1];
      rArray[2] = fArrayOld[2];

      // Update Front Face
      fArray[0] = lArrayOld[0];
      fArray[1] = lArrayOld[1];
      fArray[2] = lArrayOld[2];

      //Update Left Face
      lArray[0] = bArrayOld[0];
      lArray[1] = bArrayOld[1];
      lArray[2] = bArrayOld[2];

      // Update Back Face
      bArray[0] = rArrayOld[0];
      bArray[1] = rArrayOld[1];
      bArray[2] = rArrayOld[2];
    }

    // Update old arrays
    for (int i = 0; i < 9; i++) {
      uArrayOld[i] = uArray[i];
      rArrayOld[i] = rArray[i];
      fArrayOld[i] = fArray[i];
      dArrayOld[i] = dArray[i];
      lArrayOld[i] = lArray[i];
      bArrayOld[i] = bArray[i];
    }
  }
  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArray[i] = uArray[i];
    cubeArray[i + 9] = rArray[i];
    cubeArray[i + 18] = fArray[i];
    cubeArray[i + 27] = dArray[i];
    cubeArray[i + 36] = lArray[i];
    cubeArray[i + 45] = bArray[i];
  }

  return 0;
}

int VirtualCube::rotR(int turns) {
  // Rotates Cube R or R' based on sign of turns input
  // Outputs: 0 - Ran successfully
  //          1 - Cube is not in a ready state

  if (!cubeReady) { // Check if cube is in ready state
    return 1;
  }

  if (turns == 0) { // exit program if not no turns
    return 0;
  }

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArray[i];
    rArrayOld[i] = cubeArray[i + 9];
    fArrayOld[i] = cubeArray[i + 18];
    dArrayOld[i] = cubeArray[i + 27];
    lArrayOld[i] = cubeArray[i + 36];
    bArrayOld[i] = cubeArray[i + 45];
  }

  // Make temporary arrays
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Fill new temporary arrays
  for (int i = 0; i < 9; i++) {
    uArray[i] = uArrayOld[i];
    rArray[i] = rArrayOld[i];
    fArray[i] = fArrayOld[i];
    dArray[i] = dArrayOld[i];
    lArray[i] = lArrayOld[i];
    bArray[i] = bArrayOld[i];
  }

  int dir = turns / abs(turns);
  for (int turnsLeft = turns; turnsLeft != 0; turnsLeft -= dir) { // Starts at the # of turns and either counts up or counts down to 0 based on direction
    if (dir > 0) { // Clockwise
      // Update Top Face
      uArray[2] = fArrayOld[2];
      uArray[5] = fArrayOld[5];
      uArray[8] = fArrayOld[8];

      //Update Right Face
      rotateColorArray(rArray, 3); // Rotate 3 turns ccw (1 turn cw)

      // Update Front Face
      fArray[2] = dArrayOld[2];
      fArray[5] = dArrayOld[5];
      fArray[8] = dArrayOld[8];

      // Update Back Face
      bArray[0] = uArrayOld[8];
      bArray[3] = uArrayOld[5];
      bArray[6] = uArrayOld[2];

      // Update Down Face
      dArray[2] = bArrayOld[6];
      dArray[5] = bArrayOld[3];
      dArray[8] = bArrayOld[0];
    }
    else if (dir < 0) { // Counterclockwise
      // Update Top Face
      uArray[2] = bArrayOld[6];
      uArray[5] = bArrayOld[3];
      uArray[8] = bArrayOld[0];

      // Update Right Face
      rotateColorArray(rArray, 1);  // Rotates one turn ccw

      // Update Front Face
      fArray[2] = uArrayOld[2];
      fArray[5] = uArrayOld[5];
      fArray[8] = uArrayOld[8];

      // Update Down Face
      dArray[2] = fArrayOld[2];
      dArray[5] = fArrayOld[5];
      dArray[8] = fArrayOld[8];

      // Update Back Face
      bArray[0] = dArrayOld[8];
      bArray[3] = dArrayOld[5];
      bArray[6] = dArrayOld[2];
    }

    // Update old arrays
    for (int i = 0; i < 9; i++) {
      uArrayOld[i] = uArray[i];
      rArrayOld[i] = rArray[i];
      fArrayOld[i] = fArray[i];
      dArrayOld[i] = dArray[i];
      lArrayOld[i] = lArray[i];
      bArrayOld[i] = bArray[i];
    }
  }
  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArray[i] = uArray[i];
    cubeArray[i + 9] = rArray[i];
    cubeArray[i + 18] = fArray[i];
    cubeArray[i + 27] = dArray[i];
    cubeArray[i + 36] = lArray[i];
    cubeArray[i + 45] = bArray[i];
  }

  return 0;
}

int VirtualCube::rotF(int turns) {
  // Rotates Cube F or F' based on sign of turns input
  // Outputs: 0 - Ran successfully
  //          1 - Cube is not in a ready state

  if (!cubeReady) { // Check if cube is in ready state
    return 1;
  }

  if (turns == 0) { // exit program if not no turns
    return 0;
  }

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArray[i];
    rArrayOld[i] = cubeArray[i + 9];
    fArrayOld[i] = cubeArray[i + 18];
    dArrayOld[i] = cubeArray[i + 27];
    lArrayOld[i] = cubeArray[i + 36];
    bArrayOld[i] = cubeArray[i + 45];
  }

  // Make temporary arrays
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Fill new temporary arrays
  for (int i = 0; i < 9; i++) {
    uArray[i] = uArrayOld[i];
    rArray[i] = rArrayOld[i];
    fArray[i] = fArrayOld[i];
    dArray[i] = dArrayOld[i];
    lArray[i] = lArrayOld[i];
    bArray[i] = bArrayOld[i];
  }

  int dir = turns / abs(turns);
  for (int turnsLeft = turns; turnsLeft != 0; turnsLeft -= dir) { // Starts at the # of turns and either counts up or counts down to 0 based on direction
    if (dir > 0) { // Clockwise
      // Update Top Face
      uArray[6] = lArrayOld[8];
      uArray[7] = lArrayOld[5];
      uArray[8] = lArrayOld[2];

      //Update Right Face
      rArray[0] = uArrayOld[6];
      rArray[3] = uArrayOld[7];
      rArray[6] = uArrayOld[8];

      // Update Front Face
      rotateColorArray(fArray, 3);

      // Update Down Face
      dArray[0] = rArrayOld[6];
      dArray[1] = rArrayOld[3];
      dArray[2] = rArrayOld[0];

      // Update Left Face
      lArray[2] = dArrayOld[0];
      lArray[5] = dArrayOld[1];
      lArray[8] = dArrayOld[2];


    }
    else if (dir < 0) { // Counterclockwise
      // Update Top Face
      uArray[6] = rArrayOld[0];
      uArray[7] = rArrayOld[3];
      uArray[8] = rArrayOld[6];

      //Update Right Face
      rArray[0] = dArrayOld[2];
      rArray[3] = dArrayOld[1];
      rArray[6] = dArrayOld[0];

      // Update Front Face
      rotateColorArray(fArray, 1);  // 1 turn ccw

      // Update Down Face
      dArray[0] = lArrayOld[2];
      dArray[1] = lArrayOld[5];
      dArray[2] = lArrayOld[8];

      // Update Left Face
      lArray[2] = uArrayOld[8];
      lArray[5] = uArrayOld[7];
      lArray[8] = uArrayOld[6];
    }

    // Update old arrays
    for (int i = 0; i < 9; i++) {
      uArrayOld[i] = uArray[i];
      rArrayOld[i] = rArray[i];
      fArrayOld[i] = fArray[i];
      dArrayOld[i] = dArray[i];
      lArrayOld[i] = lArray[i];
      bArrayOld[i] = bArray[i];
    }
  }
  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArray[i] = uArray[i];
    cubeArray[i + 9] = rArray[i];
    cubeArray[i + 18] = fArray[i];
    cubeArray[i + 27] = dArray[i];
    cubeArray[i + 36] = lArray[i];
    cubeArray[i + 45] = bArray[i];
  }

  return 0;
}

int VirtualCube::rotD(int turns) {
  // Rotates Cube D or D' based on sign of turns input
  // Outputs: 0 - Ran successfully
  //          1 - Cube is not in a ready state

  if (!cubeReady) { // Check if cube is in ready state
    return 1;
  }

  if (turns == 0) { // exit program if not no turns
    return 0;
  }

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArray[i];
    rArrayOld[i] = cubeArray[i + 9];
    fArrayOld[i] = cubeArray[i + 18];
    dArrayOld[i] = cubeArray[i + 27];
    lArrayOld[i] = cubeArray[i + 36];
    bArrayOld[i] = cubeArray[i + 45];
  }

  // Make temporary arrays
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Fill new temporary arrays
  for (int i = 0; i < 9; i++) {
    uArray[i] = uArrayOld[i];
    rArray[i] = rArrayOld[i];
    fArray[i] = fArrayOld[i];
    dArray[i] = dArrayOld[i];
    lArray[i] = lArrayOld[i];
    bArray[i] = bArrayOld[i];
  }

  int dir = turns / abs(turns);
  for (int turnsLeft = turns; turnsLeft != 0; turnsLeft -= dir) { // Starts at the # of turns and either counts up or counts down to 0 based on direction
    if (dir > 0) { // Clockwise

      //Update Right Face
      rArray[6] = fArrayOld[6];
      rArray[7] = fArrayOld[7];
      rArray[8] = fArrayOld[8];

      // Update Front Face
      fArray[6] = lArrayOld[6];
      fArray[7] = lArrayOld[7];
      fArray[8] = lArrayOld[8];

      // Update Down Face
      rotateColorArray(dArray, 3);

      //Update Left Face
      lArray[6] = bArrayOld[6];
      lArray[7] = bArrayOld[7];
      lArray[8] = bArrayOld[8];

      // Update Back Face
      bArray[6] = rArrayOld[6];
      bArray[7] = rArrayOld[7];
      bArray[8] = rArrayOld[8];

    }
    else if (dir < 0) { // Counterclockwise

      //Update Right Face
      rArray[6] = bArrayOld[6];
      rArray[7] = bArrayOld[7];
      rArray[8] = bArrayOld[8];

      // Update Front Face
      fArray[6] = rArrayOld[6];
      fArray[7] = rArrayOld[7];
      fArray[8] = rArrayOld[8];

      // Update Down Face
      rotateColorArray(dArray, 1);

      //Update Left Face
      lArray[6] = fArrayOld[6];
      lArray[7] = fArrayOld[7];
      lArray[8] = fArrayOld[8];

      // Update Back Face
      bArray[6] = lArrayOld[6];
      bArray[7] = lArrayOld[7];
      bArray[8] = lArrayOld[8];
    }

    // Update old arrays
    for (int i = 0; i < 9; i++) {
      uArrayOld[i] = uArray[i];
      rArrayOld[i] = rArray[i];
      fArrayOld[i] = fArray[i];
      dArrayOld[i] = dArray[i];
      lArrayOld[i] = lArray[i];
      bArrayOld[i] = bArray[i];
    }
  }
  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArray[i] = uArray[i];
    cubeArray[i + 9] = rArray[i];
    cubeArray[i + 18] = fArray[i];
    cubeArray[i + 27] = dArray[i];
    cubeArray[i + 36] = lArray[i];
    cubeArray[i + 45] = bArray[i];
  }

  return 0;
}

int VirtualCube::rotL(int turns) {
  // Rotates Cube L or L' based on sign of turns input
  // Outputs: 0 - Ran successfully
  //          1 - Cube is not in a ready state

  if (!cubeReady) { // Check if cube is in ready state
    return 1;
  }

  if (turns == 0) { // exit program if not no turns
    return 0;
  }

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArray[i];
    rArrayOld[i] = cubeArray[i + 9];
    fArrayOld[i] = cubeArray[i + 18];
    dArrayOld[i] = cubeArray[i + 27];
    lArrayOld[i] = cubeArray[i + 36];
    bArrayOld[i] = cubeArray[i + 45];
  }

  // Make temporary arrays
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Fill new temporary arrays
  for (int i = 0; i < 9; i++) {
    uArray[i] = uArrayOld[i];
    rArray[i] = rArrayOld[i];
    fArray[i] = fArrayOld[i];
    dArray[i] = dArrayOld[i];
    lArray[i] = lArrayOld[i];
    bArray[i] = bArrayOld[i];
  }

  int dir = turns / abs(turns);
  for (int turnsLeft = turns; turnsLeft != 0; turnsLeft -= dir) { // Starts at the # of turns and either counts up or counts down to 0 based on direction
    if (dir > 0) { // Clockwise
      // Update Up Face
      uArray[0] = bArrayOld[8];
      uArray[3] = bArrayOld[5];
      uArray[6] = bArrayOld[2];

      // Update Front Face
      fArray[0] = uArrayOld[0];
      fArray[3] = uArrayOld[3];
      fArray[6] = uArrayOld[6];

      // Update Down Face
      dArray[0] = fArrayOld[0];
      dArray[3] = fArrayOld[3];
      dArray[6] = fArrayOld[6];

      //Update Left Face
      rotateColorArray(lArray, 3); // Rotate 3 turns ccw (1 turn cw)

      // Update Back Face
      bArray[2] = dArrayOld[6];
      bArray[5] = dArrayOld[3];
      bArray[8] = dArrayOld[0];

    }
    else if (dir < 0) { // Counterclockwise
      // Update Up Face
      uArray[0] = fArrayOld[0];
      uArray[3] = fArrayOld[3];
      uArray[6] = fArrayOld[6];

      // Update Front Face
      fArray[0] = dArrayOld[0];
      fArray[3] = dArrayOld[3];
      fArray[6] = dArrayOld[6];

      // Update Down Face
      dArray[0] = bArrayOld[8];
      dArray[3] = bArrayOld[5];
      dArray[6] = bArrayOld[2];

      //Update Left Face
      rotateColorArray(lArray, 1); // Rotate 3 turns ccw (1 turn cw)

      // Update Back Face
      bArray[2] = uArrayOld[6];
      bArray[5] = uArrayOld[3];
      bArray[8] = uArrayOld[0];
    }

    // Update old arrays
    for (int i = 0; i < 9; i++) {
      uArrayOld[i] = uArray[i];
      rArrayOld[i] = rArray[i];
      fArrayOld[i] = fArray[i];
      dArrayOld[i] = dArray[i];
      lArrayOld[i] = lArray[i];
      bArrayOld[i] = bArray[i];
    }
  }
  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArray[i] = uArray[i];
    cubeArray[i + 9] = rArray[i];
    cubeArray[i + 18] = fArray[i];
    cubeArray[i + 27] = dArray[i];
    cubeArray[i + 36] = lArray[i];
    cubeArray[i + 45] = bArray[i];
  }

  return 0;
}

int VirtualCube::rotB(int turns) {
  // Rotates Cube B or B' based on sign of turns input
  // Outputs: 0 - Ran successfully
  //          1 - Cube is not in a ready state

  if (!cubeReady) { // Check if cube is in ready state
    return 1;
  }

  if (turns == 0) { // exit program if not no turns
    return 0;
  }

  // Make temporary arrays of old orientation
  char uArrayOld[9];
  char rArrayOld[9];
  char fArrayOld[9];
  char dArrayOld[9];
  char lArrayOld[9];
  char bArrayOld[9];

  for (int i = 0; i < 9; i++) {
    uArrayOld[i] = cubeArray[i];
    rArrayOld[i] = cubeArray[i + 9];
    fArrayOld[i] = cubeArray[i + 18];
    dArrayOld[i] = cubeArray[i + 27];
    lArrayOld[i] = cubeArray[i + 36];
    bArrayOld[i] = cubeArray[i + 45];
  }

  // Make temporary arrays
  char uArray[9];
  char rArray[9];
  char fArray[9];
  char dArray[9];
  char lArray[9];
  char bArray[9];

  // Fill new temporary arrays
  for (int i = 0; i < 9; i++) {
    uArray[i] = uArrayOld[i];
    rArray[i] = rArrayOld[i];
    fArray[i] = fArrayOld[i];
    dArray[i] = dArrayOld[i];
    lArray[i] = lArrayOld[i];
    bArray[i] = bArrayOld[i];
  }

  int dir = turns / abs(turns);
  for (int turnsLeft = turns; turnsLeft != 0; turnsLeft -= dir) { // Starts at the # of turns and either counts up or counts down to 0 based on direction
    if (dir > 0) { // Clockwise
      // Update Up Face
      uArray[0] = rArrayOld[2];
      uArray[1] = rArrayOld[5];
      uArray[2] = rArrayOld[8];

      //Update Right Face
      rArray[2] = dArrayOld[8];
      rArray[5] = dArrayOld[7];
      rArray[8] = dArrayOld[6];

      // Update Down Face
      dArray[6] = lArrayOld[0];
      dArray[7] = lArrayOld[3];
      dArray[8] = lArrayOld[6];

      // Update Left Face
      lArray[0] = uArrayOld[2];
      lArray[3] = uArrayOld[1];
      lArray[6] = uArrayOld[0];

      // Update Back Face
      rotateColorArray(bArray, 3);
    }
    else if (dir < 0) { // Counterclockwise
      // Update Up Face
      uArray[0] = lArrayOld[6];
      uArray[1] = lArrayOld[3];
      uArray[2] = lArrayOld[0];

      //Update Right Face
      rArray[2] = uArrayOld[0];
      rArray[5] = uArrayOld[1];
      rArray[8] = uArrayOld[2];

      // Update Down Face
      dArray[6] = rArrayOld[8];
      dArray[7] = rArrayOld[5];
      dArray[8] = rArrayOld[2];

      // Update Left Face
      lArray[0] = dArrayOld[6];
      lArray[3] = dArrayOld[7];
      lArray[6] = dArrayOld[8];

      // Update Back Face
      rotateColorArray(bArray, 1);
    }

    // Update old arrays
    for (int i = 0; i < 9; i++) {
      uArrayOld[i] = uArray[i];
      rArrayOld[i] = rArray[i];
      fArrayOld[i] = fArray[i];
      dArrayOld[i] = dArray[i];
      lArrayOld[i] = lArray[i];
      bArrayOld[i] = bArray[i];
    }
  }
  // Update cube array
  for (int i = 0; i < 9; i++) { // Update U
    cubeArray[i] = uArray[i];
    cubeArray[i + 9] = rArray[i];
    cubeArray[i + 18] = fArray[i];
    cubeArray[i + 27] = dArray[i];
    cubeArray[i + 36] = lArray[i];
    cubeArray[i + 45] = bArray[i];
  }

  return 0;
}

int VirtualCube::executeMove(const String &move) {
  // Rotates virtual cube
  // Outputs:
  //  0 - Rotated successfuly
  //  1 - Cube is not ready to be turned
  //  2 - Move string is empty
  //  3 - Move execution failed


  // Check if cube is ready to be moved
  if (!cubeReady) {
    return 1;
  }

  // Check if move string is empty
  if (move.length() == 0) return 1;

  // Extract which face is moving
  char face = move.charAt(0);

  // Initialize number of turns
  int turns = 1;

  // If rotating twice
  if (move.endsWith("2")) {         
      turns = 2;
  }
  
  // If rotating backwards
  if (move.endsWith("'")) {  
      turns = -1*turns;
  }

  // Execute desired move
  int result = 0;
  switch (face) {
    case 'U': result = rotU(turns); break;
    case 'R': result = rotR(turns); break;
    case 'F': result = rotF(turns); break;
    case 'D': result = rotD(turns); break;
    case 'L': result = rotL(turns); break;
    case 'B': result = rotB(turns); break;
    default: return 3; // Invalid face character
  }

  // Check if move execution failed
  if (result != 0) {
    return 3;
  }

  return 0;
}

