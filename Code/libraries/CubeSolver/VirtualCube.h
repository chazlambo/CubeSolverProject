// VirtualCube.h
#ifndef VirtualCube_h
#define VirtualCube_h

#include <Arduino.h>
#include <kociemba.h>
#include "string.h"

class VirtualCube
{
private:
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

    // Cube Arrays
    char unorientedCubeArray[54];
    char cubeArray[54];
    char colorCubeArray[54];

public:
    VirtualCube();
    bool isReady();
    void resetCube();
    void setSolved();
    int setColorArray(char color, char newFace[9], char leftSide);
    int setFaceSquare(char color, int squarePos, char newColor);
    int setOrientation(char leftColor, char backColor);
    int buildUnorientedCubeArray();
    int buildCubeArray();
    int rebuildFromCubeArray(); // UNFINISHED NEEDS DEBUGGING

    // Get Solution
    // maxTokens bounds `output` — see the note in the .cpp. Passing 0 or a
    // negative value is treated as "no room" and returns -1.
    int splitSolveString(String input, char delimiter, String output[], int maxTokens);
    int solveCube(String moves[], int maxMoves);

    // Whole-cube sanity checks, run automatically by solveCube() but public so
    // the scan path can reject a bad read before committing to a solve.
    //
    // validateCentres(): 0 ok, non-zero if a centre facelet is not in its
    //   canonical URFDLB position.
    //
    //   IMPORTANT — this does NOT detect an orientation misread, despite the
    //   obvious intuition that it should. buildCubeArray() relabels colours
    //   through orientation[], so the centres come out canonical BY
    //   CONSTRUCTION whatever orientation was reported. A misread orientation
    //   produces a different but internally consistent cube that is legal,
    //   piece-valid, centre-canonical and solvable — the machine then executes
    //   ~20 moves in the wrong frame with no error at any layer. Catching that
    //   requires cross-checking the scanned left/back colours against something
    //   outside the model, which this class cannot do on its own.
    //
    //   Kept as cheap insurance against a future path that writes cubeArray
    //   without going through buildCubeArray(). Expect it to always return 0.
    //
    // validatePieces(): 0 ok. Catches compensating misreads that leave the
    //   per-colour counts at exactly 9 — the dominant failure mode here, since
    //   Y and W are the closest colour pair on most of the sensors.
    int validateCentres() const;
    int validatePieces() const;

    // Movement functions
    int executeMove(const String &moveString);

    // State accessors.
    //
    // Both return a pointer to a fixed 54-byte buffer that is NOT
    // NUL-terminated — treat them as char[54], never as a C string. Do not
    // pass them to strlen/printf("%s"). kociemba::solve() reads exactly 54
    // bytes, which is why passing cubeArray to it directly is safe.
    //
    // The buffer is owned by this object and is invalidated by resetCube(),
    // buildCubeArray(), executeMove() and friends. Copy it if you need to keep it.
    const char* getCubeArray() const           { return cubeArray; }
    const char* getUnorientedCubeArray() const { return unorientedCubeArray; }

    // TODO: DEBUG REMOVE LATER
    void printUnorientedCubeArray();
    void printCubeArray();

private:    
    void resetOrientation();
    void resetColor();
    void resetUnorientedCubeArray();
    void resetCubeArray();
    int copyColorArray(char color, char copyArray[]);
    void rotateColorArray(char rotArray[], int turns);
    void rotOrientX(char orientArr[], char cubeArr[]);
    void rotOrientY(char orientArr[], char cubeArr[]);
    void rotOrientZ(char orientArr[], char cubeArr[]);
    int rotOrientAll(char tempCubeArr[]);

    int rotU(int turns);
    int rotR(int turns);
    int rotF(int turns);
    int rotD(int turns);
    int rotL(int turns);
    int rotB(int turns);
    
};

#endif // VirtualCube_h
