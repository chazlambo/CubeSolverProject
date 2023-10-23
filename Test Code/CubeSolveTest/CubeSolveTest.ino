#include "Cube.h"
#include <Arduino.h>
#include <kociemba.h>

Cube myCube;

char leftCol;
char backCol;

//Kociemba Variables
elapsedMillis em;

DMAMEM char buf479[479 * 1024]; // 479K in DMAMEM
char  buf248[248 * 1024];       // 248K on in DTCM

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial.available());
  Serial.println("Starting...");

  // Kociemba Setup
  em = 0;
  kociemba::set_memory(buf479, buf248);   // removing this line slows the computation by a factor of 4 (but saves a lot of RAM...)
  Serial.printf("RAM buffer created in %d ms.\n", (int)em);

  // Testing Checkerboard Cube Solve
  myCube.resetCube();
  myCube.setColorArray('W', "WYWYWYWYW", 'G');
  myCube.setColorArray('B', "BGBGBGBGB", 'R');
  myCube.setColorArray('R', "ROROROROR", 'G');
  myCube.setColorArray('Y', "YWYWYWYWY", 'G');
  myCube.setColorArray('G', "GBGBGBGBG", 'O');
  myCube.setColorArray('O', "ORORORORO", 'B');
  myCube.setOrientation('G', 'O');

  Serial.print("Checkerboard (Oriented: W, B): ");
  Serial.println(myCube.buildCubeArray());
  printCubeArray();

  Serial.println("Solving:");
  const char* res1 = kociemba::solve(myCube.cubeArray);
  if (res1 == nullptr) {
    Serial.println("no solution found");
  }
  else {
    Serial.printf("Solution [%s] found in %d ms.\n", res1, (int)em);
  }

  Serial.print("Rotating:");
  Serial.print("U2 ");
  myCube.rotU(2);
  Serial.print("D2 ");
  myCube.rotD(2);
  Serial.print("R2 ");
  myCube.rotR(2);
  Serial.print("L2 ");
  myCube.rotL(2);
  Serial.print("F2 ");
  myCube.rotF(2);
  Serial.print("B2 ");
  myCube.rotB(2);
  Serial.println("Rotation completed:");
  Serial.print("Rebuilding Cube...");
  Serial.println(myCube.rebuildFromCubeArray());
  Serial.println("Rebuild complete, printing...");
  Serial.println("\nColor Cube Array:");
  printOrientation();
  printColorCubeArray();

  // Testing Scramble [L R U D F B L' R' U' D' F' B'] (ALL MOVES IN YBOWGR ORIENTATION)
  myCube.resetCube();
  myCube.setColorArray('Y', "YOYBYGYRY", 'G');
  myCube.setColorArray('B', "BYBBBBBWB", 'O');
  myCube.setColorArray('O', "OYOOOOOWO", 'G');
  myCube.setColorArray('W', "WRWBWGWOW", 'G');
  myCube.setColorArray('G', "GYGGGGGWG", 'R');
  myCube.setColorArray('R', "RYRRRRRWR", 'B');
  myCube.setOrientation('G', 'R');

  Serial.print("Random Scramble (Oriented: YBOWGR): ");
  Serial.println(myCube.buildCubeArray());
  printCubeArray();

  Serial.println("Solving:");
  const char* res2 = kociemba::solve(myCube.cubeArray);
  if (res2 == nullptr) {
    Serial.println("no solution found");
  }
  else {
    Serial.print("Solution found: [");
    for(int i = 0; i<strlen(res2);i++){
      Serial.print(res2[i]);
    }
    Serial.printf("Solution [%s] found in %d ms.\n", res2, (int)em);
  }
  // Code was run prior to get this solution:
  // Solution [R L F B R L' F2 R2 B2 U D F2 L2 B2 R2] found in 77 ms.
  Serial.println("Rotating Cube: ");
  myCube.rotR(1);
  myCube.rotL(1);
  myCube.rotF(1);
  myCube.rotB(1);
  myCube.rotR(1);
  myCube.rotL(-1);
  myCube.rotF(2);
  myCube.rotR(2);
  myCube.rotB(2);
  myCube.rotU(1);
  myCube.rotD(1);
  myCube.rotF(2);
  myCube.rotL(2);
  myCube.rotB(2);
  myCube.rotR(2);
  
  Serial.println("Rotation completed:");
  printCubeArray();
  Serial.print("Rebuilding Cube...");
  Serial.println(myCube.rebuildFromCubeArray());
  Serial.println("Rebuild complete, printing...");
  Serial.println("\nColor Cube Array:");
  printOrientation();
  printColorCubeArray();
}

void loop() {
  Serial.println("Code finished, restart program.");
  while(Serial.available()) {
    Serial.read();
  }
  while(!Serial.available());

}

void printFace(char color) {
  Serial.print("Face: ");
  Serial.print(color);
  Serial.print("\t(");

  switch (color) {
    case 'R':
      for (int i = 0; i < 9; i++) {
        Serial.print(myCube.redSide[i]);
        Serial.print(" ");
      }
      break;

    case 'O':
      for (int i = 0; i < 9; i++) {
        Serial.print(myCube.orangeSide[i]);
        Serial.print(" ");
      }
      break;

  }
  Serial.println(")");
}

void printOrientation() {
  Serial.print("Left: ");
  Serial.print(leftCol);
  Serial.print("\tBack: ");
  Serial.print(backCol);
  Serial.print("\t URFDLB: (");

  for (int i = 0; i < 6; i++) {
    Serial.print(myCube.orientation[i]);
  }

  Serial.println(")");
}

void printUnorientedCubeArray() {
  Serial.print("Unoriented Cube Array: [");
  for (int i = 0; i < 54; i++) {
    Serial.print(myCube.unorientedCubeArray[i]);
  }
  Serial.println("]\n");
}

void printColorCubeArray() {
  Serial.print("Final Cube Array: \n");
  for (int i = 0; i < 54; i++) {
    Serial.print(myCube.colorCubeArray[i]);
    if (((i + 1) % 9) == 0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}

void printCubeArray() {
  Serial.print("Final Cube Array: \n");
  for (int i = 0; i < 54; i++) {
    Serial.print(myCube.cubeArray[i]);
    if (((i + 1) % 9) == 0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}
