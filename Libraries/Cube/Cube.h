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

    // Left Side Characters
    char redLeft;
    char orangeLeft;
    char yellowLeft;
    char greenLeft;
    char blueLeft;
    char whiteLeft;
    char defaultLeft[6] = {'G', 'R', 'G', 'G', 'O', 'B'};   // Left sides corresponding to default orientation

    // Color Set Bool
    bool redSet = 0;
    bool orangeSet = 0;
    bool yellowSet = 0;
    bool greenSet = 0;
    bool blueSet = 0;
    bool whiteSet = 0;
    int setCount = 0;

    char orientation[6];        //URFDLB
    char defaultOrientation[6] = {'W', 'B', 'R', 'Y', 'G', 'O'};   // Red in front, white on top

    int cubeColorStatus = 3;    // 0 = Unknown, 1 = Solved, 2 = Built
    bool cubeOriented = 0;      // 1 = Oriented, 0 = Unoriented
    bool cubeBuilt = 0;
    bool cubeReady = 0;

    char unorientedCubeArray[54];
    char cubeArray[54];
    char colorCubeArray[54];
public:
    Cube();
    void resetCube();
    void resetOrientation();
    void resetColor();
    void resetUnorientedCubeArray();
    void resetCubeArray();
    void setSolved();
    int setColorArray(char color, char newFace[9], char leftSide);
    int setFaceSquare(char color, int squarePos, char newColor);
    int setOrientation(char leftColor, char backColor);
    int buildUnorientedCubeArray();
    int buildCubeArray();
    int rebuildFromCubeArray(); // UNFINISHED NEEDS DEBUGGING
    int rotU(int turns);
    int rotR(int turns);
    int rotF(int turns);
    int rotD(int turns);
    int rotL(int turns);
    int rotB(int turns);


private:    
    int copyColorArray(char color, char copyArray[]);
    void rotateColorArray(char rotArray[], int turns);
    void rotOrientX(char orientArr[], char cubeArr[]);
    void rotOrientY(char orientArr[], char cubeArr[]);
    void rotOrientZ(char orientArr[], char cubeArr[]);
    int rotOrientAll(char tempCubeArr[]);
    
    
};

#endif // CUBE_H
