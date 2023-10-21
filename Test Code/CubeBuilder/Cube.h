// Cube.h
#ifndef Cube_h
#define Cube_h

#include <Arduino.h>
#include "string.h"

class Cube
{
public:
    // Cube Sides Character Arrays
    char redSide[9];
    char orangeSide[9];
    char yellowSide[9];
    char greenSide[9];
    char blueSide[9];
    char whiteSide[9];

    char orientation[6];        //URFDLB

    int cubeColorStatus = 3;    // 1 = Solved, 2 = Scrambled, 3 = Unknown
    bool cubeOriented = 0;      // 1 = Oriented, 0 = Unoriented
    bool cubeReady = 0;

    char cubeArray[54];
public:
    Cube();
    void resetCube();
    void resetOrientation();
    void resetColor();
    void resetCubeArray();
    void setSolved();
    int setFace(char color, char newFace[9]);
    int setSquare(char color, int squarePos, char newColor);
    int setOrientation(char leftColor, char backColor);
    bool colorsToCubeArray;
    bool cubeArrayToColors;
    
    
};

#endif // CUBE_H
