#include "cube.h"

Cube::Cube()
{
  this->resetCube();
}


void Cube::resetCube()
{
  this->resetOrientation();
  this->resetColor();
  this->resetCubeArray();
}

void Cube::resetCubeArray()
{
  // Sets each character in the cube array to garbage char X
  char resetArray[] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
  for (int i = 0; i < 54; i++) {
    cubeArray[i] = 'X';
  }

  cubeReady = 0;  // Sets the ready toggle to false
}

void Cube::resetOrientation()
{
  // Sets each character in orientation array to garbage char Q
  char resetArray[] = "QQQQQQ";
  for (int i = 0; i < 6; i++) {
    orientation[i] = resetArray[i];
  }

  cubeOriented = 0; // Sets the oriented boolean to false

  this->resetCubeArray();
}

void Cube::resetColor()
{
  // Undefine faces
  setFace('R', "QQQQRQQQQ");
  setFace('O', "QQQQOQQQQ");
  setFace('Y', "QQQQYQQQQ");
  setFace('G', "QQQQGQQQQ");
  setFace('B', "QQQQBQQQQ");
  setFace('W', "QQQQWQQQQ");

  // Set color status to unknown
  cubeColorStatus = 3;

  this->resetCubeArray();
}

void Cube::setSolved()  // Sets the cube's color faces to solved
{
  setFace('R', "RRRRRRRRR");
  setFace('O', "OOOOOOOOO");
  setFace('Y', "YYYYYYYYY");
  setFace('G', "GGGGGGGGG");
  setFace('B', "BBBBBBBBB");
  setFace('W', "WWWWWWWWW");
}

int Cube::setFace(char color, char newFace[])
{
  // Function to set an entire face of the cube
  // Inputs:  color - must be a capitalized valid color e.g. 'R'
  //          newFace - must be correct sized string with correct middle color e.g. "YBBWRWGRW"
  // Outputs: 0 - Ran successfully
  //          1 - Error: Middle square of newFace doesn't match color
  //          2 - Error: newFace string contained invalid characters
  //          3 - Error: color input doesn't match a side (check uppercase)

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

  // Set Face
  switch (color) {
    // Red
    case 'R':
      for (int i = 0; i < 9; i++) { // Loops through each character in the array
        redSide[i] = newFace[i];    // Assigns new value
      }
      break;

    // Orange
    case 'O':
      for (int i = 0; i < 9; i++) {
        orangeSide[i] = newFace[i];
      }
      break;

    // Yellow
    case 'Y':
      for (int i = 0; i < 9; i++) {
        yellowSide[i] = newFace[i];
      }
      break;

    // Green
    case 'G':
      for (int i = 0; i < 9; i++) {
        greenSide[i] = newFace[i];
      }
      break;

    // Blue
    case 'B':
      for (int i = 0; i < 9; i++) {
        blueSide[i] = newFace[i];
      }
      break;

    // White
    case 'W':
      for (int i = 0; i < 9; i++) {
        whiteSide[i] = newFace[i];
      }
      break;

    default:
      return 3; // Error: Color does not correspond to a side. Invalid input

  }

  return 0;
}

int Cube::setSquare(char color, int squarePos, char newColor)
{
  // Function to set the color of an individual square on a specified face
  // Inputs:  color - must be a capitalized valid color e.g. 'R'
  //          squarePos - must be 0-8, cannot be 4 because you can't set middle color
  //          newColor - must be a capitalized valid color e.g. 'R'
  // Outputs: 0 - Ran successfully
  //          1 - Error: color input does not correspond to a side. Invalid input
  //          2 - Error: squarePos value is not valid for this operation
  //          3 - Error: newColor input is not a valid color (check uppercase)

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

  return 0;
}

int Cube::setOrientation(char leftColor, char backColor)
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
