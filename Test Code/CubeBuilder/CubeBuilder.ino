#include "Cube.h"
Cube myCube;

char leftCol;
char backCol;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial.available());
  Serial.println("Starting...");

// Testing Cube Array
  // Building orientation with 2 moves (in default orientation) (R, U)
  myCube.resetCube();
  
  // Set to checkerboard
  myCube.setColorArray('W', "WYWYWYWYW", 'G');
  myCube.setColorArray('B', "BGBGBGBGB", 'R');
  myCube.setColorArray('R', "ROROROROR", 'G');
  myCube.setColorArray('Y', "YWYWYWYWY", 'G');
  myCube.setColorArray('G', "GBGBGBGBG", 'O');
  myCube.setColorArray('O', "ORORORORO", 'B');
  myCube.setOrientation('G', 'O');
  Serial.print("Solved (Oriented: W, B): ");
  Serial.println(myCube.buildCubeArray());
  printCubeArray();
  Serial.print("Rotating:");
  myCube.rotU(2);
  rotPrintFun();
  myCube.rotD(2);
  rotPrintFun();
  myCube.rotR(2);
  rotPrintFun();
  myCube.rotL(2);
  rotPrintFun();
  myCube.rotF(2);
  rotPrintFun();
  myCube.rotB(2);
  rotPrintFun();
  
  
}

void loop() {
  // put your main code here, to run repeatedly:

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
  for(int i = 0; i < 54; i++) {
    Serial.print(myCube.unorientedCubeArray[i]);
  }
  Serial.println("]\n");
}

void printColorCubeArray() {
  Serial.print("Final Cube Array: \n");
  for(int i = 0; i < 54; i++) {
    Serial.print(myCube.colorCubeArray[i]);
    if (((i+1)%9)==0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}

void printCubeArray() {
  Serial.print("Final Cube Array: \n");
  for(int i = 0; i < 54; i++) {
    Serial.print(myCube.cubeArray[i]);
    if (((i+1)%9)==0) {
      Serial.println(" ");
    }
  }
  Serial.println("\n");
}

void rotPrintFun() {
  Serial.println("Rotation completed, printing:");
  Serial.print("Rebuilding Cube: ");
  Serial.println(myCube.rebuildFromCubeArray());
  Serial.println("Rebuild complete");
  Serial.println("\nColor Cube Array");
  printOrientation();
  printColorCubeArray();
}
